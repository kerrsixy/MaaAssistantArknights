import json
import tempfile
import threading
from unittest import mock
import unittest
from pathlib import Path

from blackflow_battle_observation import BattleObservationRecorder


def event(what="StageInfo", name="遗忘时间"):
    return json.dumps(
        {"subtask": "RoguelikeBattleTask", "what": what, "details": {"name": name}}
    )


class ObservationFixture:
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.recorder = BattleObservationRecorder(self.root, 2, 3)
        self.calls = 0

    def snapshot(self, name):
        self.calls += 1
        path = self.root / name
        path.write_bytes(b"same cached image")
        return path

    def rows(self):
        path = self.recorder.directory / "observations.jsonl"
        return (
            [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            if path.exists()
            else []
        )


class ObservationTests(ObservationFixture, unittest.TestCase):
    def test_no_callback_io_and_no_capture_before_stage(self):
        self.recorder.poll(self.snapshot, 0)
        self.recorder.callback(20_003, event())
        self.assertEqual(self.calls, 0)
        self.assertEqual(list(self.recorder.directory.iterdir()), [])

    def test_sampling_is_bounded_and_duplicate_frames_do_not_prove_motion(self):
        self.recorder.callback(20_003, event())
        for now in (0, 1, 2, 3, 4, 5, 100):
            self.recorder.poll(self.snapshot, now)
        rows = self.rows()
        self.assertEqual(self.calls, 3)
        self.assertEqual(
            [row["distinct_cached_frame"] for row in rows], [True, False, False]
        )
        self.assertTrue(
            all(
                row["classification"] == "unknown"
                and not row["normal_battle_confirmed"]
                for row in rows
            )
        )

    def test_same_stage_new_battle_does_not_inherit_hash_or_label(self):
        self.recorder.callback(20_003, event())
        self.recorder.poll(self.snapshot, 0)
        self.recorder.callback(
            20_003, event()
        )  # Duplicate metadata, not another encounter.
        self.recorder.poll(self.snapshot, 2)
        self.recorder.callback(20_002, event())
        self.recorder.callback(20_003, event())
        self.recorder.poll(self.snapshot, 3)
        self.assertEqual([row["battle"] for row in self.rows()], [1, 1, 2])
        self.assertTrue(self.rows()[-1]["distinct_cached_frame"])

    def test_capture_crossing_boundary_is_not_attributed_as_confirmed(self):
        self.recorder.callback(20_003, event())

        def racing_snapshot(name):
            self.recorder.callback(20_003, event("RoguelikeCombatEnd"))
            return self.snapshot(name)

        self.recorder.poll(racing_snapshot, 0)
        self.assertFalse(self.rows()[0]["battle_attribution_confirmed"])
        self.recorder.poll(self.snapshot, 2)
        self.assertEqual(self.calls, 1)

    def test_empty_terminal_and_malformed_payloads_cannot_keep_sampling(self):
        self.recorder.callback(20_003, event())
        self.recorder.callback(3, "")
        for payload in ("", "[]", '{"details":[]}'):
            self.recorder.callback(20_003, payload)
        self.recorder.poll(self.snapshot, 0)
        self.assertEqual(self.calls, 0)

    def test_failed_snapshots_are_bounded_and_do_not_escape_into_actor(self):
        self.recorder.callback(20_003, event())

        def fail(_name):
            raise OSError("camera unavailable")

        for now in (0, 2, 4, 6):
            self.recorder.poll(fail, now)
        self.assertEqual(len(self.rows()), 3)
        self.assertTrue(all("error" in row for row in self.rows()))

    def test_invalid_intervals_and_limits_are_rejected(self):
        for value in (0, -1, float("nan"), float("inf")):
            with self.subTest(interval=value), self.assertRaises(ValueError):
                BattleObservationRecorder(self.root, value, 2)
        for value in (0, -1, 1.5, True):
            with self.subTest(limit=value), self.assertRaises(ValueError):
                BattleObservationRecorder(self.root, 2, value)


def terminal_event(task="GamePass", page="1", *, taskid=2, uuid="official-test"):
    return json.dumps(
        {
            "class": "asst::ProcessTask",
            "subtask": "ProcessTask",
            "taskchain": "Roguelike",
            "taskid": taskid,
            "uuid": uuid,
            "details": {
                "task": task,
                "page_id": f"BlackFlow@Roguelike@{task}#{page}",
                "result": {"template": "test-only", "score": 1.0},
            },
        }
    )


def settlement_event(game_pass):
    return json.dumps(
        {
            "class": "asst::RoguelikeSettlementTaskPlugin",
            "subtask": "RoguelikeSettlementTask",
            "taskchain": "Roguelike",
            "taskid": 2,
            "uuid": "official-test",
            "what": "RoguelikeSettlement",
            "details": {"game_pass": game_pass, "floor": 5 if game_pass else 3},
        }
    )


class TerminalEvidenceTests(ObservationFixture, unittest.TestCase):
    def terminal_rows(self, filename="observations.jsonl"):
        path = self.recorder.terminal_directory / filename
        return (
            [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines()]
            if path.exists()
            else []
        )

    def test_terminal_callback_has_no_io_or_snapshot(self):
        with mock.patch.object(Path, "open", side_effect=AssertionError("callback IO")):
            self.recorder.callback(20_001, terminal_event(), now=0)
        self.assertEqual(self.calls, 0)
        self.assertEqual(list(self.recorder.terminal_directory.iterdir()), [])
        self.assertTrue(self.recorder._wake.is_set())

    def test_names_and_native_success_are_candidates_not_clear_proof(self):
        self.recorder.callback(20_001, terminal_event(), now=0)
        self.recorder.poll(self.snapshot, 0)
        self.recorder.callback(20_003, settlement_event(True), now=1)
        self.recorder.poll(self.snapshot, 1)
        rows = self.terminal_rows()
        self.assertEqual(
            [x["kind"] for x in rows],
            ["ending_ui_candidate", "native_settlement_candidate"],
        )
        self.assertTrue(all(not x["first_ending_proven"] for x in rows))
        self.assertTrue(all(not x["independent_fresh_capture_confirmed"] for x in rows))
        self.assertTrue(
            all(x["classification"] == "unverified_terminal_candidate" for x in rows)
        )

    def test_actual_run72_failure_chain_never_turns_confirm_template_into_victory(self):
        path = (
            Path(__file__).parent
            / "testdata/blackflow/run72_failure_settlement_callbacks.json"
        )
        if not path.exists():
            # Compatibility for the original isolated candidate test corpus only.
            path = (
                Path(__file__).parent.parent
                / "inputs/run72_failure_settlement_callbacks.json"
            )
        fixture = json.loads(path.read_text(encoding="utf-8"))
        for i, event in enumerate(fixture):
            self.recorder.callback(event["msg"], event["details"], now=i * 0.1)
            self.recorder.poll(lambda _name: None, i * 0.1)
        rows = self.terminal_rows("events.jsonl")
        native = [x for x in rows if x["kind"] == "native_settlement_candidate"]
        self.assertEqual(len(native), 1)
        self.assertFalse(native[0]["source_callback"]["details"]["game_pass"])
        self.assertFalse(any(x["kind"] == "ending_ui_candidate" for x in rows))
        self.assertTrue(all(not x["first_ending_proven"] for x in rows))
        self.assertTrue(all(x["path"] is None for x in self.terminal_rows()))

    def test_duplicate_callbacks_do_not_restart_window_or_grow_events(self):
        raw = terminal_event()
        self.recorder.callback(20_001, raw, now=0)
        self.recorder.poll(self.snapshot, 0)
        self.recorder.callback(20_002, raw, now=7)
        self.recorder.poll(self.snapshot, 9)
        self.assertEqual(self.calls, 1)
        self.assertEqual(len(self.terminal_rows("events.jsonl")), 1)

    def test_duplicate_cached_frames_across_windows_are_not_fresh(self):
        self.recorder.callback(20_001, terminal_event(), now=0)
        self.recorder.poll(self.snapshot, 0)
        self.recorder.callback(20_001, terminal_event("GamePassTheEndConfirm"), now=1)
        self.recorder.poll(self.snapshot, 1)
        self.assertEqual(
            [x["distinct_cached_frame"] for x in self.terminal_rows()], [True, False]
        )
        import hashlib

        self.assertEqual(
            self.terminal_rows()[0]["source_callback_sha256"],
            hashlib.sha256(terminal_event().encode()).hexdigest(),
        )

    def test_missing_images_and_exceptions_are_bounded(self):
        self.recorder.callback(20_001, terminal_event(), now=0)
        for i in range(30):
            self.recorder.poll(lambda _name: None, i * 0.25)
        self.assertEqual(len(self.terminal_rows()), self.recorder.TERMINAL_BURST_LIMIT)
        self.recorder.callback(20_001, terminal_event(page="second"), now=8)

        def fail(_name):
            raise OSError("cached image unavailable")

        self.recorder.poll(fail, 8)
        self.assertIn("error", self.terminal_rows()[-1])
        self.assertIsNone(self.terminal_rows()[-1]["path"])

    def test_total_capture_and_metadata_limits_cannot_be_extended(self):
        for window in range(5):
            at = window * 4
            self.recorder.callback(20_001, terminal_event(page=str(window)), now=at)
            for i in range(self.recorder.TERMINAL_BURST_LIMIT):
                self.recorder.poll(self.snapshot, at + i * 0.25)
        self.assertEqual(self.calls, self.recorder.TERMINAL_CAPTURE_LIMIT)
        for i in range(100):
            self.recorder.callback(20_001, terminal_event(page=f"overflow{i}"), now=30)
        self.recorder.poll(self.snapshot, 30)
        self.assertEqual(
            len(self.terminal_rows("events.jsonl")), self.recorder.TERMINAL_EVENT_LIMIT
        )
        self.assertGreater(self.recorder._terminal_dropped, 0)

    def test_new_battle_and_other_task_cannot_inherit_terminal_window(self):
        self.recorder.callback(20_001, terminal_event(), now=0)
        self.recorder.callback(20_003, event(), now=1)
        self.recorder.poll(self.snapshot, 1)
        self.assertEqual(len(self.rows()), 1)
        self.assertEqual(self.terminal_rows(), [])
        self.recorder.callback(20_001, terminal_event(taskid=99), now=2)
        self.recorder.poll(self.snapshot, 2)
        self.assertEqual(self.terminal_rows(), [])

    def test_capture_crossing_new_battle_has_no_confirmed_terminal_attribution(self):
        self.recorder.callback(20_001, terminal_event(), now=0)

        def racing(name):
            self.recorder.callback(20_003, event(), now=0)
            return self.snapshot(name)

        self.recorder.poll(racing, 0)
        self.assertFalse(self.terminal_rows()[0]["terminal_attribution_confirmed"])
        self.assertFalse(self.terminal_rows()[0]["first_ending_proven"])

    def test_task_end_only_allows_one_existing_idle_candidate_and_close_stops_all(self):
        self.recorder.callback(20_001, terminal_event(), now=0)
        self.recorder.callback(3, "", now=0)
        self.recorder.poll(self.snapshot, 0)
        self.assertEqual(self.calls, 0)
        self.recorder.poll(self.snapshot, 0, final=True)
        self.recorder.poll(self.snapshot, 1, final=True)
        self.assertEqual(self.calls, 1)
        self.assertTrue(self.terminal_rows()[0]["capture_after_native_task_end"])
        self.recorder.close()
        self.recorder.callback(20_001, terminal_event(page="afterclose"), now=2)
        self.recorder.poll(self.snapshot, 2, final=True)
        self.assertEqual(self.calls, 1)

    def test_unrelated_or_malformed_task_names_cannot_create_candidates(self):
        cases = [
            "[]",
            "",
            "{}",
            terminal_event("GamePassExtra"),
            terminal_event(uuid=[]),
        ]
        bad = json.loads(terminal_event())
        bad["details"]["page_id"] = {"bad": True}
        cases.append(json.dumps(bad))
        wrong = json.loads(terminal_event())
        wrong["details"]["page_id"] = "Mizuki@Roguelike@GamePass#1"
        cases.append(json.dumps(wrong))
        for raw in cases:
            self.recorder.callback(20_001, raw, now=0)
        self.recorder.poll(self.snapshot, 0)
        self.assertEqual(self.calls, 0)
        self.assertEqual(self.terminal_rows("events.jsonl"), [])

    def test_evidence_write_failure_disables_only_observation(self):
        self.recorder.callback(20_001, terminal_event(), now=0)
        with mock.patch.object(
            Path, "open", side_effect=OSError("evidence volume unavailable")
        ):
            self.recorder.poll(lambda _name: None, 0)
        self.assertTrue(self.recorder._disabled)
        self.recorder.poll(self.snapshot, 1)
        self.assertEqual(self.calls, 0)

    def test_due_wait_is_positive_after_limit_and_event_is_consumed(self):
        self.recorder.callback(20_001, terminal_event(), now=0)
        for i in range(12):
            self.recorder.poll(lambda _name: None, i * 0.25)
        self.recorder._wake = mock.Mock()
        with mock.patch("blackflow_battle_observation.time.monotonic", return_value=3):
            self.recorder.wait_for_due(timeout=2)
        self.recorder._wake.wait.assert_called_once_with(timeout=2)
        self.recorder._wake.clear.assert_called_once()


class DriverWaitEvidenceTests(unittest.TestCase):
    def fake_driver(self, observer, root, running):
        class Fake:
            out_dir = root
            _observation = observer
            _done = threading.Event()
            stopped = False
            captures = 0

            def running(self):
                return running()

            def stop(self):
                self.stopped = True

            def snapshot(self, _name):
                self.captures += 1
                return None

        return Fake()

    def test_idle_flush_stop_request_timeout_and_disabled_observer(self):
        from blackflow_live_run import LiveDriver

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            recorder = BattleObservationRecorder(root, 1, 1200)
            recorder.callback(20_001, terminal_event())
            driver = self.fake_driver(recorder, root, lambda: False)
            self.assertTrue(LiveDriver.wait(driver, 1))
            self.assertEqual(driver.captures, 1)
            disabled = self.fake_driver(None, root, lambda: False)
            self.assertTrue(LiveDriver.wait(disabled, 1))
            self.assertEqual(disabled.captures, 0)
            (root / "stop.request").write_text("stop", encoding="utf-8")
            driver = self.fake_driver(recorder, root, lambda: True)
            self.assertFalse(LiveDriver.wait(driver, 1))
            self.assertTrue(driver.stopped)
            self.assertTrue(recorder._closed)
            (root / "stop.request").unlink()
            self.assertFalse(LiveDriver.wait(driver, 0))

    def test_short_candidate_window_is_polled_during_normal_wait(self):
        from blackflow_live_run import LiveDriver

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            recorder = BattleObservationRecorder(root, 1, 1200)
            checks = iter([True, False])
            driver = self.fake_driver(recorder, root, lambda: next(checks))
            recorder.callback(20_001, terminal_event())
            self.assertTrue(LiveDriver.wait(driver, 1))
            self.assertEqual(driver.captures, 1)
            self.assertFalse(recorder._wake.is_set())

    def test_close_clears_observer_without_capture_or_wait_extension(self):
        from blackflow_live_run import LiveDriver

        with tempfile.TemporaryDirectory() as temp:
            recorder = BattleObservationRecorder(temp, 1, 1200)
            recorder.callback(20_001, terminal_event())
            driver = self.fake_driver(recorder, Path(temp), lambda: False)
            driver._lib = mock.Mock()
            driver._handle = object()
            LiveDriver.close(driver)
            self.assertTrue(recorder._closed)
            self.assertEqual(driver.captures, 0)
            driver._lib.AsstDestroy.assert_called_once()


if __name__ == "__main__":
    unittest.main()
