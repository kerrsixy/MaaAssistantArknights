"""Host exit status from native terminal events, separate from victory acceptance."""


def audit_host_completion(events: list[dict], requested_runs: int) -> dict:
    """Keep settlement observations without treating a child as a completed batch."""
    failures = []
    settlements = []
    progress = []
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

    batch_complete = False
    if requested_runs > 1:
        # Native batches finish with their final progress then the outer chain
        # completion; each child shares the parent taskid. A child receipt alone
        # cannot close the host, even when an earlier callback says completed.
        if progress:
            index, last = progress[-1]
            final = last.get("details", {})
            batch_complete = (
                final.get("settled_runs") == requested_runs
                and final.get("requested_runs") == requested_runs
                and final.get("return_home_verified") is True
                and any(
                    event.get("message") == 10002
                    and event.get("details", {}).get("taskchain")
                    == "StrongholdProtocol"
                    and event.get("details", {}).get("taskid") == last.get("taskid")
                    and not event.get("details", {}).get("run_execution_id")
                    for event in events[index + 1 :]
                )
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
