"""Import reviewed guide summaries and audit their selection metadata.

Only explicit source IDs are imported. Reference schedules are deliberately
kept separate from executable plans; author summaries are not win evidence.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "resource/config/MiniGame/SPAGuideRoutes.json"
NEW = {
    "fuzou-1": ("莱塔尼亚", ["band_bonus"], ["精准", "奥术"]),
    "yan-bison4": ("炎", ["band_closeout"], []),
    "tiepi-scope": ("灵巧", ["band_mech"], []),
    "victoria-stead": (
        "维多利亚",
        ["band_jesica", "band_bonus", "band_prepfin", "band_damaztic"],
        ["坚守"],
    ),
    "iberia-3": ("伊比利亚", ["band_prepfin", "band_damaztic"], ["坚守", "聚合"]),
}
NOTES = {
    "fuzou-1": [
        "The diagram uses III at R6-8 and IV at R9-10; narration permits III at R6/7 and IV at R8/9 only after a middle-game core arrives.",
        "V around R11 and VI around R13 remain conditional on the acquired engine.",
    ],
    "yan-bison4": [
        "The diagram reaches IV at R6; narration allows R5 only with favorable offers and economy.",
        "Stay IV normally. V requires elite key units and exceptional offers; VI is not required.",
        "The author summarizes the strategy as costing 9, but its live cost falls each round until used. Never hardcode a price of 9.",
    ],
    "tiepi-scope": [
        "The drawn R10-14 level V is illustrative: narration only says upgrade from R10; the final screenshot is VI without intermediate timing.",
        "Do not merge this R3-9 tier-II hold with preci-arcane, which leaves II at R8 even without communication gear.",
        "The damage-multiplier claim concerns large bosses and is not independent simulator evidence.",
    ],
    "victoria-stead": [
        "The diagram's R1-6 level III represents a I-III phase, not an initial level of III; early upgrade rounds are unspecified.",
        "Jessica aims for IV at R7; Fuzou saves pairs R1-5, promotes once each R6-8, and upgrades after receiving Jessica.",
        "V follows the key operator, as early as R8 in one example; VI timing is unspecified.",
    ],
    "iberia-3": [
        "R2 II and R4 III, then stay III for promotions and leave by R9/10.",
        "The final IV diagram represents IV-V-VI progression; V and VI rounds are unspecified.",
        "Precision is optional only with band_damaztic; do not impose it on band_prepfin.",
    ],
}


def read(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def import_reviewed(source: Path) -> None:
    text = source.read_text(encoding="utf-8-sig")
    prefix = "window.ARK_LINEUPS = "
    guides, _ = json.JSONDecoder().raw_decode(text.split(prefix, 1)[1])
    source_by_id = {guide["id"]: guide for guide in guides}
    current = read(TARGET)
    present = {guide["id"] for guide in current["guides"]}
    strategy_names = {
        item["name"]: item["band_id"]
        for item in read(ROOT / "resource/config/MiniGame/SPAStrategies.json")[
            "strategies"
        ]
    }
    for guide_id, (primary, strategies, required) in NEW.items():
        if guide_id in present:
            continue
        guide = source_by_id[guide_id]
        assert guide["pact"] == primary
        assert [strategy_names[item[0]] for item in guide["strategies"]] == strategies
        supported = guide_id != "tiepi-scope"
        imported = {
            "id": guide_id,
            "name": guide["name"],
            "primary": primary,
            "strategy_ids": strategies,
            "planner_supported": supported,
            "required_available_pacts": required,
            "target_pacts": [
                {"pact": name, "members": count} for name, count in guide["final"]
            ],
            "role_candidates": [
                {"role": role, "operators": operators}
                for role, operators in guide["roles"]
            ],
            "shop_plan": [],
            "source_drawn_shop_plan": [
                {
                    "first_round": first,
                    "last_round": last,
                    "shop_level": level,
                    "label": label,
                }
                for first, last, level, label in guide.get("road", [])
            ],
            "shop_plan_notes": NOTES[guide_id],
            "source_round_conditions": [
                {"text": item[0], "video_seconds": item[1]}
                for item in guide.get("roadTips", [])
            ],
            "equipment_candidates": [
                {"name": name, "recipient": recipient, "usage_note": note}
                for name, recipient, note in guide.get("items", [])
            ],
            "skill_preferences": [],
            "sources": [
                {
                    "url": item.get("url")
                    or "https://www.bilibili.com/video/" + item["bvid"],
                    "author": item.get("up", ""),
                    "title": item.get("title", ""),
                    "source_date_text": item.get("date", ""),
                }
                for item in guide["sources"]
            ],
            "source_capture": {
                "url": "https://squad-pool.com/arknights-lineups-data.js?v=20261118-1",
                "captured_date": "2026-11-18",
                "type": "author_video_summaries_not_original_video_verification",
            },
            "execution_verified": False,
            "round_plan_executed": False,
            "runtime_integration": {
                "primary_filter": supported,
                "documented_auxiliary_support_preferences": supported,
                "round_shop_plan_executed": False,
                "all_equipment_actions_verified": False,
                "whole_guide_execution_verified": False,
            },
            "mode_constraints": {
                "quick": "Fourteen-round reference requires adaptation to the quick ninth-round boss; no literal schedule execution."
            },
        }
        if not supported:
            imported["runtime_integration"]["limitation"] = (
                "Lingqiao primary planner is unavailable; reference only, excluded from selection and contention scoring."
            )
        if guide_id == "iberia-3":
            imported["conditional_support_pacts"] = [
                {"strategy_id": "band_damaztic", "pacts": ["精准"], "optional": True}
            ]
        current["guides"].append(imported)
    current["captured_date"] = "2026-11-18"
    current["source_counts"] = {
        "squadpool_reference_routes": 23,
        "skland_reviewed_routes": 3,
        "combined_unique_route_ids": 26,
        "note": "Source-specific IDs, not a count of executable or independently verified builds.",
    }
    current["integration_status"]["reference_only_routes"] = (
        "reverse has no Miracle execution loop; tiepi-scope has no Lingqiao primary planner."
    )
    TARGET.write_text(
        json.dumps(current, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def audit() -> None:
    data = read(TARGET)
    guides = {guide["id"]: guide for guide in data["guides"]}
    assert len(guides) == len(data["guides"]) == 26
    assert sum(name.startswith("skland-") for name in guides) == 3
    for name, (primary, strategies, required) in NEW.items():
        guide = guides[name]
        assert guide["primary"] == primary
        assert guide["strategy_ids"] == strategies
        assert guide["required_available_pacts"] == required
        assert guide["planner_supported"] == (name != "tiepi-scope")
        assert not guide["execution_verified"] and not guide["round_plan_executed"]
        assert not guide["runtime_integration"]["whole_guide_execution_verified"]
        assert not guide["runtime_integration"]["round_shop_plan_executed"]
        assert guide["shop_plan"] == [] and guide["shop_plan_notes"]
        assert (
            guide["sources"]
            and guide["equipment_candidates"]
            and guide["role_candidates"]
        )
    assert guides["tiepi-scope"]["primary"] != "精准"
    print(
        "Guide metadata audit passed: 23 SquadPool + 3 Skland IDs; five new references unverified."
    )


def audit_native(build_dir: Path, cmake: str) -> None:
    """Compile the actual header, exercising bans and unsupported references."""
    build_dir.mkdir(parents=True, exist_ok=True)
    source = build_dir / "source"
    source.mkdir(exist_ok=True)
    code = r"""
