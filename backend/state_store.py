from __future__ import annotations

import threading
from datetime import datetime
from typing import Dict, Optional

from .schemas import AGVInfo, AGVRuntime, DynamicConfigUpdate, Position, StatusResponse


class GlobalStore:
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._runtime_by_serial: Dict[str, AGVRuntime] = {}

    def _ensure_runtime(self, serial_number: str) -> AGVRuntime:
        serial_number_s = str(serial_number).strip()
        if not serial_number_s:
            serial_number_s = "unknown"
        rt = self._runtime_by_serial.get(serial_number_s)
        if rt is None:
            rt = AGVRuntime()
            self._runtime_by_serial[serial_number_s] = rt
        return rt

    def _copy_runtime(self, rt: AGVRuntime) -> AGVRuntime:
        try:
            return rt.model_copy(deep=True)
        except Exception:
            return rt

    def get_runtime(self, serial_number: str) -> AGVRuntime:
        with self._lock:
            rt = self._ensure_runtime(serial_number)
            return self._copy_runtime(rt)

    def update_dynamic(self, serial_number: str, dyn: DynamicConfigUpdate) -> Optional[AGVRuntime]:
        serial_number_s = str(serial_number).strip()
        if not serial_number_s:
            return None
        with self._lock:
            rt = self._ensure_runtime(serial_number_s)
            try:
                if dyn.battery_level is not None:
                    rt.battery_level = int(dyn.battery_level)
            except Exception:
                pass
            try:
                if dyn.current_map is not None:
                    rt.current_map = str(dyn.current_map)
            except Exception:
                pass
            try:
                if dyn.speed_limit is not None:
                    rt.speed_limit = float(dyn.speed_limit)
            except Exception:
                pass
            try:
                if dyn.movement_state is not None:
                    rt.movement_state = str(dyn.movement_state)
            except Exception:
                pass
            try:
                if dyn.position is not None:
                    rt.position = Position(
                        x=float(dyn.position.x),
                        y=float(dyn.position.y),
                        theta=float(dyn.position.theta) if dyn.position.theta is not None else rt.position.theta,
                    )
            except Exception:
                pass
            rt.last_update = datetime.utcnow()
            return self._copy_runtime(rt)

    def move_translate(
        self,
        serial_number: str,
        dx: float,
        dy: float,
        movement_state: str | None = None,
    ) -> Optional[AGVRuntime]:
        serial_number_s = str(serial_number).strip()
        if not serial_number_s:
            return None
        with self._lock:
            rt = self._ensure_runtime(serial_number_s)
            try:
                rt.position.x = float(rt.position.x) + float(dx)
            except Exception:
                pass
            try:
                rt.position.y = float(rt.position.y) + float(dy)
            except Exception:
                pass
            try:
                if movement_state is not None:
                    rt.movement_state = str(movement_state)
            except Exception:
                pass
            rt.last_update = datetime.utcnow()
            return self._copy_runtime(rt)

    def move_rotate(self, serial_number: str, dtheta: float) -> Optional[AGVRuntime]:
        serial_number_s = str(serial_number).strip()
        if not serial_number_s:
            return None
        with self._lock:
            rt = self._ensure_runtime(serial_number_s)
            try:
                base = float(rt.position.theta or 0.0)
                rt.position.theta = base + float(dtheta)
            except Exception:
                pass
            rt.last_update = datetime.utcnow()
            return self._copy_runtime(rt)

    def get_status(self, serial_number: str, info: AGVInfo | None) -> Optional[StatusResponse]:
        serial_number_s = str(serial_number).strip()
        if not serial_number_s:
            return None
        if info is None:
            return None
        with self._lock:
            rt = self._ensure_runtime(serial_number_s)
            return StatusResponse(
                serial_number=serial_number_s,
                status="online",
                battery_level=int(getattr(rt, "battery_level", 100)),
                current_map=getattr(rt, "current_map", None),
                position=getattr(rt, "position", Position(x=0.0, y=0.0, theta=0.0)),
                speed=float(getattr(rt, "speed", 0.0)),
                last_update=getattr(rt, "last_update", datetime.utcnow()),
                errors=list(getattr(rt, "errors", []) or []),
            )


global_store = GlobalStore()

