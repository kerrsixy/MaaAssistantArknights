"""Check native scheduling boundaries; clock behavior is tested in C++."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
TASK = ROOT / "src/MaaCore/Task/Interface/StrongholdProtocolTask.cpp"


class PreparationDeadlineWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = TASK.read_text(encoding="utf-8").split(
            "bool asst::StrongholdProtocolTask::run_session(", 1
        )[1]

    def test_only_fresh_observer_calls_supply_frame_timestamp(self):
        boundary = self.session.split("json::value state_storage;", 1)[1].split(
            "const auto& state = state_storage;", 1
        )[0]
        uncached = boundary.index("if (!used_cached_deployment_observation)")
        timestamp = boundary.index(
            "observation_started = spa::PreparationDeadline::Clock::now();"
        )
        observe = boundary.index("if (!observer.run())")
        consume = boundary.index("m_joint_preparation_deadline.observe_frame(")
        self.assertLess(uncached, timestamp)
        self.assertLess(timestamp, observe)
        self.assertLess(observe, consume)
        self.assertEqual(boundary.count("observation_started ="), 1)
        self.assertRegex(boundary, r"read_now\s*=\s*observation_started\.has_value\(\)")
        self.assertIn(
            'state_storage["preparation_seconds_remaining_known"] = remaining.has_value();',
            boundary,
        )

    def test_expired_branch_reconciles_existing_pending_without_new_actions(self):
        boundary = self.session.split(
            'state.get("preparation_seconds_remaining", 0) <= 8) {', 1
        )[1].split("if (m_bench_recount_pending", 1)[0]
        self.assertIn(
            "if (m_pending_spatial_orientation) {\n"
            "                static_cast<void>(handle_spatial_reorientation(state));\n"
            "            }",
            boundary,
        )
        self.assertLess(
            boundary.index("handle_spatial_reorientation(state)"),
            boundary.index('basic_info_with_what("SPAJointPreparationDeadline")'),
        )
        self.assertIn('"new_transaction_issued", false', boundary)
        self.assertIn("if (!sleep(400))", boundary)
        self.assertTrue(boundary.rstrip().endswith("continue;\n        }"))


if __name__ == "__main__":
    unittest.main()
