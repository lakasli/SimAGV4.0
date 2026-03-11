from __future__ import annotations
from typing import Any, List

from ..L4_atoms import config_loader


class ActionHandler:
    def __init__(self, cfg: config_loader.EquipmentConfig) -> None:
        self.cfg = cfg

    def validate_floor(self, floor: Any, valid_floors: List[int]) -> bool:
        try:
            return int(floor) in valid_floors
        except (ValueError, TypeError):
            return False

    def get_trigger_mode(self) -> str:
        return str(self.cfg.settings.trigger_mode or "instant")

    def get_action_time(self) -> float:
        return float(self.cfg.settings.action_time)

    def handle_cancel(
        self,
        act_running: bool,
        act_id: str | None,
        act_type: str | None,
    ) -> tuple[bool, dict | None]:
        if act_running and self.get_trigger_mode() == "cancel":
            return True, {
                "action_id": str(act_id) if act_id else "",
                "action_status": "FAILED",
                "action_type": str(act_type) if act_type else "",
                "result_description": "canceled",
            }
        return False, None

    def handle_pause(
        self,
        act_running: bool,
        act_paused: bool,
    ) -> tuple[bool, bool, dict | None]:
        if act_running and self.get_trigger_mode() == "pause":
            new_paused = not act_paused
            return True, new_paused, {
                "action_id": "",
                "action_status": "PAUSED" if new_paused else "RUNNING",
                "action_type": "",
            }
        return False, act_paused, None
