"""Host exit status from native terminal events, separate from victory acceptance."""

from spa_staged_acceptance import select_execution, strategy_catalogue_badge_evidence


def _outer_completion_after(events, index, task_id):
    return any(
        event.get("message") == 10002
        and event.get("details", {}).get("taskchain") == "StrongholdProtocol"
        and event.get("details", {}).get("taskid") == task_id
        and not event.get("details", {}).get("run_execution_id")
        for event in events[index + 1 :]
    )


def _verified_catalogue_stop(events, stop, settled_runs, requested_runs):
    index, envelope = stop
    detail = envelope.get("details", {})
    report_run = detail.get("report_run_id")
    task_id = envelope.get("taskid")
    if (
        envelope.get("class") != "asst::StrongholdProtocolTask"
        or envelope.get("run_execution_id")
        or type(detail.get("settled_runs")) is not int
        or detail["settled_runs"] != settled_runs
        or type(detail.get("requested_runs")) is not int
        or detail["requested_runs"] != requested_runs
        or not 0 <= settled_runs < requested_runs
        or not isinstance(report_run, str)
        or not report_run
        or not _outer_completion_after(events, index, task_id)
    ):
        return False

    child = [
        event
        for event in events[:index]
        if event.get("details", {}).get("taskchain") == "StrongholdProtocol"
        and event["details"].get("taskid") == task_id
        and event["details"].get("run_id") == report_run
    ]
    starts = [
        e["details"]
        for e in child
        if e["details"].get("what") == "SPARunExecutionStarted"
    ]
    if len(starts) != 1 or not starts[0].get("run_execution_id"):
        return False
    child = select_execution(child, starts[0]["run_execution_id"])
    terminals = [
        e["details"].get("details", {})
        for e in child
        if e["details"].get("what") == "SPARunExecutionFinished"
    ]
    return (
        len(terminals) == 1
        and terminals[0].get("runner_success") is True
        and terminals[0].get("stop_requested") is False
        and terminals[0].get("run_victory_verified") is not True
        and not any(
            e["details"].get("what")
            in ("SPADifficultySelected", "SPARoundStarted", "SPALifecycleCompleted")
            for e in child
        )
        and strategy_catalogue_badge_evidence(child) is not None
    )


def audit_host_completion(
    events: list[dict], requested_runs: int, *, schedule_strategy_badges: bool = False
) -> dict:
    """Keep settlement observations without treating a child as a completed batch."""
    failures = []
    settlements = []
    progress = []
    catalogue_stops = []
    reported_settled = 0
    for index, event in enumerate(events):
        envelope = event.get("details", {})
        if envelope.get("taskchain") != "StrongholdProtocol":
            continue
        what = envelope.get("what")
        details = envelope.get("details", {})
        if what == "SPALifecycleCompleted":
            # This is a copy of native observations, not a new completion event,
            # an independently verified child receipt, or a victory count.
            settlements.append(
                {
                    "event_index": index,
                    **{
                        key: envelope[key]
                        for key in ("run_id", "run_execution_id", "taskid")
                        if key in envelope
                    },
                    "observation": {
                        key: details[key]
                        for key in (
                            "passed_rounds",
                            "passed_rounds_known",
                            "result_completed",
                            "result_interrupted",
                            "result_text_verified",
                            "result_joint",
                            "result_difficulty",
                            "result_difficulty_known",
                            "result_difficulty_name",
                            "return_home_verified",
                            "battle_seen",
                        )
                        if key in details
                    },
                }
            )
        if what in ("SPABatchStopped", "SPALifecycleIncomplete"):
            failures.append(what)
        if what == "SPARunExecutionFinished" and (
            details.get("runner_success") is False
            or details.get("stop_requested") is True
        ):
            failures.append("native_execution_failed_or_stopped")
        if what == "SPABatchStrategyCatalogueCompleted":
            catalogue_stops.append((index, envelope))
        if what in ("SPABatchProgress", "SPABatchStopped"):
            count = details.get("settled_runs")
            if (
                type(count) is not int
                or not 0 <= count <= requested_runs
                or type(details.get("requested_runs")) is not int
                or details["requested_runs"] != requested_runs
            ):
                failures.append("batch_count_mismatch")
            else:
                if count < reported_settled:
                    failures.append("batch_count_regressed")
                reported_settled = count
            if what == "SPABatchProgress":
                progress.append((index, envelope))

    catalogue_verified = (
        schedule_strategy_badges
        and len(catalogue_stops) == 1
        and reported_settled == len(settlements)
        and (not progress or progress[-1][0] < catalogue_stops[0][0])
        and _verified_catalogue_stop(
            events, catalogue_stops[0], reported_settled, requested_runs
        )
    )
    if catalogue_stops and not catalogue_verified:
        failures.append("strategy_catalogue_completion_unverified")

    batch_complete = catalogue_verified
    if requested_runs > 1:
        # Native batches finish with their final progress then the outer chain
        # completion; each child shares the parent taskid. A child receipt alone
        # cannot close the host, even when an earlier callback says completed.
        if progress and not catalogue_stops:
            index, last = progress[-1]
            final = last.get("details", {})
            batch_complete = (
                final.get("settled_runs") == requested_runs
                and final.get("requested_runs") == requested_runs
                and final.get("return_home_verified") is True
                and _outer_completion_after(events, index, last.get("taskid"))
            )
        if not batch_complete:
            failures.append("batch_not_completed")
        if any(
            event.get("message") in (10000, 10004)
            and event.get("details", {}).get("taskchain") == "StrongholdProtocol"
            and not event.get("details", {}).get("run_execution_id")
            for event in events
        ):
            failures.append("outer_batch_failed_or_stopped")

    return {
        "requested_runs": requested_runs,
        "native_reported_settled_runs": reported_settled
        if requested_runs > 1
        else None,
        "batch_complete": batch_complete and not failures,
        "strategy_catalogue_complete": catalogue_verified and not failures,
        "host_failure": bool(failures),
        "failure_reasons": list(dict.fromkeys(failures)),
        "settlement_observations": settlements,
        "victory_acceptance_evaluated": False,
    }


def require_host_completion(audit: dict) -> None:
    if audit["host_failure"]:
        raise RuntimeError(
            "SPA native execution incomplete: " + ", ".join(audit["failure_reasons"])
        )
