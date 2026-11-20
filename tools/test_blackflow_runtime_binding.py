"""Regression checks for device routing, account isolation, and startup rejection."""

import contextlib
import io
import json
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import cv2
import numpy as np

import blackflow_auto_rounds as rounds
import blackflow_live_run as live
import blackflow_pick as pick
import blackflow_state_check as state


class RuntimeBindingTests(unittest.TestCase):
    def test_requested_live_stop_exits_wait_without_destroying_driver(self):
        driver = object.__new__(live.LiveDriver)
        driver.out_dir = self.root
        driver._done = threading.Event()
        driver.running = Mock(return_value=True)
        driver.stop = Mock(return_value=True)
        (self.root / "stop.request").touch()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(driver.wait(60))
        driver.stop.assert_called_once()
        self.assertFalse(driver._done.is_set())

    def test_squad_selection_is_not_overridden_by_fixed_candidates(self):
        tasks = json.loads(
            (live.REPO_ROOT / "resource/tasks/Roguelike/BlackFlow.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            tasks["BlackFlow@Roguelike@Squad-EnterPoint"]["next"],
            ["BlackFlow@Roguelike@SquadConfirm"],
        )
        self.assertEqual(
            tasks["BlackFlow@Roguelike@SquadConfirm"]["next"],
            ["BlackFlow@Roguelike@SquadConfirmLeave#next"],
        )
        self.assertIn(
            "BlackFlow@Roguelike@LastReward-EnterPoint",
            tasks["BlackFlow@Roguelike@SquadConfirmLeave"]["next"],
        )

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.accounts = patch.object(pick, "ACCOUNT_DIR", str(self.root))
        self.accounts.start()
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(self.accounts.stop)

    def roster(self, filename, uid, package="com.hypergryph.arknights"):
        (self.root / filename).write_text(
            json.dumps(
                {
                    "account": {"uid": uid},
                    "package": package,
                    "operators": {"test": {"level": 90}},
                }
            ),
            encoding="utf-8",
        )

    def test_identity_uses_content_even_with_legacy_filename(self):
        self.roster("bilibili_280875771.json", "280875771")
        self.assertEqual(
            pick.load_account("280875771", "Official")[0]["account"]["uid"], "280875771"
        )
        with self.assertRaises(SystemExit):
            pick.load_account("280875771", "Bilibili")

    def test_uid_must_match_exactly(self):
        self.roster("123.json", "123")
        with self.assertRaises(SystemExit):
            pick.load_account("12", "Official")

    def test_unbound_snapshot_is_not_an_account(self):
        self.roster("123.json", "")
        with self.assertRaises(SystemExit):
            pick.load_account("123", "Official")

    def test_multiple_accounts_require_selection(self):
        self.roster("1.json", "1")
        self.roster("2.json", "2")
        with self.assertRaises(SystemExit):
            pick.load_account(client="Official")

    def test_module_database_is_not_a_roster(self):
        (self.root / "modules_1.json").write_text(
            json.dumps({"account": {"uid": "1"}}), encoding="utf-8"
        )
        with self.assertRaises(SystemExit):
            pick.load_account("1", "Official")

    def test_round_precheck_and_runner_share_target(self):
        with (
            patch(
                "sys.argv",
                [
                    "rounds",
                    "--rounds",
                    "1",
                    "--address",
                    "device-1",
                    "--client",
                    "Official",
                ],
            ),
            patch.object(
                rounds,
                "check_state",
                return_value={"ok": True, "best": {"template": "lobby", "score": 1}},
            ) as precheck,
            patch.object(rounds.subprocess, "call", return_value=0) as run,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(rounds.main(), 0)
        precheck.assert_called_once_with(address="device-1")
        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--address") + 1], "device-1")
        self.assertEqual(command[command.index("--client") + 1], "Official")

    def test_unsupported_page_never_appends_or_starts(self):
        image = self.root / "blank.png"
        cv2.imencode(".png", np.zeros((720, 1280, 3), dtype=np.uint8))[1].tofile(image)
        driver = Mock()
        driver.snapshot.return_value = image
        driver.save.return_value = (self.root / "callbacks.jsonl", 0)
        with (
            patch(
                "sys.argv", ["live", "--out", str(self.root), "--address", "device-1"]
            ),
            patch.object(live, "LiveDriver", return_value=driver),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(live.main(), 4)
        driver.connect.assert_called_once_with("Official", "device-1")
        driver.append_task.assert_not_called()
        driver.start.assert_not_called()
        self.assertFalse(json.loads((self.root / "preflight.json").read_text())["ok"])

    def test_state_capture_routes_to_requested_device(self):
        with patch.object(
            state,
            "adb_screencap",
            return_value=np.zeros((720, 1280, 3), dtype=np.uint8),
        ) as capture:
            result = state.check(address="device-1")
        capture.assert_called_once_with("device-1")
        self.assertEqual(result["address"], "device-1")
        self.assertFalse(result["ok"])

    def test_verified_cache_choice_reaches_native_task(self):
        image = self.root / "screen.png"
        cv2.imencode(".png", np.zeros((720, 1280, 3), dtype=np.uint8))[1].tofile(image)
        for scan in (True, False):
            with self.subTest(scan=scan):
                driver = Mock()
                driver.snapshot.return_value = image
                driver.save.return_value = (self.root / "callbacks.jsonl", 0)
                args = [
                    "live",
                    "--out",
                    str(self.root),
                    "--account-cache-file",
                    "same-uid.json",
                ]
                if not scan:
                    args.append("--no-scan-account-modules")
                with (
                    patch("sys.argv", args),
                    patch.object(live, "LiveDriver", return_value=driver),
                    patch.object(state, "check", return_value={"ok": True}),
                    contextlib.redirect_stdout(io.StringIO()),
                ):
                    self.assertEqual(live.main(), 0)
                task, params = driver.append_task.call_args.args
                self.assertEqual(task, "Roguelike")
                self.assertEqual(params["scan_account_modules"], scan)
                self.assertEqual(params["account_cache_file"], "same-uid.json")


if __name__ == "__main__":
    unittest.main()
