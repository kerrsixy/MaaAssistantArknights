"""Bounded, read-only evidence collection; never classify or control gameplay."""

import hashlib
import json
import math
import threading
import time
from pathlib import Path


class BattleObservationRecorder:
    def __init__(self, out_dir, interval, limit):
        if not math.isfinite(interval) or interval <= 0:
            raise ValueError("Observation interval must be positive and finite")
        if isinstance(limit, bool) or not isinstance(limit, int) or limit <= 0:
            raise ValueError("Observation limit must be a positive integer")
        self.directory = Path(out_dir) / "battle_observations"
        self.directory.mkdir(parents=True, exist_ok=False)
        self.interval = interval
        self.limit = limit
        self._lock = threading.Lock()
        self._generation = 0
        self._active = None
        self._disabled = False

    def callback(self, msg, text):
        """Metadata only: no image calls or filesystem work on the native thread."""
        if msg in (0, 3, 10_000, 10_002):
            with self._lock:
                self._active = None
            return
        try:
            data = json.loads(text)
        except (ValueError, TypeError):
            return
        if not isinstance(data, dict):
            return
        battle = data.get("subtask") == "RoguelikeBattleTask"
        with self._lock:
            if battle and msg in (20_001, 20_002, 20_000):
                self._active = None
            if not battle or msg != 20_003 or data.get("what") != "StageInfo":
                if battle and data.get("what") == "RoguelikeCombatEnd":
                    self._active = None
                return
            details = data.get("details", {})
            name = details.get("name") if isinstance(details, dict) else None
            if not isinstance(name, str) or not name:
                return
            if self._active is not None and self._active["stage"] == name:
                return
            self._generation += 1
            self._active = {
                "battle": self._generation,
                "stage": name,
                "source_callback": data,
                "attempt": 0,
                "next_due": 0.0,
                "hashes": set(),
            }

    def poll(self, snapshot, now=None):
        """Called only by the wait thread, outside native callbacks and locks."""
        now = time.monotonic() if now is None else now
        with self._lock:
            state = self._active
            if (
                self._disabled
                or state is None
                or state["attempt"] >= self.limit
                or now < state["next_due"]
            ):
                return
            state["attempt"] += 1
            state["next_due"] = now + self.interval
            attempt = state["attempt"]
        relative = (
            Path("battle_observations")
            / f"battle_{state['battle']:04d}_frame_{attempt:03d}.png"
        )
        record = {
            "battle": state["battle"],
            "stage": state["stage"],
            "attempt": attempt,
            "capture_monotonic": now,
            "captured_at": time.time(),
            "source_callback": state["source_callback"],
            "normal_battle_confirmed": False,
            "capture_source": "AsstGetImage cached frame; capture time is not engine frame time",
            "classification": "unknown",
        }
        try:
            path = snapshot(str(relative))
            pixels = Path(path).read_bytes() if path else None
            digest = hashlib.sha256(pixels).hexdigest() if pixels else None
            with self._lock:
                record["battle_attribution_confirmed"] = self._active is state
                record["distinct_cached_frame"] = (
                    digest is not None and digest not in state["hashes"]
                )
                if digest:
                    state["hashes"].add(digest)
            record.update(path=str(relative) if pixels else None, sha256=digest)
        except Exception as error:
            record.update(
                error=f"{type(error).__name__}: {error}",
                battle_attribution_confirmed=False,
            )
        try:
            with (self.directory / "observations.jsonl").open(
                "a", encoding="utf-8"
            ) as stream:
                stream.write(json.dumps(record, ensure_ascii=False) + "\n")
        except OSError as error:
            # Evidence failures must not stop, restart or issue gameplay input.
            self._disabled = True
            print(
                f"Battle observation disabled after evidence write failure: {error}",
                flush=True,
            )
