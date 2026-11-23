"""Merge committed receipts without discarding previously observed geometry."""


def setup_pact_receipt(details: dict) -> dict:
    verified = details.get("selection_verified") is True
    availability_verified = (
        verified and details.get("availability_after_selection_verified") is True
    )
    return {
        "requested_core_pact": details.get(
            "requested_core_pact", details.get("core_pact", "")
        ),
        "core_pact": details.get("core_pact", "") if verified else "",
        "core_selection_verified": verified,
        "core_selection_mode": details.get("core_selection_mode", "explicit"),
        "provisional_core": not verified or details.get("provisional_core", False),
        "support_pacts": details.get("support_pacts", []) if verified else [],
        "disabled_pacts": details.get("disabled_pacts", [])
        if availability_verified
        else [],
        "pact_availability_verified": availability_verified,
        "setup_stage": details.get("stage", ""),
    }


def merge_receipt(previous: dict, receipt: dict) -> dict:
    if previous.get("run_id") != receipt.get("run_id"):
        return dict(receipt)
    merged = {**previous, **receipt}
    if "occupied_strategy_ids" in previous or "occupied_strategy_ids" in receipt:
        merged["occupied_strategy_ids"] = list(
            dict.fromkeys(
                previous.get("occupied_strategy_ids", [])
                + receipt.get("occupied_strategy_ids", [])
            )
        )
    return merged


def verified_boss_context(receipt: dict) -> dict:
    family = receipt.get("boss_family_name")
    if (
        receipt.get("boss_advertisement_verified") is not True
        or not isinstance(family, str)
        or not family.strip()
    ):
        return {}
    return {"boss_family_name": family, "boss_advertisement_verified": True}


def setup_context_receipt(event_name: str, details: dict) -> dict:
    """Retain verified bans/strategy independently of physical unit receipts."""
    if (
        event_name == "SPAPactAvailabilityScanned"
        and details.get("complete") is True
        and details.get("availability_after_selection_verified") is True
        and isinstance(details.get("disabled_pacts"), list)
    ):
        return {"disabled_pacts": list(details["disabled_pacts"])}
    if (
        event_name == "SPAStrategyCardSelected"
        and details.get("strategy_id")
        and (
            details.get("stage_three_verified") is True
            or details.get("joint_selection_committed") is True
        )
    ):
        return {"selected_strategy_id": details["strategy_id"]}
    if (
        event_name == "SPAStrategyOccupiedObserved"
        and details.get("occupied_label_verified") is True
        and isinstance(details.get("occupied_strategy_ids"), list)
        and all(
            isinstance(value, str) and value
            for value in details["occupied_strategy_ids"]
        )
    ):
        return {
            "occupied_strategy_ids": list(
                dict.fromkeys(details["occupied_strategy_ids"])
            )
        }
    return {}


def committed_shop_level(event_name: str, details: dict) -> int | None:
    """Retain the book level from the committed upgrade, scoped to its run."""
    if event_name != "SPAShopUpgradeReconciled":
        return None
    after = details.get("shop_level_after")
    before = details.get("shop_level_before")
    if type(after) is not int or not 2 <= after <= 6:
        return None
    if before is not None:
        return after if type(before) is int and before + 1 == after else None
    return after if details.get("schedule") == "phase1_fixed" else None


def checkpoint_roster(
    state: dict, previous_pending: str = ""
) -> tuple[list[dict], str]:
    """Keep physical form counts intact, including copies awaiting synthesis."""
    roster = [
        dict(entry) for entry in state.get("roster", []) if isinstance(entry, dict)
    ]
    pending = state.get("promotion_pending", previous_pending)
    if not isinstance(pending, str):
        pending = previous_pending
    if "promotion_pending" not in state and not pending:
        # Old native purchase receipts could precede their pending marker.
        # Recover a unique ordinary three-copy source without erasing a copy.
        sources = [
            entry.get("name")
            for entry in roster
            if type(entry.get("initial_copies")) is int
            and entry["initial_copies"] > 2
            and not entry.get("elite")
            and isinstance(entry.get("name"), str)
        ]
        if len(sources) == 1:
            pending = sources[0]
    return roster, pending


def verified_lifecycle_receipt(
    events: list[dict], chain_completed_message: int = 10002
) -> dict | None:
    receipt = None
    task_id = None
    for event in events:
        envelope = event.get("details", {})
        if envelope.get("what") == "SPALifecycleCompleted":
            details = envelope.get("details", {})
            named_joint_difficulty = details.get(
                "result_joint"
            ) is True and details.get("result_difficulty_name") in (
                "标准模拟",
                "险境模拟",
                "绝境模拟",
                "终极模拟",
            )
            named_quick_mode = (
                details.get("result_joint") is False
                and details.get("result_difficulty_name") == "极速模拟"
                and not details.get("result_difficulty_known")
            )
            if (
                details.get("return_home_verified")
                and details.get("battle_seen")
                and details.get("result_text_verified")
                and details.get("result_completed", False)
                != details.get("result_interrupted", False)
                and details.get("passed_rounds_known")
                and (
                    details.get("result_difficulty_known")
                    or named_joint_difficulty
                    or named_quick_mode
                )
            ):
                receipt = details
                task_id = envelope.get("taskid")
        if (
            receipt is not None
            and event.get("message") == chain_completed_message
            and envelope.get("taskchain") == "StrongholdProtocol"
            and envelope.get("taskid") == task_id
        ):
            return receipt
    return None
