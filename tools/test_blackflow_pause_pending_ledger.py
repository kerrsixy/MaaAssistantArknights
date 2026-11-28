"""No DLL, capture, task or connection: actual Python summary/acceptance calls."""

import copy
import json
import sys
import threading
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import blackflow_live_run as live
import blackflow_auto_rounds as rounds
from test_blackflow_terminal_batch import terminal_outcome


def evidence(identity="battle1:pause1"):
    return {
        "pause_observation_id": identity,
        "stage": "生存习性",
        "captured_ms": 2000,
        "received_ms": 2100,
        "banner_captured_ms": 1000,
        "classification_deadline_ms": 32000,
        "current_kills": 24,
        "current_total": 24,
        "banner_frame": "banner.png",
        "image_file": "pause.png",
        "classification_pending": True,
        "current_hud_known": True,
        "selected_ui_owned": False,
    }


def record(
    tag="BlackFlowBattlePausePending", details=None, msg=live.MSG_SUBTASK_EXTRA_INFO
):
    return {
        "msg": msg,
        "details": json.dumps(
            {"what": tag, "details": evidence() if details is None else details}
        ),
    }


def final(identity="battle1:pause1", paused=False):
    details = evidence(identity)
    details.update(
        classification_pending=False,
        classified_as_end_transition=not paused,
        classification_recorded_ms=2400,
        automation_resumed_pause=False,
        classification_reason="successor_error_or_stop"
        if paused
        else "local_banner_then_nonbattle_then_positive_result_ack",
    )
    if not paused:
        details.update(
            positive_successor_task="BlackFlow@Roguelike@MissionCompletedFlag",
            successor_received_ms=2300,
        )
    return record(
        "BlackFlowBattlePaused" if paused else "BlackFlowBattleEndTransition", details
    )


def summarize(records):
    driver = object.__new__(live.LiveDriver)
    driver._lock, driver._records = threading.Lock(), list(records)
    return driver.result_summary()


def accept_summary(summary):
    outcome = terminal_outcome()
    outcome.update(summary)
    # Use the independently established settlement/identity fixture rather than
    # inventing those unrelated messages in each classification case.
    for key in ("settlements", "account_bindings", "fresh_exploration_starts"):
        outcome[key] = terminal_outcome()[key]
    return rounds.verified_round(outcome, "Official", "90000002", 9)


