import contextlib
import io
import json
import math
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import observe_live
from spa_task_timeout import task_timeout_seconds


class WaitFinished(Exception):
    """End the fake host after its wait, before screenshot/result processing."""


class TaskTimeoutTests(unittest.TestCase):
    def test_auto_budget_matches_actual_count(self):
        self.assertEqual(task_timeout_seconds("auto-preparation", {}), 7200)
        for count, expected in ((1, 7200), (3, 21600), (99_999, 719_992_800)):
            with self.subTest(count=count):
                budget = task_timeout_seconds("auto-preparation", {"run_count": count})
                self.assertIs(type(budget), int)
                self.assertEqual(budget, expected)
                self.assertTrue(math.isfinite(1_000_000.5 + budget))

    def test_invalid_counts_are_not_coerced_or_clamped(self):
        for count in (0, -1, 100_000, 10**100, True, False, "3", 3.0, None, [], {}):
            with self.subTest(count=count), self.assertRaises(ValueError):
                task_timeout_seconds("auto-preparation", {"run_count": count})
        for policy in (None, [], "{}", 3):
            with self.subTest(policy=policy), self.assertRaises(ValueError):
                task_timeout_seconds("auto-preparation", policy)

    def test_other_tasks_keep_existing_budgets(self):
        for task, expected in (
            ("ready", 300),
            ("abandon-home", 180),
            ("observe", 90),
            ("setup-flow", 90),
            ("setup-remaining", 90),
            ("buy", 90),
            ("start-independent", 90),
        ):
            with self.subTest(task=task):
                self.assertEqual(task_timeout_seconds(task), expected)
                self.assertEqual(
                    task_timeout_seconds(task, {"run_count": 99_999}), expected
                )

    @staticmethod
    def argv(root, policy):
        policy_file = root / "policy.json"
        policy_file.write_text(json.dumps(policy), encoding="utf-8")
        return [
            "observe_live.py",
            "--core-dir",
            str(root / "unused-core"),
            "--adb",
            str(root / "unused-adb"),
            "--address",
            "fake-device",
            "--output",
            str(root / "run"),
            "--task",
            "auto-preparation",
            "--policy-file",
            str(policy_file),
        ]

    def test_invalid_count_fails_before_device_or_native_access(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = self.argv(Path(temporary), {"run_count": True})
            with (
                patch.object(sys, "argv", args),
                patch.object(observe_live.subprocess, "run") as adb,
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaises(SystemExit) as stopped,
            ):
                observe_live.main()
            self.assertEqual(stopped.exception.code, 2)
            adb.assert_not_called()

    def fake_wait(self, count, completion_time):
        """Run the real main/callback/wait path with no DLL or device access."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = self.argv(
                root, {"run_count": count, "auto_run": True, "manage_lifecycle": True}
            )
            clock = [0.0]
            emitted = []
            callback = []

            def running():
                # Per-game callbacks occur while the native parent remains busy.
                for game in range(1, min(int(clock[0] // 3600), count) + 1):
                    if game not in emitted:
                        emitted.append(game)
                        callback[0](
                            20003,
                            json.dumps(
                                {
                                    "what": "SPALifecycleCompleted",
                                    "details": {"return_home_verified": True},
                                    "batch_run_id": f"fake-batch-{game}",
                                }
                            ).encode(),
                            None,
                        )
                return clock[0] < completion_time

            host = types.SimpleNamespace(
                connect=Mock(return_value=True),
                append_task=Mock(return_value=1),
                start=Mock(return_value=True),
                running=running,
                stop=Mock(),
                get_image=Mock(side_effect=WaitFinished),
            )

            class FakeAsst:
                CallBackType = staticmethod(lambda fn: fn)
                load = staticmethod(lambda *args, **kwargs: True)

                def __new__(cls, fn):
                    callback.append(fn)
                    return host

            def sleep(_):
                clock[0] += 3000

            package = "com.hypergryph.arknights.bilibili"
            adb_results = [
                types.SimpleNamespace(stdout=f"topResumedActivity={package}/game"),
                types.SimpleNamespace(stdout=f"mCurrentFocus={package}/game"),
            ]
            # Runtime imports are replaced before main; no native module is loaded.
            modules = {
                "asst": types.ModuleType("asst"),
                "asst.asst": types.SimpleNamespace(Asst=FakeAsst),
                "asst.utils": types.SimpleNamespace(Message=object()),
            }
            with (
                patch.object(sys, "argv", args),
                patch.object(sys, "path", sys.path.copy()),
                patch.dict(sys.modules, modules),
                patch.object(observe_live.subprocess, "run", side_effect=adb_results),
                patch.object(
                    observe_live.time, "monotonic", side_effect=lambda: clock[0]
                ),
                patch.object(observe_live.time, "sleep", side_effect=sleep),
            ):
                error = None
                try:
                    observe_live.main()
                except (WaitFinished, RuntimeError) as caught:
                    error = caught
            host.stop.assert_called_once()
            saved = json.loads((root / "run/events.json").read_text(encoding="utf-8"))
            return error, clock[0], emitted, saved

    def test_three_games_continue_past_single_game_budget_until_parent_completes(self):
        error, elapsed, emitted, saved = self.fake_wait(3, 12_000)
        self.assertIsInstance(error, WaitFinished)
        self.assertEqual(elapsed, 12_000)
        self.assertEqual(emitted, [1, 2, 3])
        self.assertEqual(len(saved), 3)

    def test_single_game_still_times_out_if_native_never_finishes(self):
        error, elapsed, _, _ = self.fake_wait(1, 15_000)
        self.assertIsInstance(error, RuntimeError)
        self.assertIn("timed out", str(error))
        self.assertEqual(elapsed, 9000)

    def test_batch_still_has_a_total_deadline(self):
        error, elapsed, _, _ = self.fake_wait(3, 30_000)
        self.assertIsInstance(error, RuntimeError)
        self.assertIn("timed out", str(error))
        self.assertEqual(elapsed, 24_000)


if __name__ == "__main__":
    unittest.main()
