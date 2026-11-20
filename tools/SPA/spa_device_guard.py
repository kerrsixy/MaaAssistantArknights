"""Bind each live invocation to an explicitly authorized game channel."""

CLIENT_PACKAGES = {
    "Bilibili": "com.hypergryph.arknights.bilibili",
    "Official": "com.hypergryph.arknights",
}


def authorize_client(client_type, resumed, focused, policy=None):
    package = CLIENT_PACKAGES.get(client_type)
    if package is None:
        raise ValueError("Unsupported authorized client")
    if policy is not None and policy.get("client_type", client_type) != client_type:
        raise ValueError("Policy client differs from the explicitly authorized client")
    component = package + "/"
    if (sum(component in line for line in resumed) != 1
            or len(focused) != 1 or component not in focused[0]):
        raise ValueError(f"Device is not confirmed in the authorized {client_type} game")
    return package
