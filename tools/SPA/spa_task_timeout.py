"""Host waiting budgets; native callbacks still determine task completion."""


class AutoPreparationProgress:
    """Extend a running batch only when its current native game advances."""

    # A recorded solo preparation phase took just over 19 minutes. Allow
    # the in-flight game to finish that work before stopping an idle host.
    inactivity_seconds = 20 * 60

    def __init__(self, run_count: int):
        self.run_count = run_count
        self.host_session = None
        self.execution = None
        self.executions = set()
        self.milestone = (0, 0)
        self.last_progress_at = None

    def observe(self, value: dict, now: float):
        host = value.get("host_session_id")
        execution = value.get("run_execution_id")
        if (
            not isinstance(host, str)
            or not host
            or not isinstance(execution, str)
            or not execution
        ):
            return
        if self.host_session is not None and host != self.host_session:
            return
        details = value.get("details", {})
        what, source = value.get("what"), value.get("class")
        if (
            what == "SPARunExecutionStarted"
            and source == "asst::StrongholdProtocolTask"
        ):
            if (
                execution not in self.executions
                and len(self.executions) < self.run_count
            ):
                self.host_session = host
                self.execution = execution
                self.executions.add(execution)
                self.milestone = (0, 0)
            return
        if host != self.host_session or execution != self.execution:
            return

        round_number = details.get("round")
        milestone = (0, 0)
        if (
            what == "SPAAutomationBattleObserved"
            and source == "asst::StrongholdProtocolTask"
            and details.get("battle_started") is True
            and details.get("next_round_ready") is True
            and type(round_number) is int
            and 1 <= round_number < 20
            and details.get("next_round") == round_number + 1
        ):
            milestone = (round_number + 1, 0)
        elif (
            what == "SPABattleSample"
            and source == "asst::spa::RoundTaskPlugin"
            and details.get("battle_frame_saved") is True
            and details.get("life_known") is True
            and details.get("enemies_total_known") is True
            and type(round_number) is int
            and 1 <= round_number <= 20
        ):
            milestone = (round_number, 1)
        elif (
            what == "SPARoundResultObserved"
            and source == "asst::spa::RoundTaskPlugin"
            and details.get("page") == "result"
            and details.get("battle_seen") is True
            and details.get("result_frame_saved") is True
            and details.get("result_text_verified") is True
        ):
            milestone = (21, 0)
        if milestone > self.milestone:
            self.milestone = milestone
            self.last_progress_at = now

    def deadline(self, base: float) -> float:
        if self.last_progress_at is None:
            return base
        return max(base, self.last_progress_at + self.inactivity_seconds)


def task_timeout_seconds(task: str, policy: dict | None = None) -> int:
    if task == "auto-preparation":
        if not isinstance(policy, dict):
            raise ValueError("auto-preparation policy must be an object")
        count = policy.get("run_count", 1)
        # Match the native integer/range contract, including rejection of bool.
        if type(count) is not int or not 1 <= count <= 99_999:
            raise ValueError("run_count must be an integer within 1..99999")
        # Python integers retain the full batch budget even at the legal maximum.
        return 7200 * count
    if task == "ready":
        return 300
    if task == "scan-strategy-report":
        return 360
    if task == "abandon-home":
        return 180
    return 90