class PendingLedgerTests(unittest.TestCase):
    def assertRejected(self, records):
        summary = summarize(records)
        with self.assertRaises(ValueError):
            accept_summary(summary)
        return summary

    def test_legacy_no_pending_remains_accepted(self):
        rounds.verified_round(terminal_outcome(), "Official", "90000002", 9)

    def test_versioned_empty_is_accepted(self):
        accept_summary(summarize([]))

    def test_pending_without_ack_is_rejected_before_cleanup(self):
        summary = self.assertRejected([record()])
        self.assertEqual(summary["pause_callbacks"], 0)
        self.assertEqual(summary["pause_unresolved_observations"], 1)

    def test_terminal_callbacks_before_cleanup_do_not_clear_pending(self):
        records = [
            record(),
            {
                "msg": live.MSG_TASK_CHAIN_COMPLETED,
                "details": '{"taskchain":"Roguelike"}',
            },
            {"msg": live.MSG_ALL_TASKS_COMPLETED, "details": '{"finished_tasks":[1]}'},
        ]
        summary = self.assertRejected(records)
        self.assertEqual(summary["pause_unresolved_observations"], 1)
        self.assertEqual(summary["pause_callbacks"], 0)

    def test_same_id_final_resolves(self):
        summary = summarize([record(), final()])
        self.assertEqual(summary["pause_unresolved_observations"], 0)
        accept_summary(summary)

    def test_foreign_id_cannot_resolve(self):
        self.assertRejected([record(), final("battle2:pause1")])

    def test_duplicate_pending_cannot_be_washed_by_ack(self):
        self.assertRejected([record(), record(), final()])

    def test_duplicate_final_is_conflict(self):
        self.assertRejected([record(), final(), final()])

    def test_final_before_pending_is_conflict(self):
        self.assertRejected([final(), record()])

    def test_same_id_different_scene_is_rejected(self):
        changed = json.loads(final()["details"])
        changed["details"]["stage"] = "另一场"
        self.assertRejected(
            [
                record(),
                {"msg": live.MSG_SUBTASK_EXTRA_INFO, "details": json.dumps(changed)},
            ]
        )

    def test_wrong_message_cannot_ack(self):
        ack = final()
        ack["msg"] = live.MSG_SUBTASK_COMPLETED
        self.assertRejected([record(), ack])

    def test_minimal_fake_ack_cannot_resolve(self):
        self.assertRejected(
            [
                record(),
                record(
                    "BlackFlowBattleEndTransition",
                    {"pause_observation_id": "battle1:pause1"},
                ),
            ]
        )

    def test_malformed_json_cannot_be_ignored(self):
        self.assertRejected(
            [
                record(),
                {
                    "msg": live.MSG_SUBTASK_EXTRA_INFO,
                    "details": '{"what":"BlackFlowBattleEndTransition"',
                },
            ]
        )

    def test_bool_is_not_timestamp(self):
        pending = evidence()
        pending["captured_ms"] = True
        self.assertRejected([record(details=pending), final()])

    def test_missing_id_stays_unresolved(self):
        pending = evidence()
        del pending["pause_observation_id"]
        self.assertRejected([record(details=pending), final()])

    def test_expired_ack_is_rejected(self):
        ack = json.loads(final()["details"])
        ack["details"].update(
            successor_received_ms=32001, classification_recorded_ms=32002
        )
        self.assertRejected(
            [record(), {"msg": live.MSG_SUBTASK_EXTRA_INFO, "details": json.dumps(ack)}]
        )

    def test_pending_then_paused_preserves_old_veto(self):
        summary = self.assertRejected([record(), final(paused=True)])
        self.assertEqual(summary["pause_unresolved_observations"], 0)
        self.assertEqual(summary["pause_callbacks"], 1)

    def test_classified_pause_cannot_be_hidden_by_counter_edit(self):
        summary = summarize([record(), final(paused=True)])
        summary["pause_callbacks"] = 0
        with self.assertRaises(ValueError):
            accept_summary(summary)

    def test_malformed_final_flags_do_not_clear_pending(self):
        for key, value in [
            ("classified_as_end_transition", False),
            ("automation_resumed_pause", True),
        ]:
            ack = json.loads(final()["details"])
            ack["details"][key] = value
            with self.subTest(key=key):
                self.assertRejected(
                    [
                        record(),
                        {
                            "msg": live.MSG_SUBTASK_EXTRA_INFO,
                            "details": json.dumps(ack),
                        },
                    ]
                )

    def test_immediate_classified_pause_preserves_old_veto(self):
        self.assertRejected([final(paused=True)])

    def test_old_manual_pause_preserves_old_veto(self):
        self.assertRejected(
            [
                record(
                    "BlackFlowBattlePaused", {"stage": "生存习性", "origin": "unknown"}
                )
            ]
        )

    def test_late_cleanup_does_not_retroactively_change_snapshot(self):
        records = [record()]
        before = summarize(records)
        records.append(final(paused=True))
        after = summarize(records)
        self.assertEqual(before["pause_unresolved_observations"], 1)
        self.assertEqual(after["pause_callbacks"], 1)
        with self.assertRaises(ValueError):
            accept_summary(before)

    def test_unresolved_counter_cannot_hide_unresolved_row(self):
        summary = summarize([record()])
        summary["pause_unresolved_observations"] = 0
        with self.assertRaises(ValueError):
            accept_summary(summary)

    def test_typed_version_fields_and_unknown_version(self):
        baseline = summarize([])
        for key, value in [
            ("pause_classification_version", True),
            ("pause_classification_version", 2),
            ("pause_unresolved_observations", False),
            ("pause_pending_callbacks", "0"),
            ("pause_classification_ledger", {}),
        ]:
            changed = copy.deepcopy(baseline)
            changed[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                accept_summary(changed)

    def test_partial_version_is_rejected(self):
        outcome = terminal_outcome()
        outcome["pause_unresolved_observations"] = 0
        with self.assertRaises(ValueError):
            rounds.verified_round(outcome, "Official")


if __name__ == "__main__":
    unittest.main()
