"""Bounded, read-only evidence collection; never classify or control gameplay."""

import hashlib
import json
import math
import threading
import time
from pathlib import Path


class BattleObservationRecorder:
    ENDING_TASKS = frozenset(
        {
            "GamePass",
            "GamePassSkip1",
            "GamePassSkip1Confirm",
            "GamePassSkip2",
            "GamePassConfirm",
            "GamePassTheEndConfirm",
        }
    )
    SETTLEMENT_TASKS = frozenset(
        {
            "SettlementStatistics-Enter",
            "SettlementStatistics-Ready",
            "SettlementStatistics-Next",
            "SettlementStatistics-Splash",
            "SettlementStatistics-LoadingText",
            "SettlementStatistics-LoadingIcon",
        }
    )
    TERMINAL_INTERVAL = 0.25
    TERMINAL_WINDOW = 8.0
    TERMINAL_BURST_LIMIT = 12
    TERMINAL_CAPTURE_LIMIT = 48
    TERMINAL_EVENT_LIMIT = 32

    def __init__(self, out_dir, interval, limit):
        if not math.isfinite(interval) or interval <= 0:
            raise ValueError("Observation interval must be positive and finite")
        if isinstance(limit, bool) or not isinstance(limit, int) or limit <= 0:
            raise ValueError("Observation limit must be a positive integer")
        self.directory = Path(out_dir) / "battle_observations"
        self.directory.mkdir(parents=True, exist_ok=False)
        self.terminal_directory = Path(out_dir) / "terminal_observations"
        self.terminal_directory.mkdir(parents=True, exist_ok=False)
        self.interval = interval
        self.limit = limit
        self._lock = threading.Lock()
        self._wake = threading.Event()
        self._generation = 0
        self._active = None
        self._disabled = False
        self._closed = False
        self._task_ended = False
        self._task_key = None
        self._last_stage = None
        self._terminal = None
        self._terminal_generation = 0
        self._terminal_attempts = 0
        self._terminal_hashes = set()
        self._terminal_events = []
        self._terminal_seen = set()
        self._terminal_dropped = 0

    @classmethod
    def _terminal_kind(cls, msg, data):
        if data.get("taskchain") != "Roguelike":
            return None
        details = data.get("details")
        if not isinstance(details, dict):
            return None
        if (
            msg == 20_003
            and data.get("class") == "asst::RoguelikeSettlementTaskPlugin"
            and data.get("subtask") == "RoguelikeSettlementTask"
            and data.get("what") == "RoguelikeSettlement"
            and isinstance(details.get("game_pass"), bool)
        ):
            return "native_settlement_candidate", "RoguelikeSettlement"
        if (
            msg not in (20_001, 20_002)
            or data.get("class") != "asst::ProcessTask"
            or data.get("subtask") != "ProcessTask"
        ):
            return None
        task = details.get("task")
        if not isinstance(task, str):
            return None
        page = details.get("page_id", "")
        qualified = task.startswith("BlackFlow@Roguelike@") or (
            isinstance(page, str) and page.startswith("BlackFlow@Roguelike@")
        )
        if not qualified:
            return None
        task = task.rsplit("@", 1)[-1]
        if task in cls.ENDING_TASKS:
            return "ending_ui_candidate", task
        if task in cls.SETTLEMENT_TASKS:
            return "settlement_ui_candidate", task
        return None

    def callback(self, msg, text, now=None):
        """Metadata and wake notification only; no image calls or filesystem work."""
        now = time.monotonic() if now is None else now
        if msg in (0, 3, 10_000, 10_002):
            with self._lock:
                self._active = None
                self._task_ended = True
                self._wake.set()
            return
        try:
            data = json.loads(text)
        except (ValueError, TypeError):
            return
        if not isinstance(data, dict):
            return
        battle = data.get("subtask") == "RoguelikeBattleTask"
        terminal = self._terminal_kind(msg, data)
        with self._lock:
            if self._closed or self._disabled:
                return
            if battle and msg in (20_001, 20_002, 20_000):
                self._active = None
            if battle and msg == 20_003 and data.get("what") == "StageInfo":
                details = data.get("details", {})
                name = details.get("name") if isinstance(details, dict) else None
                if not isinstance(name, str) or not name:
                    return
                if self._active is not None and self._active["stage"] == name:
                    return
                self._generation += 1
                self._last_stage = name
                self._task_key = (data.get("uuid"), data.get("taskid"))
                self._task_ended = False
                self._terminal = None
                self._active = {
                    "battle": self._generation,
                    "stage": name,
                    "source_callback": data,
                    "attempt": 0,
                    "next_due": 0.0,
                    "hashes": set(),
                }
                self._wake.set()
                return
            if battle and data.get("what") == "RoguelikeCombatEnd":
                self._active = None
                return
            if terminal is None or self._task_ended:
                return
            if (
                not isinstance(text, str)
                or not isinstance(data.get("uuid"), str)
                or not data["uuid"]
                or isinstance(data.get("taskid"), bool)
                or not isinstance(data.get("taskid"), int)
            ):
                return
            task_key = (data["uuid"], data["taskid"])
            if self._task_key is not None and task_key != self._task_key:
                return
            kind, task = terminal
            details = data["details"]
            page = details.get("page_id")
            if page is not None and not isinstance(page, str):
                return
            key = (task_key, kind, task, page)
            if key in self._terminal_seen:
                return
            if len(self._terminal_seen) >= self.TERMINAL_EVENT_LIMIT:
                self._terminal_dropped += 1
                return
            self._task_key = task_key
            self._terminal_seen.add(key)
            self._terminal_generation += 1
            state = {
                "terminal": self._terminal_generation,
                "kind": kind,
                "task": task,
                "source_msg": msg,
                "source_callback": data,
                "source_text": text,
                "source_received_monotonic": now,
                "battle_generation": self._generation,
                "last_stage": self._last_stage,
                "attempt": 0,
                "next_due": 0.0,
                "deadline": now + self.TERMINAL_WINDOW,
            }
            self._terminal_events.append(state.copy())
            self._terminal = state
            self._active = None
            self._wake.set()

    def _write_record(self, directory, filename, record):
        try:
            with (directory / filename).open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(record, ensure_ascii=False) + "\n")
        except OSError as error:
            self._disabled = True
            print(
                f"Battle observation disabled after evidence write failure: {error}",
                flush=True,
            )

    @staticmethod
    def _source_metadata(state):
        return {
            "source_msg": state["source_msg"],
            "source_callback": state["source_callback"],
            "source_callback_sha256": hashlib.sha256(
                state["source_text"].encode("utf-8")
            ).hexdigest(),
            "source_received_monotonic": state["source_received_monotonic"],
            "classification": "unverified_terminal_candidate",
            "first_ending_proven": False,
        }

    def poll(self, snapshot, now=None, *, final=False):
        """Wait thread only. A final idle poll drains metadata and at most one image."""
        now = time.monotonic() if now is None else now
        with self._lock:
            if self._disabled or self._closed:
                return
            events, self._terminal_events = self._terminal_events, []
            dropped_events = self._terminal_dropped
            terminal = self._terminal
            if terminal is not None and now > terminal["deadline"]:
                self._terminal = terminal = None
            state = terminal if terminal is not None else self._active
            terminal_due = terminal is not None
            eligible = (
                state is not None
                and (not self._task_ended or (final and terminal_due))
                and (not final or terminal_due)
                and now >= state["next_due"]
                and state["attempt"]
                < (self.TERMINAL_BURST_LIMIT if terminal_due else self.limit)
                and (
                    not terminal_due
                    or self._terminal_attempts < self.TERMINAL_CAPTURE_LIMIT
                )
            )
            if eligible:
                state["attempt"] += 1
                state["next_due"] = now + (
                    self.TERMINAL_INTERVAL if terminal_due else self.interval
                )
                if terminal_due:
                    self._terminal_attempts += 1
                attempt = state["attempt"]
            else:
                state = None
        for event in events:
            self._write_record(
                self.terminal_directory,
                "events.jsonl",
                {
                    "terminal": event["terminal"],
                    "kind": event["kind"],
                    "task": event["task"],
                    "last_battle": event["battle_generation"],
                    "last_stage": event["last_stage"],
                    "terminal_events_dropped": dropped_events,
                    **self._source_metadata(event),
                },
            )
        if self._disabled or state is None:
            return
        if terminal_due:
            relative = (
                Path("terminal_observations")
                / f"terminal_{state['terminal']:04d}_frame_{attempt:03d}.png"
            )
            record = {
                "terminal": state["terminal"],
                "kind": state["kind"],
                "task": state["task"],
                "last_battle": state["battle_generation"],
                "last_stage": state["last_stage"],
                "last_battle_is_final_confirmed": False,
                "terminal_events_dropped": dropped_events,
                **self._source_metadata(state),
            }
            directory = self.terminal_directory
        else:
            relative = (
                Path("battle_observations")
                / f"battle_{state['battle']:04d}_frame_{attempt:03d}.png"
            )
            record = {
                "battle": state["battle"],
                "stage": state["stage"],
                "source_callback": state["source_callback"],
                "normal_battle_confirmed": False,
                "classification": "unknown",
            }
            directory = self.directory
        record.update(
            attempt=attempt,
            capture_monotonic=now,
            captured_at=time.time(),
            capture_source="AsstGetImage cached frame; capture time is not engine frame time",
            independent_fresh_capture_confirmed=False,
            final_idle_poll=final,
            path=None,
            sha256=None,
        )
        try:
            path = snapshot(str(relative))
            pixels = Path(path).read_bytes() if path else None
            digest = hashlib.sha256(pixels).hexdigest() if pixels else None
            with self._lock:
                if terminal_due:
                    record["terminal_attribution_confirmed"] = (
                        self._terminal is state
                        and self._generation == state["battle_generation"]
                        and not self._closed
                    )
                    hashes = self._terminal_hashes
                    record["capture_after_native_task_end"] = self._task_ended
                else:
                    record["battle_attribution_confirmed"] = self._active is state
                    hashes = state["hashes"]
                record["distinct_cached_frame"] = (
                    digest is not None and digest not in hashes
                )
                if digest:
                    hashes.add(digest)
            record.update(path=str(relative) if pixels else None, sha256=digest)
        except Exception as error:
            record.update(error=f"{type(error).__name__}: {error}")
            record[
                "terminal_attribution_confirmed"
                if terminal_due
                else "battle_attribution_confirmed"
            ] = False
        self._write_record(directory, "observations.jsonl", record)
        if final:
            with self._lock:
                self._terminal = None

    def wait_for_due(self, timeout=2.0):
        """Wake for metadata or the next bounded due time; never busy-spin."""
        now = time.monotonic()
        with self._lock:
            due = []
            if not self._disabled and not self._closed and not self._task_ended:
                if self._active is not None and self._active["attempt"] < self.limit:
                    due.append(self._active["next_due"])
                terminal = self._terminal
                if (
                    terminal is not None
                    and terminal["attempt"] < self.TERMINAL_BURST_LIMIT
                    and self._terminal_attempts < self.TERMINAL_CAPTURE_LIMIT
                    and now <= terminal["deadline"]
                ):
                    due.append(terminal["next_due"])
            delay = min(timeout, max(0.0, min(due) - now)) if due else timeout
        self._wake.wait(timeout=max(0.0, delay))
        self._wake.clear()

    def close(self):
        """Metadata cleanup only. Do not extend driver shutdown for evidence."""
        with self._lock:
            self._closed = True
            self._active = None
            self._terminal = None
            self._terminal_events.clear()
            self._wake.set()
