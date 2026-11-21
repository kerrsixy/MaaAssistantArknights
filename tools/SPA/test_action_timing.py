import json
import unittest

from audit_action_timing import audit_lines


def event(second, outcome, process=1):
    body = json.dumps({"class": "asst::spa::ShopTaskPlugin", "taskid": 1})
    return (
        f"[2026-11-18 08:00:{second:02d}.000][INF][Px{process}][Tx2] "
        f"Assistant::append_callback | {outcome} {body}"
    )


class TimingAuditTests(unittest.TestCase):
    def test_success_and_failure_are_separate(self):
        result = audit_lines(
            [
                event(1, "SubTaskStart"),
                event(3, "SubTaskCompleted"),
                event(5, "SubTaskStart"),
                event(9, "SubTaskError"),
            ]
        )
        stats = result["plugins"]["asst::spa::ShopTaskPlugin"]
        self.assertEqual((stats["completed"], stats["failed"]), (1, 1))
        self.assertEqual(stats["mean_seconds"], 3)
        self.assertFalse(result["game_success_verified"])

    def test_processes_cannot_complete_each_others_calls(self):
        result = audit_lines(
            [event(1, "SubTaskStart"), event(3, "SubTaskCompleted", 2)]
        )
        self.assertEqual(result["unfinished_calls"], 1)
        self.assertEqual(result["unmatched_end_events"], 1)
        self.assertEqual(result["plugins"], {})

    def test_unfinished_call_has_no_fabricated_duration(self):
        result = audit_lines([event(1, "SubTaskStart")])
        self.assertEqual(result["unfinished_calls"], 1)
        self.assertEqual(result["plugins"], {})


if __name__ == "__main__":
    unittest.main()
