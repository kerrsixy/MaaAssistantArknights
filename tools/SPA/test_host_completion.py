import contextlib
import copy
import io
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import observe_live
from spa_checkpoint import verified_lifecycle_receipt
from spa_host_completion import audit_host_completion, require_host_completion


FIXTURE = Path(__file__).parent / "fixtures" / "batch1_terminal_sequence.json"


def archived_failure():
    return [
        record["event"]
        for record in json.loads(FIXTURE.read_text(encoding="utf-8"))["records"]
    ]


def root_completed(task_id=1):
    return {
        "message": 10002,
        "details": {"taskchain": "StrongholdProtocol", "taskid": task_id},
    }


def synthetic_completed_runs(count):
    """Positive control using the archived first child's event shapes, not wins."""
    original = archived_failure()
    first = next(
        event["details"]["run_execution_id"]
        for event in original
        if event["details"].get("what") == "SPARunExecutionStarted"
    )
    child = [
        event for event in original if event["details"].get("run_execution_id") == first
    ]
    progress = next(
        event
        for event in original
        if event["details"].get("what") == "SPABatchProgress"
    )
    result = []
    for index in range(1, count + 1):
        current = copy.deepcopy(child)
        for event in current:
            envelope = event["details"]
            envelope["run_id"] = envelope["batch_run_id"] = f"synthetic-{index}"
            envelope["run_execution_id"] = f"synthetic-execution-{index}"
            if "run_id" in envelope["details"]:
                envelope["details"]["run_id"] = envelope["run_id"]
        result.extend(current)
        if count > 1:
            current_progress = copy.deepcopy(progress)
            current_progress["details"]["details"].update(
                requested_runs=count, settled_runs=index
            )
            result.append(current_progress)
    result.append(root_completed())
    return result


