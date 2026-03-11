from __future__ import annotations


def build_base_topic(interface: str, version: str, manufacturer: str, serial: str) -> str:
    return f"{interface}/{version}/{manufacturer}/{serial}"


def build_connection_topic(base: str) -> str:
    return f"{base}/connection"


def build_state_topic(base: str) -> str:
    return f"{base}/state"


def build_factsheet_topic(base: str) -> str:
    return f"{base}/factsheet"


def build_actions_topic(base: str) -> str:
    return f"{base}/instantActions"
