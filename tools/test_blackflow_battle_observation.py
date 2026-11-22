import json
import tempfile
import unittest
from pathlib import Path

from blackflow_battle_observation import BattleObservationRecorder


def event(what="StageInfo", name="遗忘时间"):
    return json.dumps(
        {"subtask": "RoguelikeBattleTask", "what": what, "details": {"name": name}}
    )


class ObservationTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