#include "Task/MiniGame/SPA/SPAGuideRoutes.h"
#include <cassert>
int main() {
    using namespace asst::spa;
    const auto has = [](const auto& values, const auto& item) {
        return std::find(values.begin(), values.end(), item) != values.end();
    };
    const auto& routes = strategy_guide_routes();
    const auto get = [&](const std::string& id) -> const StrategyGuideRoute& {
        const auto found = std::find_if(routes.begin(), routes.end(), [&](const auto& route) {
            return route.guide_id == id;
        });
        assert(found != routes.end());
        return *found;
    };
    assert(routes.size() == 26);
    assert(get("fuzou-1").strategies == std::vector<std::string>{"band_bonus"});
    assert(get("yan-bison4").strategies == std::vector<std::string>{"band_closeout"});
    assert(!guide_pacts_available(get("fuzou-1"), {"精准"}));
    assert(!guide_pacts_available(get("victoria-stead"), {"坚守"}));
    assert(!guide_pacts_available(get("iberia-3"), {"聚合"}));
    assert(guide_pacts_available(get("iberia-3"), {"精准"}));
    assert(guided_primary_choices("band_closeout").pacts == std::vector<std::string>{"炎"});
    assert(guided_primary_choices("band_closeout", {"炎"}).pacts.empty());
    assert(has(guided_primary_choices("band_prepfin").pacts, "伊比利亚"));
    assert(!has(guided_primary_choices("band_prepfin", {"聚合"}).pacts, "伊比利亚"));
    assert(!get("tiepi-scope").planner_supported);
    assert(!guide_pacts_available(get("tiepi-scope"), {}));
    assert(!has(guided_primary_choices("band_mech").pacts, "灵巧"));
    assert(guided_support_choices("band_mech", "灵巧").empty());
    assert(inferred_route_contention("灵巧", {"band_mech"}, {}) == 0);
    assert(inferred_route_contention("乌萨斯", {"band_mech"}, {}) == 50);
    assert(!guided_primary_choices("uncovered-strategy").guide_found);
    assert(inferred_route_contention("乌萨斯", {"band_jesica"}, {}) == 33);
    assert(inferred_route_contention("乌萨斯", {"band_jesica"}, {"莱塔尼亚"}) == 50);
}
"""
    (source / "GuideAudit.cpp").write_text(code, encoding="utf-8")
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.28)\nproject(GuideAudit LANGUAGES CXX)\n"
        "add_executable(GuideAudit GuideAudit.cpp)\n"
        f'target_include_directories(GuideAudit PRIVATE "{(ROOT / "src/MaaCore").as_posix()}")\n'
        "target_compile_features(GuideAudit PRIVATE cxx_std_20)\n"
        "if(MSVC)\n target_compile_options(GuideAudit PRIVATE /UNDEBUG /utf-8)\n"
        "else()\n target_compile_options(GuideAudit PRIVATE -UNDEBUG)\nendif()\n",
        encoding="utf-8",
    )
    subprocess.run(
        [
            cmake,
            "-S",
            str(source),
            "-B",
            str(build_dir),
            "-G",
            "Visual Studio 17 2022",
            "-A",
            "x64",
        ],
        check=True,
    )
    subprocess.run([cmake, "--build", str(build_dir), "--config", "Debug"], check=True)
    subprocess.run([str(build_dir / "Debug/GuideAudit.exe")], check=True)
    print("Debug native guide selection audit passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--import-source", type=Path)
    parser.add_argument("--native-build-dir", type=Path)
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    if args.import_source:
        import_reviewed(args.import_source)
    audit()
    if args.native_build_dir:
        audit_native(args.native_build_dir, args.cmake)
