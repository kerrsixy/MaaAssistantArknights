import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from blackflow_live_run import (
    MSG_ALL_TASKS_COMPLETED,
    MSG_INTERNAL_ERROR,
    MSG_SUBTASK_COMPLETED,
    MSG_SUBTASK_ERROR,
    MSG_SUBTASK_EXTRA_INFO,
    MSG_TASK_CHAIN_COMPLETED,
    MSG_TASK_CHAIN_ERROR,
    LiveDriver,
)


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class LiveWaitTests(unittest.TestCase):
    def make_driver(self, out_dir):
        driver = LiveDriver.__new__(LiveDriver)
        driver.out_dir = Path(out_dir)
        driver._done = threading.Event()
        driver._lock = threading.Lock()
        driver._records = []
        driver.stop = Mock()
        return driver

    def test_completion_callback_precedes_worker_idle(self):
        with tempfile.TemporaryDirectory() as directory:
            driver = self.make_driver(directory)
            driver._done.set()
            driver.running = Mock(side_effect=[True, True, False])
            clock = FakeClock()
            with (
                patch("blackflow_live_run.time.monotonic", clock.monotonic),
                patch("blackflow_live_run.time.sleep", clock.sleep),
            ):
                self.assertTrue(driver.wait(1))
            self.assertEqual(driver.running.call_count, 3)
            self.assertGreater(clock.now, 0)
            driver.stop.assert_not_called()

    def test_callback_does_not_hide_a_worker_that_never_stops(self):
        with tempfile.TemporaryDirectory() as directory:
            driver = self.make_driver(directory)
            driver._done.set()
            driver.running = Mock(return_value=True)
            clock = FakeClock()
            with (
                patch("blackflow_live_run.time.monotonic", clock.monotonic),
                patch("blackflow_live_run.time.sleep", clock.sleep),
            ):
                self.assertFalse(driver.wait(0.1))
            self.assertGreaterEqual(clock.now, 0.1)

    def test_stop_request_is_honored_during_callback_idle_race(self):
        with tempfile.TemporaryDirectory() as directory:
            driver = self.make_driver(directory)
            driver._done.set()
            driver.running = Mock(return_value=True)
            (driver.out_dir / "stop.request").touch()
            self.assertFalse(driver.wait(1))
            driver.stop.assert_called_once()

    def test_idle_worker_does_not_require_a_callback(self):
        with tempfile.TemporaryDirectory() as directory:
            driver = self.make_driver(directory)
            driver.running = Mock(return_value=False)
            self.assertTrue(driver.wait(1))
            driver.stop.assert_not_called()

    def test_chain_error_is_not_hidden_by_all_tasks_completed(self):
        driver = self.make_driver(".")
        driver._records = [
            {"msg": MSG_TASK_CHAIN_ERROR},
            {"msg": MSG_ALL_TASKS_COMPLETED},
        ]
        self.assertEqual(driver.completion_exit_code(True), 2)

    def test_internal_error_is_not_hidden_by_completed_chain(self):
        driver = self.make_driver(".")
        driver._records = [
            {"msg": MSG_INTERNAL_ERROR},
            {"msg": MSG_TASK_CHAIN_COMPLETED},
        ]
        self.assertEqual(driver.completion_exit_code(True), 2)

    def test_worker_must_finish_after_completion_callback(self):
        driver = self.make_driver(".")
        driver._records = [{"msg": MSG_TASK_CHAIN_COMPLETED}]
        self.assertEqual(driver.completion_exit_code(False), 1)

    def test_idle_and_all_tasks_message_do_not_prove_chain_completion(self):
        driver = self.make_driver(".")
        self.assertEqual(driver.completion_exit_code(True), 1)
        driver._records = [{"msg": MSG_ALL_TASKS_COMPLETED}]
        self.assertEqual(driver.completion_exit_code(True), 1)

    def test_completed_chain_has_zero_process_status_not_a_win_claim(self):
        driver = self.make_driver(".")
        driver._records = [
            {"msg": MSG_TASK_CHAIN_COMPLETED},
            {"msg": MSG_ALL_TASKS_COMPLETED},
        ]
        self.assertEqual(driver.completion_exit_code(True), 0)

    def test_recovered_subtask_error_does_not_fail_completed_chain(self):
        driver = self.make_driver(".")
        driver._records = [
            {"msg": MSG_SUBTASK_ERROR},
            {"msg": MSG_TASK_CHAIN_COMPLETED},
        ]
        self.assertEqual(driver.completion_exit_code(True), 0)

    def settlement_driver(self, details):
        driver = self.make_driver(".")
        driver._records = [
            {"msg": MSG_SUBTASK_EXTRA_INFO, "details": details},
            {"msg": MSG_TASK_CHAIN_COMPLETED},
        ]
        return driver

    def test_shop_stop_without_settlement_is_not_a_completed_exploration(self):
        driver = self.make_driver(".")
        driver._records = [
            {"msg": MSG_SUBTASK_ERROR},
            {"msg": MSG_TASK_CHAIN_COMPLETED},
        ]
        self.assertEqual(driver.completion_exit_code(True, require_settlement=True), 1)

    def test_lobby_without_settlement_does_not_prove_exploration_completion(self):
        driver = self.make_driver(".")
        driver._records = [{"msg": MSG_TASK_CHAIN_COMPLETED}]
        self.assertEqual(
            driver.completion_exit_code(
                True, require_settlement=True, lobby_verified=True
            ),
            1,
        )

    def test_settlement_without_lobby_keeps_terminal_flow_incomplete(self):
        driver = self.settlement_driver(
            '{"what":"RoguelikeSettlement","details":{"game_pass":false}}'
        )
        self.assertEqual(driver.completion_exit_code(True, require_settlement=True), 1)

    def test_natural_defeat_and_lobby_complete_flow_without_claiming_a_win(self):
        driver = self.settlement_driver(
            '{"what":"RoguelikeSettlement","details":{"game_pass":false}}'
        )
        self.assertEqual(
            driver.completion_exit_code(
                True, require_settlement=True, lobby_verified=True
            ),
            0,
        )

    def test_victory_receipt_and_lobby_complete_terminal_flow(self):
        driver = self.settlement_driver(
            '{"what":"RoguelikeSettlement","details":{"game_pass":true}}'
        )
        self.assertEqual(
            driver.completion_exit_code(
                True, require_settlement=True, lobby_verified=True
            ),
            0,
        )

    def test_confirmed_abandon_invalidates_even_an_earlier_settlement(self):
        for task in ("AbandonConfirm", "BlackFlow@Roguelike@AbandonConfirm"):
            with self.subTest(task=task):
                driver = self.settlement_driver(
                    '{"what":"RoguelikeSettlement","details":{"game_pass":false}}'
                )
                driver._records.append(
                    {
                        "msg": MSG_SUBTASK_COMPLETED,
                        "details": '{"subtask":"ProcessTask","details":{"task":"'
                        + task
                        + '"}}',
                    }
                )
                self.assertEqual(
                    driver.completion_exit_code(
                        True, require_settlement=True, lobby_verified=True
                    ),
                    1,
                )
                self.assertEqual(driver.completion_exit_code(True), 0)

    def test_uncompleted_abandon_recognition_is_not_a_confirmed_abandon(self):
        driver = self.settlement_driver(
            '{"what":"RoguelikeSettlement","details":{"game_pass":false}}'
        )
        driver._records.insert(
            0,
            {
                "msg": 20_001,
                "details": '{"subtask":"ProcessTask","details":{"task":"AbandonConfirm"}}',
            },
        )
        self.assertEqual(
            driver.completion_exit_code(
                True, require_settlement=True, lobby_verified=True
            ),
            0,
        )

    def test_planning_failure_does_not_become_success_after_settlement(self):
        driver = self.settlement_driver(
            '{"what":"RoguelikeSettlement","details":{"game_pass":false}}'
        )
        driver._records.append(
            {
                "msg": MSG_SUBTASK_EXTRA_INFO,
                "details": '{"what":"BlackFlowStrategyResult","details":{"outcome":"planning_failed"}}',
            }
        )
        self.assertEqual(
            driver.completion_exit_code(
                True, require_settlement=True, lobby_verified=True
            ),
            1,
        )

    def test_malformed_and_unverified_settlement_receipts_remain_incomplete(self):
        for details in (
            "not JSON",
            "[]",
            '{"what":"Other","details":{"game_pass":true}}',
            '{"what":"RoguelikeSettlement","details":{}}',
            '{"what":"RoguelikeSettlement","details":{"game_pass":"false"}}',
        ):
            with self.subTest(details=details):
                driver = self.settlement_driver(details)
                self.assertEqual(
                    driver.completion_exit_code(
                        True, require_settlement=True, lobby_verified=True
                    ),
                    1,
                )


if __name__ == "__main__":
    unittest.main()