class HostCompletionTests(unittest.TestCase):
    def test_real_batch_failure_keeps_the_first_settlement_without_success_exit(self):
        events = archived_failure()
        original = copy.deepcopy(events)
        # This actual run had no completed root. The stale promotion branch,
        # rather than verified_lifecycle_receipt, was its old exit-0 path.
        self.assertIsNone(verified_lifecycle_receipt(events))
        self.assertTrue(
            any(
                event["details"].get("what") == "SPAPromotionReconciliationRequired"
                for event in events
            )
        )
        audit = audit_host_completion(events, 3)
        self.assertTrue(audit["host_failure"])
        self.assertFalse(audit["batch_complete"])
        self.assertEqual(audit["native_reported_settled_runs"], 1)
        self.assertEqual(len(audit["settlement_observations"]), 1)
        observation = audit["settlement_observations"][0]["observation"]
        self.assertEqual(observation["passed_rounds"], 9)
        self.assertTrue(observation["result_completed"])
        self.assertFalse(audit["victory_acceptance_evaluated"])
        self.assertIn("SPABatchStopped", audit["failure_reasons"])
        self.assertIn("SPALifecycleIncomplete", audit["failure_reasons"])
        self.assertIn("native_execution_failed_or_stopped", audit["failure_reasons"])
        with self.assertRaises(RuntimeError):
            require_host_completion(audit)
        self.assertEqual(events, original)

    def test_an_old_completed_receipt_cannot_close_an_incomplete_batch(self):
        events = synthetic_completed_runs(1)
        self.assertIsNotNone(verified_lifecycle_receipt(events))
        self.assertTrue(audit_host_completion(events, 3)["host_failure"])

    def test_explicit_failure_outweighs_a_receipt_even_with_completed_root(self):
        failed = archived_failure()
        markers = [
            event
            for event in failed
            if event["details"].get("what")
            in ("SPABatchStopped", "SPALifecycleIncomplete", "SPARunExecutionFinished")
            and (
                event["details"].get("what") != "SPARunExecutionFinished"
                or event["details"]["details"].get("runner_success") is False
            )
        ]
        for marker in markers:
            with self.subTest(what=marker["details"]["what"]):
                events = synthetic_completed_runs(1) + [marker]
                self.assertIsNotNone(verified_lifecycle_receipt(events))
                with self.assertRaises(RuntimeError):
                    require_host_completion(audit_host_completion(events, 1))

    def test_single_and_all_three_settled_runs_remain_successful(self):
        for count in (1, 3):
            with self.subTest(count=count):
                events = synthetic_completed_runs(count)
                audit = audit_host_completion(events, count)
                require_host_completion(audit)
                self.assertEqual(audit["batch_complete"], count > 1)
                self.assertEqual(len(audit["settlement_observations"]), count)
                self.assertIsNotNone(verified_lifecycle_receipt(events))

    def test_final_batch_progress_needs_later_matching_outer_completion(self):
        events = synthetic_completed_runs(3)
        for invalid in (
            events[:-1],
            events[:-1] + [root_completed(2)],
            [root_completed()] + events[:-1],
        ):
            with self.subTest(events=invalid[-1]["details"].get("what")):
                self.assertTrue(audit_host_completion(invalid, 3)["host_failure"])

    def test_three_run_progress_does_not_satisfy_a_different_request(self):
        audit = audit_host_completion(synthetic_completed_runs(3), 5)
        self.assertIn("batch_count_mismatch", audit["failure_reasons"])
        self.assertTrue(audit["host_failure"])

    def fake_host(self, events, count):
        """Replay real main/callback/result handling without a DLL or device."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = {
                "run_id": "synthetic-host",
                "run_count": count,
                "mode": "quick",
                "difficulty": -1,
                "auto_run": True,
                "manage_lifecycle": True,
            }
            policy_file = root / "policy.json"
            policy_file.write_text(json.dumps(policy), encoding="utf-8")
            argv = [
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
            callback = []

            def start():
                for event in events:
                    callback[0](
                        event["message"],
                        json.dumps(event["details"], ensure_ascii=False).encode(
                            "utf-8"
                        ),
                        None,
                    )
                return True

            host = types.SimpleNamespace(
                connect=Mock(return_value=True),
                append_task=Mock(return_value=1),
                start=Mock(side_effect=start),
                running=Mock(return_value=False),
                stop=Mock(),
                get_image=Mock(return_value=None),
            )

            class FakeAsst:
                CallBackType = staticmethod(lambda fn: fn)
                load = staticmethod(lambda *args, **kwargs: True)

                def __new__(cls, fn):
                    callback.append(fn)
                    return host

            messages = types.SimpleNamespace(
                TaskChainCompleted=types.SimpleNamespace(value=10002),
                TaskChainError=types.SimpleNamespace(value=10000),
                SubTaskError=types.SimpleNamespace(value=20000),
            )
            modules = {
                "asst": types.ModuleType("asst"),
                "asst.asst": types.SimpleNamespace(Asst=FakeAsst),
                "asst.utils": types.SimpleNamespace(Message=messages),
            }
            with (
                patch.object(sys, "argv", argv),
                patch.object(sys, "path", sys.path.copy()),
                patch.dict(sys.modules, modules),
                patch.object(observe_live, "authorize_client", return_value="fake"),
                patch.object(
                    observe_live.subprocess,
                    "run",
                    return_value=types.SimpleNamespace(stdout=""),
                ),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                error = None
                try:
                    observe_live.main()
                except RuntimeError as caught:
                    error = caught
            host.stop.assert_called_once()
            files = {
                path.name: json.loads(path.read_text(encoding="utf-8"))
                for path in (root / "run").glob("*.json")
            }
            self.assertEqual(files["events.json"], events)
            return error, files

    def test_real_main_rejects_old_promotion_and_preserves_native_settlement(self):
        error, files = self.fake_host(archived_failure(), 3)
        self.assertIsInstance(error, RuntimeError)
        self.assertIn("SPABatchStopped", str(error))
        self.assertNotIn("nonterminal-audit.json", files)
        self.assertEqual(
            files["host-completion-audit.json"]["native_reported_settled_runs"], 1
        )
        self.assertTrue(files["promotion-audit.json"])

    def test_real_main_rejects_old_receipt_as_batch_completion(self):
        error, files = self.fake_host(synthetic_completed_runs(1), 3)
        self.assertIsInstance(error, RuntimeError)
        self.assertIn("batch_not_completed", str(error))
        self.assertNotIn("result-audit.json", files)

    def test_real_main_keeps_single_and_complete_batch_success(self):
        for count in (1, 3):
            with self.subTest(count=count):
                error, files = self.fake_host(synthetic_completed_runs(count), count)
                self.assertIsNone(error)
                self.assertTrue(files["result-audit.json"]["result_completed"])
                self.assertFalse(files["host-completion-audit.json"]["host_failure"])


if __name__ == "__main__":
    unittest.main()
