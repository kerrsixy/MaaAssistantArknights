"""Host waiting budgets; native callbacks still determine task completion."""


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
