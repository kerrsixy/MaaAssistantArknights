import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from blackflow_live_run import (
    MSG_ALL_TASKS_COMPLETED,
    MSG_INTERNAL_ERROR,
    MSG_SUBTASK_ERROR,
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


if __name__ == "__main__":
    unittest.main()
