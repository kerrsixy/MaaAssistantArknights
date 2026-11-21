import copy
import json
import unittest

from spa_joint_acceptance import (
    JOINT_DIFFICULTIES,
    audit_joint_run,
    combat_hud_verified,
)


def event(what, details):
    return {
        "message": 20003,
        "details": {"what": what, "details": details, "taskid": 1},
    }


def full_run():
    result = {
        "page": "result",
        "result_joint": True,
        "result_text_verified": True,
        "result_difficulty_name": "险境模拟",
        "result_completed": True,
        "result_interrupted": False,
        "passed_rounds_known": True,
        "passed_rounds": 14,
        "return_home_verified": True,
        "battle_seen": True,
    }
    return [
        event("SPAObservation", {"page": "lobby"}),
        event("SPADifficultySelected", {"mode": "joint", "difficulty": 2}),
        event("SPASimulationStartIssued", {"mode": "joint", "difficulty": 2}),
        event(
            "SPAObservation", {"page": "preparation", "round_known": True, "round": 1}
        ),
        event(
            "SPAObservation",
            {
                "page": "battle",
                "texts": [
                    {"text": "险境模拟", "score": 0.999, "x": 138, "y": 17},
                    {"text": "2/2", "score": 0.999, "x": 645, "y": 18},
                ],
            },
        ),
        event(
            "SPAObservation",
            {
                "page": "result_summary",
                "texts": [
                    {"text": t, "score": 0.999, "x": x, "y": y}
                    for t, x, y in [
                        ("险境模拟", 200, 150),
                        ("模拟完成", 160, 191),
                        ("通过回合", 49, 270),
                        ("14", 42, 299),
                        ("Dr.Test#1234", 184, 467),
                        ("下一步", 1040, 639),
                    ]
                ],
            },
        ),
        event("SPAObservation", copy.deepcopy(result)),
        event("SPAObservation", {"page": "lobby"}),
        event("SPALifecycleCompleted", result),
        {"message": 10002, "details": {"taskchain": "StrongholdProtocol", "taskid": 1}},
    ]


class JointAcceptanceTests(unittest.TestCase):
    policy = {"mode": "joint", "difficulty": 2, "run_id": "one-run"}

    def test_full_run_is_only_a_candidate_not_repeated_stability(self):
        audit = audit_joint_run(full_run(), self.policy)
        self.assertTrue(audit["same_host_full_run_candidate"])
        self.assertFalse(audit["stable_acceptance_verified"])

    def test_all_joint_tiers_require_matching_selection_hud_and_result(self):
        for level, name in JOINT_DIFFICULTIES.items():
            events = json.loads(
                json.dumps(full_run(), ensure_ascii=False).replace("险境模拟", name)
            )
            for index in (1, 2):
                events[index]["details"]["details"]["difficulty"] = level
            policy = {**self.policy, "difficulty": level}
            self.assertTrue(
                audit_joint_run(events, policy)["same_host_full_run_candidate"]
            )
            events[5]["details"]["details"]["texts"][0]["text"] = "wrong mode"
            self.assertFalse(
                audit_joint_run(events, policy)["same_host_full_run_candidate"]
            )

    def test_resuming_at_settlement_is_not_an_autonomous_run(self):
        self.assertFalse(
            audit_joint_run(full_run()[5:], self.policy)["same_host_full_run_candidate"]
        )

    def test_bonus_round_completion_is_not_hidden_boss_identity_proof(self):
        events = full_run()
        events[5]["details"]["details"]["texts"][3]["text"] = "15"
        for index in (6, 8):
            events[index]["details"]["details"]["passed_rounds"] = 15
        report = audit_joint_run(events, self.policy)
        self.assertTrue(report["same_host_full_run_candidate"])
        self.assertEqual(report["personal_passed_rounds"], 15)
        self.assertFalse(report["hidden_boss_victory_verified"])
        events[8]["details"]["details"]["passed_rounds"] = 14
        self.assertFalse(
            audit_joint_run(events, self.policy)["same_host_full_run_candidate"]
        )

    def test_personal_rounds_are_not_alliance_aggregate(self):
        events = full_run()
        for i in (6, 8):
            events[i]["details"]["details"]["passed_rounds"] = 13
        report = audit_joint_run(events, self.policy)
        self.assertTrue(report["same_host_full_run_candidate"])
        self.assertEqual(report["personal_passed_rounds"], 14)
        self.assertEqual(report["alliance_passed_rounds"], 13)
        for value in ("6", "13", "16"):
            events[5]["details"]["details"]["texts"][3]["text"] = value
            self.assertFalse(
                audit_joint_run(events, self.policy)["same_host_full_run_candidate"]
            )

    def test_summary_requires_identity_anchors_and_confidence(self):
        for index in range(6):
            events = full_run()
            events[5]["details"]["details"]["texts"][index]["score"] = 0.4
            self.assertFalse(
                audit_joint_run(events, self.policy)["same_host_full_run_candidate"]
            )
        events = full_run()
        events[5]["details"]["details"]["texts"][1]["text"] = "模拟中断"
        self.assertFalse(
            audit_joint_run(events, self.policy)["same_host_full_run_candidate"]
        )

    def test_misclassified_summary_does_not_prove_combat(self):
        summary = {
            "page": "battle",
            "texts": [
                {"text": "险境模拟", "score": 0.999, "x": 198, "y": 150},
                {"text": "通过回合", "score": 0.999, "x": 48, "y": 270},
                {"text": "55", "score": 0.999, "x": 1166, "y": 52},
            ],
        }
        self.assertFalse(combat_hud_verified(summary))
        events = full_run()
        events[4] = event("SPAObservation", summary)
        self.assertFalse(
            audit_joint_run(events, self.policy)["same_host_full_run_candidate"]
        )

    def test_wrong_mode_partial_rounds_and_interruption_rejected(self):
        for key, value in [
            ("result_difficulty_name", "标准模拟"),
            ("result_interrupted", True),
            ("contradictory", True),
        ]:
            with self.subTest(key=key):
                events = full_run()
                for index in (6, 8):
                    events[index]["details"]["details"][key] = value
                self.assertFalse(
                    audit_joint_run(events, self.policy)["same_host_full_run_candidate"]
                )

    def test_task_completion_and_order_required(self):
        events = full_run()
        self.assertFalse(
            audit_joint_run(events[:-1], self.policy)["same_host_full_run_candidate"]
        )
        events[2], events[3] = events[3], events[2]
        self.assertFalse(
            audit_joint_run(events, self.policy)["same_host_full_run_candidate"]
        )

    def test_recovery_request_and_abandon_do_not_pass(self):
        for extra in [
            event("SPALifecycleRecoveryRequired", {"reason": "input reconciliation"}),
            {
                "message": 20001,
                "details": {
                    "details": {"task": "ForceExitConfirm", "action": "ClickSelf"}
                },
            },
        ]:
            self.assertFalse(
                audit_joint_run(full_run() + [extra], self.policy)[
                    "same_host_full_run_candidate"
                ]
            )

    def test_loading_and_unreadable_counter_are_not_combat(self):
        for value in ["55", "14/2", "0/0"]:
            observation = full_run()[4]["details"]["details"]
            observation["texts"][1]["text"] = value
            self.assertFalse(combat_hud_verified(observation))
        observation = full_run()[4]["details"]["details"]
        observation["texts"][1]["score"] = float("nan")
        self.assertFalse(combat_hud_verified(observation))


if __name__ == "__main__":
    unittest.main()
