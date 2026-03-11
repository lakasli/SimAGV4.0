from __future__ import annotations
from typing import Any, List

from ..L4_atoms import config_loader


class ElevatorStateMachine:
    def __init__(self, cfg: config_loader.EquipmentConfig) -> None:
        self.cfg = cfg
        self.door_open: bool = True
        self.current_floor: int = 1
        self.floors: List[int] = list(range(-99, 99))
        self._move_dir: str = "stop"
        self._travel_plan: list[int] = []
        self._door_close_due: float = 0.0
        self._door_open_due: float = 0.0
        self._next_floor_due: float = 0.0

    def open_door(self) -> None:
        self.door_open = True

    def close_door(self) -> None:
        self.door_open = False

    def move_to_floor(self, floor: int) -> list[int]:
        if floor not in self.floors:
            return []
        prev = self.current_floor
        path: list[int] = []
        if floor > prev:
            path = list(range(prev + 1, floor + 1))
            self._move_dir = "up"
        elif floor < prev:
            path = list(range(prev - 1, floor - 1, -1))
            self._move_dir = "down"
        else:
            self._move_dir = "stop"
        self._travel_plan = path
        return path

    def get_move_direction(self) -> str:
        return self._move_dir

    def set_travel_plan(self, plan: list[int]) -> None:
        self._travel_plan = plan

    def get_travel_plan(self) -> list[int]:
        return self._travel_plan

    def set_door_close_due(self, due: float) -> None:
        self._door_close_due = due

    def get_door_close_due(self) -> float:
        return self._door_close_due

    def set_door_open_due(self, due: float) -> None:
        self._door_open_due = due

    def get_door_open_due(self) -> float:
        return self._door_open_due

    def set_next_floor_due(self, due: float) -> None:
        self._next_floor_due = due

    def get_next_floor_due(self) -> float:
        return self._next_floor_due

    def update(self, now: float) -> dict[str, Any]:
        if self._door_close_due and now >= self._door_close_due and self.door_open:
            self.door_open = False
            self._door_close_due = 0.0
            if self._travel_plan:
                self._next_floor_due = now + 2.0

        if self._door_open_due and now >= self._door_open_due and not self.door_open:
            self.door_open = True
            self._door_open_due = 0.0

        if self._next_floor_due and now >= self._next_floor_due and self._travel_plan:
            nxt = self._travel_plan.pop(0)
            prev = self.current_floor
            self.current_floor = int(nxt)
            if self._travel_plan:
                self._move_dir = "up" if self.current_floor > prev else ("down" if self.current_floor < prev else self._move_dir)
                self._next_floor_due = now + 2.0
            else:
                self._move_dir = "stop"
                self._door_open_due = now + 3.0
                self._next_floor_due = 0.0
                return {"arrived": True}

        return {"arrived": False}

    def stop_movement(self) -> None:
        self._move_dir = "stop"
        self._travel_plan = []

    def get_current_state(self) -> dict[str, Any]:
        return {
            "door_open": self.door_open,
            "current_floor": self.current_floor,
            "move_dir": self._move_dir,
            "travel_plan": self._travel_plan,
        }

    def reset(self) -> None:
        self.door_open = True
        self.current_floor = 1
        self._move_dir = "stop"
        self._travel_plan = []
        self._door_close_due = 0.0
        self._door_open_due = 0.0
        self._next_floor_due = 0.0
