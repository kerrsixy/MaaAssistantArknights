"""No-device checks for native completion, serial dispatch and account continuity."""

import contextlib
import io
import json
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import blackflow_auto_rounds as rounds
import blackflow_live_run as live


def terminal_outcome(uid="280875771", game_pass=False, difficulty=9):
    return {
        "task_exit_code": 0,
        "finished": True,
        "settlement_required": True,
        "stop_requested": False,
        "workflow_error_callbacks": 0,
        "pause_callbacks": 0,
        "fresh_exploration_starts": 1,
        "assisted_roster_restorations": 0,
        "final_lobby": {
            "ok": True,
            "best": {"template": "BlackFlow@Roguelike@StartExplore.png"},
        },
        "settlements": [{"game_pass": game_pass, "difficulty": difficulty}],
        "account_bindings": [
            {"uid": uid, "client": "Official", "cache_file": "same-account.json"}
        ],
    }


class TerminalBatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def driver(self, running):
        driver = object.__new__(live.LiveDriver)
        driver.out_dir = self.root
        driver._done = threading.Event()
        driver.running = Mock(side_effect=running)
        driver.stop = Mock()
        return driver

    def test_unlimited_wait_requires_native_idle_after_completion_event(self):
        driver = self.driver([True, True, False])
        driver._done.set()
        with patch.object(live.time, "sleep") as sleep:
            self.assertTrue(driver.wait(None))
        self.assertEqual(driver.running.call_count, 3)
        self.assertEqual(sleep.call_count, 2)
        driver.stop.assert_not_called()

    def test_zero_api_wait_is_still_an_immediate_timeout(self):
        driver = self.driver([True])
        self.assertFalse(driver.wait(0))
        driver.running.assert_not_called()

    def test_negative_wait_fails_before_native_calls(self):
        driver = self.driver([True])
        with self.assertRaises(ValueError):
            driver.wait(-1)
        driver.running.assert_not_called()

    def test_unlimited_wait_respects_stop_without_resuming(self):
        driver = self.driver([True])
        (self.root / "stop.request").touch()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(driver.wait(None))
        driver.stop.assert_called_once()

    def test_summary_retains_typed_receipts_and_errors(self):
        driver = object.__new__(live.LiveDriver)
        driver._lock = threading.Lock()
        extra = live.MSG_SUBTASK_EXTRA_INFO
        driver._records = [
            {
                "msg": extra,
                "details": json.dumps(
                    {"what": "RoguelikeSettlement", "details": {"game_pass": False}}
                ),
            },
            {
                "msg": extra,
                "details": json.dumps(
                    {"what": "RoguelikeSettlement", "details": {"game_pass": "true"}}
                ),
            },
            {
                "msg": extra,
                "details": json.dumps(
                    {
                        "what": "BlackFlowAccountBound",
                        "details": {
                            "uid": "u",
                            "client": "Official",
                            "cache_file": "cache",
                        },
                    }
                ),
            },
            {
                "msg": extra,
                "details": json.dumps(
                    {"what": "StageInfo", "details": {"name": "stage"}}
                ),
            },
            {
                "msg": extra,
                "details": json.dumps({"what": "BlackFlowBattlePaused", "details": {}}),
            },
            {"msg": live.MSG_SUBTASK_ERROR, "details": "bad JSON"},
            {"msg": extra, "details": "[]"},
        ]
        result = driver.result_summary()
        self.assertEqual(result["settlements"], [{"game_pass": False}])
        self.assertEqual(result["workflow_error_callbacks"], 1)
        self.assertEqual(result["pause_callbacks"], 1)
        self.assertFalse(result["native_game_pass"])
        self.assertFalse(result["unattended_first_ending_proven"])
        self.assertEqual(result["battle_stages"], ["stage"])
        self.assertEqual(result["account_bindings"][0]["uid"], "u")

    def test_fresh_capture_completes_before_reading_cached_image(self):
        driver = object.__new__(live.LiveDriver)
        driver._lock = threading.Lock()
        driver._records = []
        driver._handle = object()
        driver._lib = Mock()

        def capture(*_args):
            driver._records.append(
                {
                    "msg": live.MSG_ASYNC_CALL_INFO,
                    "details": json.dumps(
                        {
                            "what": "Screencap",
                            "async_call_id": 17,
                            "details": {"ret": True},
                        }
                    ),
                }
            )
            return 17

        driver._lib.AsstAsyncScreencap.side_effect = capture

        def image(name):
            driver._lib.AsstAsyncScreencap.assert_called_once_with(driver._handle, True)
            return self.root / name

        driver.snapshot = Mock(side_effect=image)
        self.assertEqual(driver.fresh_snapshot("final.png"), self.root / "final.png")

    def test_rejected_capture_never_reads_an_old_cached_image(self):
        driver = object.__new__(live.LiveDriver)
        driver._lock = threading.Lock()
        driver._records = []
        driver._handle = object()
        driver._lib = Mock()
        driver._lib.AsstAsyncScreencap.return_value = 0
        driver.snapshot = Mock()
        self.assertIsNone(driver.fresh_snapshot())
        driver.snapshot.assert_not_called()

    def test_capture_id_without_matching_success_never_reads_cached_image(self):
        for receipt in (None, False, "true", 1):
            with self.subTest(receipt=receipt):
                driver = object.__new__(live.LiveDriver)
                driver._lock = threading.Lock()
                driver._records = []
                driver._handle = object()
                driver._lib = Mock()
                driver.snapshot = Mock()

                def capture(*_args):
                    if receipt is not None:
                        driver._records.append(
                            {
                                "msg": live.MSG_ASYNC_CALL_INFO,
                                "details": json.dumps(
                                    {
                                        "what": "Screencap",
                                        "async_call_id": 17,
                                        "details": {"ret": receipt},
                                    }
                                ),
                            }
                        )
                    return 17

                driver._lib.AsstAsyncScreencap.side_effect = capture
                with (
                    patch.object(live.time, "monotonic", side_effect=[0, 0, 3]),
                    patch.object(live.time, "sleep"),
                ):
                    self.assertIsNone(driver.fresh_snapshot())
                driver.snapshot.assert_not_called()

    def test_capture_ignores_old_and_unrelated_success_receipts(self):
        driver = object.__new__(live.LiveDriver)
        driver._lock = threading.Lock()
        driver._handle = object()
        old = {
            "msg": live.MSG_ASYNC_CALL_INFO,
            "details": json.dumps(
                {"what": "Screencap", "async_call_id": 17, "details": {"ret": True}}
            ),
        }
        driver._records = [old]
        driver._lib = Mock()
        driver.snapshot = Mock()

        def capture(*_args):
            for what, call_id in (("Click", 17), ("Screencap", 18)):
                driver._records.append(
                    {
                        "msg": live.MSG_ASYNC_CALL_INFO,
                        "details": json.dumps(
                            {
                                "what": what,
                                "async_call_id": call_id,
                                "details": {"ret": True},
                            }
                        ),
                    }
                )
            return 17

        driver._lib.AsstAsyncScreencap.side_effect = capture
        with (
            patch.object(live.time, "monotonic", side_effect=[0, 0, 3]),
            patch.object(live.time, "sleep"),
        ):
            self.assertIsNone(driver.fresh_snapshot())
        driver.snapshot.assert_not_called()

    def test_cli_missing_initial_capture_does_not_start_a_task(self):
        driver = Mock()
        driver.fresh_snapshot.return_value = None
        driver.save.return_value = (self.root / "callbacks.jsonl", 0)
        with (
            patch("sys.argv", ["live", "--out", str(self.root)]),
            patch.object(live, "LiveDriver", return_value=driver),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(live.main(), 4)
        driver.append_task.assert_not_called()
        driver.start.assert_not_called()

    def test_probe_without_capture_does_not_claim_success(self):
        driver = Mock()
        driver.fresh_snapshot.return_value = None
        driver.save.return_value = (self.root / "callbacks.jsonl", 0)
        with (
            patch("sys.argv", ["live", "--probe", "--out", str(self.root)]),
            patch.object(live, "LiveDriver", return_value=driver),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(live.main(), 4)
        driver.start.assert_not_called()

    def test_cli_preserves_saved_exploration_before_appending_task(self):
        import cv2
        import numpy as np
        import blackflow_state_check as state

        image = self.root / "fixture.png"
        cv2.imencode(".png", np.zeros((720, 1280, 3), dtype=np.uint8))[1].tofile(image)
        out = self.root / "new-output"
        out.mkdir()
        driver = Mock()
        driver.fresh_snapshot.return_value = image
        driver.save.return_value = (out / "callbacks.jsonl", 0)
        with (
            patch("sys.argv", ["live", "--out", str(out)]),
            patch.object(live, "LiveDriver", return_value=driver),
            patch.object(
                state,
                "check",
                return_value={
                    "ok": True,
                    "best": {"template": "BlackFlow@Roguelike@Continue.png"},
                },
            ),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(live.main(), 4)
        driver.append_task.assert_not_called()
        driver.start.assert_not_called()

    def test_cli_preserves_existing_evidence_before_loading_core(self):
        marker = self.root / "callbacks.jsonl"
        marker.write_text("original evidence", encoding="utf-8")
        with (
            patch("sys.argv", ["live", "--out", str(self.root)]),
            patch.object(live, "LiveDriver") as driver,
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(live.main(), 2)
        driver.assert_not_called()
        self.assertEqual(marker.read_text(encoding="utf-8"), "original evidence")

    def test_capture_waits_for_its_delayed_callback(self):
        driver = object.__new__(live.LiveDriver)
        driver._lock = threading.Lock()
        driver._records = []
        driver._handle = object()
        driver._lib = Mock()
        driver._lib.AsstAsyncScreencap.return_value = 17
        driver.snapshot = Mock(return_value=self.root / "fresh.png")

        def deliver(_seconds):
            driver._records.append(
                {
                    "msg": live.MSG_ASYNC_CALL_INFO,
                    "details": json.dumps(
                        {
                            "what": "Screencap",
                            "async_call_id": 17,
                            "details": {"ret": True},
                        }
                    ),
                }
            )

        with patch.object(live.time, "sleep", side_effect=deliver):
            self.assertEqual(driver.fresh_snapshot(), self.root / "fresh.png")
        driver.snapshot.assert_called_once_with("snapshot.png")

    def test_missing_or_contradictory_receipts_are_rejected(self):
        mutations = [
            {"settlements": [{"game_pass": False}]},
            {"settlements": [{"game_pass": False, "difficulty": True}]},
            {
                "final_lobby": {
                    "ok": True,
                    "best": {"template": "BlackFlow@Roguelike@Continue.png"},
                }
            },
            {"finished": False},
            {"task_exit_code": 2},
            {"stop_requested": True},
            {"fresh_exploration_starts": 0},
            {"fresh_exploration_starts": True},
            {"assisted_roster_restorations": 1},
            {"assisted_continuation": True},
            {"unattended_acceptance_eligible": False},
            {"task_exit_code": False},
            {"workflow_error_callbacks": False},
            {"pause_callbacks": 1},
            {"workflow_error_callbacks": 1},
            {"settlements": []},
            {"settlements": [{"game_pass": 1}]},
            {"final_lobby": {"ok": False}},
            {"account_bindings": []},
            {
                "account_bindings": [
                    {"uid": "u", "client": "Bilibili", "cache_file": "cache"}
                ]
            },
            {
                "account_bindings": [
                    {"uid": "", "client": "Official", "cache_file": "cache"}
                ]
            },
        ]
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                rounds.verified_round({**terminal_outcome(), **mutation}, "Official")

    def test_account_change_is_rejected_within_and_between_rounds(self):
        with self.assertRaises(ValueError):
            rounds.verified_round(terminal_outcome("other"), "Official", "280875771")
        outcome = terminal_outcome()
        outcome["account_bindings"].append(
            {"uid": "other", "client": "Official", "cache_file": "other.json"}
        )
        with self.assertRaises(ValueError):
            rounds.verified_round(outcome, "Official")

    def run_batch(self, outcomes, codes=None, options=()):
        commands = []

        def call(command, **kwargs):
            index = len(commands)
            commands.append(command)
            out = Path(command[command.index("--out") + 1])
            out.mkdir()
            if outcomes[index] is not None:
                (out / "outcome.json").write_text(
                    json.dumps(outcomes[index]), encoding="utf-8"
                )
            return codes[index] if codes else 0

        with (
            patch(
                "sys.argv",
                [
                    "rounds",
                    "--rounds",
                    str(len(outcomes)),
                    "--tag",
                    "test",
                    "--core-dir",
                    "qualified-core",
                    "--address",
                    "device1",
                    *options,
                ],
            ),
            patch.object(rounds, "LOG_ROOT", self.root),
            patch.object(
                rounds,
                "check_state",
                return_value={
                    "ok": True,
                    "best": {"template": "BlackFlow@Roguelike@StartExplore.png"},
                },
            ) as preflight,
            patch.object(rounds.subprocess, "call", side_effect=call),
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            code = rounds.main()
        report = json.loads(
            (self.root / "blackflow_regression_test.json").read_text(encoding="utf-8")
        )
        return code, commands, report, preflight

    def test_natural_defeat_can_continue_with_first_round_scan_and_same_cache(self):
        code, commands, report, preflight = self.run_batch(
            [terminal_outcome(), terminal_outcome()]
        )
        self.assertEqual(code, 0)
        self.assertEqual(len(commands), 2)
        self.assertIn("--scan-account-modules", commands[0])
        self.assertIn("--no-scan-account-modules", commands[1])
        self.assertIn("--account-cache-file", commands[1])
        self.assertEqual(
            commands[1][commands[1].index("--account-cache-file") + 1],
            "same-account.json",
        )
        for command in commands:
            self.assertEqual(command[command.index("--core-dir") + 1], "qualified-core")
            self.assertEqual(command[command.index("--address") + 1], "device1")
            self.assertEqual(command[command.index("--seconds") + 1], "0")
        self.assertEqual(preflight.call_count, 2)
        preflight.assert_called_with(address="device1")
        self.assertEqual(report["native_game_pass_count"], 0)
        self.assertFalse(report["stable_n10_proven"])

    def test_saved_lobby_never_dispatches_a_fresh_sample(self):
        with (
            patch("sys.argv", ["rounds", "--rounds", "2", "--tag", "saved"]),
            patch.object(rounds, "LOG_ROOT", self.root),
            patch.object(
                rounds,
                "check_state",
                return_value={
                    "ok": True,
                    "best": {"template": "BlackFlow@Roguelike@Continue.png"},
                },
            ),
            patch.object(rounds.subprocess, "call") as dispatch,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(rounds.main(), 4)
        dispatch.assert_not_called()
        report = json.loads(
            (self.root / "blackflow_regression_saved.json").read_text(encoding="utf-8")
        )
        self.assertEqual(report["completed_samples"], 0)

    def test_explicit_difficulty_mismatch_stops_before_next_sample(self):
        code, commands, report, _ = self.run_batch(
            [terminal_outcome(difficulty=9), terminal_outcome(difficulty=10)],
            options=["--difficulty", "10"],
        )
        self.assertEqual(code, 3)
        self.assertEqual(len(commands), 1)
        self.assertEqual(report["completed_samples"], 0)

    def test_native_n10_is_recorded_without_certifying_stability(self):
        code, commands, report, _ = self.run_batch(
            [terminal_outcome(game_pass=True, difficulty=10)],
            options=["--difficulty", "10"],
        )
        self.assertEqual(code, 0)
        self.assertEqual(report["samples"][0]["native_difficulty"], 10)
        self.assertFalse(report["stable_n10_proven"])

    def test_error_or_missing_outcome_never_launches_a_second_round(self):
        for index, (first, codes) in enumerate(
            [(terminal_outcome(), [2, 0]), (None, None), ({}, None)]
        ):
            with self.subTest(index=index):
                if index:
                    self.tear_down_evidence()
                code, commands, report, _ = self.run_batch(
                    [first, terminal_outcome()], codes
                )
                self.assertEqual(code, 3)
                self.assertEqual(len(commands), 1)
                self.assertEqual(report["completed_samples"], 0)

    def tear_down_evidence(self):
        # Each subtest uses a new temporary root; preserve the preceding evidence.
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_second_round_identity_mismatch_stops_before_third(self):
        code, commands, report, _ = self.run_batch(
            [terminal_outcome(), terminal_outcome("other"), terminal_outcome()]
        )
        self.assertEqual(code, 3)
        self.assertEqual(len(commands), 2)
        self.assertEqual(report["completed_samples"], 1)

    def test_explicit_scan_off_and_other_runtime_choices_are_propagated(self):
        code, commands, report, _ = self.run_batch(
            [terminal_outcome(game_pass=True)],
            options=[
                "--no-scan-account-modules",
                "--no-life-game",
                "--mumu-extras",
                "--account-cache-file",
                "same-account.json",
            ],
        )
        self.assertEqual(code, 0)
        for flag in ["--no-scan-account-modules", "--no-life-game", "--mumu-extras"]:
            self.assertIn(flag, commands[0])
        self.assertEqual(report["native_game_pass_count"], 1)
        self.assertFalse(report["unattended_first_ending_proven"])


if __name__ == "__main__":
    unittest.main()
