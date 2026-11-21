"""Measure completed native SPA plugin calls without treating open calls as success."""

import argparse
from collections import defaultdict
from datetime import datetime
import json
from pathlib import Path
import re


CALLBACK = re.compile(
    r"^\[([^]]+)\].*?\[Px(\d+)\]\[Tx(\d+)\].*?"
    r"Assistant::append_callback \| (SubTaskStart|SubTaskCompleted|SubTaskError) (\{.*\})$"
)


def audit_lines(lines):
    active = defaultdict(list)
    samples = defaultdict(list)
    unmatched = 0
    for line in lines:
        match = CALLBACK.search(line)
        if not match:
            continue
        try:
            when = datetime.fromisoformat(match[1])
            event = json.loads(match[5])
        except (ValueError, TypeError):
            continue
        plugin = event.get("class", "")
        if not plugin.startswith("asst::spa::"):
            continue
        key = (match[2], match[3], plugin, event.get("taskid"))
        if match[4] == "SubTaskStart":
            active[key].append(when)
        elif active[key]:
            seconds = (when - active[key].pop()).total_seconds()
            if seconds >= 0:
                samples[plugin].append(
                    {"seconds": seconds, "completed": match[4] == "SubTaskCompleted"}
                )
        else:
            unmatched += 1
    summary = {}
    for plugin, calls in sorted(samples.items()):
        durations = sorted(call["seconds"] for call in calls)
        summary[plugin] = {
            "count": len(calls),
            "completed": sum(call["completed"] for call in calls),
            "failed": sum(not call["completed"] for call in calls),
            "mean_seconds": round(sum(durations) / len(durations), 3),
            "max_seconds": round(max(durations), 3),
            "total_seconds": round(sum(durations), 3),
        }
    return {
        "plugins": summary,
        "unfinished_calls": sum(len(starts) for starts in active.values()),
        "unmatched_end_events": unmatched,
        "scope": "Per-plugin wall time, including recognition and waits; nested calls overlap.",
        "game_success_verified": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.log.open(encoding="utf-8", errors="replace") as stream:
        result = audit_lines(stream)
    result["source_log"] = str(args.log.resolve())
    args.output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
