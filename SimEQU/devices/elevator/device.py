from __future__ import annotations
import json
import time as _t
from pathlib import Path
from typing import Any, List

from SimEQU.src.L3_molecules.base_device import EquipmentDevice
from SimEQU.src.L3_molecules.elevator_state_machine import ElevatorStateMachine
from SimEQU.src.L3_molecules.action_handler import ActionHandler
from SimEQU.src.L4_atoms.config_loader import EquipmentConfig, load_config


class ElevatorSim(EquipmentDevice):
    def __init__(self, cfg: EquipmentConfig) -> None:
        super().__init__(cfg)
        self._state_machine = ElevatorStateMachine(cfg)
        self._action_handler = ActionHandler(cfg)
        self._state = self._build_base_state()
        self._act_running = False
        self._act_paused = False
        self._act_remaining = float(self.cfg.settings.action_time)
        self._last_trigger_time = 0.0
        self._act_id: str | None = None
        self._act_type: str | None = None
        self._info_type: str = "CUSTOM_REGISTER"
        self._info_desc: str | None = "REGISTER_STATE"
        self._info_level: str = "INFO"
        self._references_by_state: dict[str, list[dict]] = {
            "open": [
                {"reference_key": "name", "reference_value": "state"},
                {"reference_key": "value", "reference_value": "state:open"},
            ],
            "close": [
                {"reference_key": "name", "reference_value": "state"},
                {"reference_key": "value", "reference_value": "state:close"},
            ],
            "up": [
                {"reference_key": "name", "reference_value": "state"},
                {"reference_key": "value", "reference_value": "state:up"},
            ],
            "down": [
                {"reference_key": "name", "reference_value": "state"},
                {"reference_key": "value", "reference_value": "state:down"},
            ],
            "floor": [
                {"reference_key": "name", "reference_value": "state"},
                {"reference_key": "value", "reference_value": "state:floor"},
            ],
        }
        self._load_info_config()

    def _load_info_config(self) -> None:
        try:
            fp = Path(__file__).resolve().parent / "info_config.json"
            data = json.loads(fp.read_text(encoding="utf-8"))
            self._info_type = str(data.get("info_type", self._info_type))
            self._info_desc = data.get("info_description", self._info_desc)
            self._info_level = str(data.get("info_level", self._info_level))
            refs = data.get("references_by_state", {}) or {}
            for k in ("open", "close", "up", "down", "floor"):
                v = refs.get(k) or refs.get(k.upper())
                if isinstance(v, list):
                    self._references_by_state[k] = v
        except Exception:
            pass

    def _build_information(self) -> list[dict]:
        refs: list[dict] = []
        refs.append({"referenceKey": "name", "referenceValue": "state"})
        refs.append(
            {"referenceKey": "value", "referenceValue": ("state:open" if self._state_machine.door_open else "state:close")}
        )
        refs.append({"referenceKey": "value", "referenceValue": f"state:floor:{self._state_machine.current_floor}"})
        move_dir = self._state_machine.get_move_direction()
        if move_dir == "up":
            refs.append({"referenceKey": "value", "referenceValue": "state:up"})
        elif move_dir == "down":
            refs.append({"referenceKey": "value", "referenceValue": "state:down"})
        return [
            {
                "infoType": "ELEVATORSTATE",
                "infoDescription": "STATE",
                "infoLevel": "INFO",
                "infoReferences": refs,
            }
        ]

    def _start_thread(self) -> None:
        self._publish_factsheet("ELEVATOR")
        self._publish_state(self._state)
        super()._start_thread()

    def _run_loop(self) -> None:
        freq = max(1, int(self.cfg.settings.state_frequency))
        interval = 1.0 / freq

        while not self._stop:
            now = _t.time()

            arrival = self._state_machine.update(now)
            if arrival.get("arrived") and self._act_id and self._act_type:
                self._state["action_states"] = [
                    {"action_id": self._act_id, "action_status": "FINISHED", "action_type": self._act_type}
                ]
                self._publish_state(self._state)
                self._act_id = None
                self._act_type = None
                self._act_running = False

            self._state["information"] = self._build_information()
            self._publish_state(self._state)

            try:
                if self._act_running:
                    mode = str(self.cfg.settings.trigger_mode or "instant")
                    if mode == "hold":
                        if (now - float(self._last_trigger_time)) > max(0.0, float(self.cfg.settings.action_time)):
                            if self._act_id and self._act_type:
                                self._state["action_states"] = [
                                    {
                                        "action_id": self._act_id,
                                        "action_status": "FAILED",
                                        "action_type": self._act_type,
                                        "result_description": "interrupted",
                                    }
                                ]
                                self._publish_state(self._state)
                            self._act_running = False
                    else:
                        travel_plan = self._state_machine.get_travel_plan()
                        door_close_due = self._state_machine.get_door_close_due()
                        next_floor_due = self._state_machine.get_next_floor_due()
                        if not (travel_plan or door_close_due or next_floor_due):
                            if not self._act_paused:
                                self._act_remaining = max(0.0, float(self._act_remaining) - interval)
                        if (
                            not (travel_plan or door_close_due or next_floor_due)
                        ) and (self._act_remaining <= 0.0):
                            if self._act_id and self._act_type:
                                self._state["action_states"] = [
                                    {"action_id": self._act_id, "action_status": "FINISHED", "action_type": self._act_type}
                                ]
                                self._publish_state(self._state)
                            self._state_machine.stop_movement()
                            self._act_running = False
                            self._act_remaining = float(self.cfg.settings.action_time)
            except Exception:
                pass
            _t.sleep(interval)

    def _handle_instant_actions(self, data: dict) -> None:
        actions: List[dict] = data.get("actions", data.get("actions", [])) or []
        out_states = []
        for a in actions:
            at = a.get("action_type", a.get("actionType", ""))
            aid = a.get("action_id", a.get("actionId", ""))
            params = a.get("action_parameters", a.get("actionParameters", [])) or []
            if at == "factsheetRequest":
                self._publish_factsheet("ELEVATOR")
                out_states.append({"action_id": str(aid), "action_status": "FINISHED", "action_type": "factsheetRequest"})
                continue
            if at == "writeValue":
                cmd = None
                for p in params:
                    k = p.get("key", p.get("key", ""))
                    if k == "command":
                        cmd = str(p.get("value", ""))
                        break
                if cmd == "cmd:press":
                    self._state_machine.open_door()
                    self._state_machine.stop_movement()
                    mode = str(self.cfg.settings.trigger_mode or "instant")
                    if mode == "cancel" and self._act_running:
                        self._state_machine.close_door()
                        out_states.append(
                            {"action_id": str(aid), "action_status": "FAILED", "action_type": "writeValue", "result_description": "canceled"}
                        )
                        self._act_running = False
                    elif mode == "pause" and self._act_running:
                        self._act_paused = not self._act_paused
                        out_states.append(
                            {"action_id": str(aid), "action_status": ("PAUSED" if self._act_paused else "RUNNING"), "action_type": "writeValue"}
                        )
                    else:
                        self._act_running = True
                        self._act_paused = False
                        self._act_remaining = float(self.cfg.settings.action_time)
                        self._last_trigger_time = _t.time()
                        self._state_machine.set_door_close_due(_t.time() + 3.0)
                        self._act_id = str(aid)
                        self._act_type = "writeValue"
                        out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "writeValue"})
                elif cmd and cmd.startswith("cmd:press:"):
                    try:
                        tgt = int(cmd.split(":")[-1])
                    except Exception:
                        tgt = None
                    if tgt is not None and tgt in self._state_machine.floors:
                        prev = self._state_machine.current_floor
                        path: list[int] = []
                        if tgt > prev:
                            path = list(range(prev + 1, tgt + 1))
                            self._state_machine._move_dir = "up"
                        elif tgt < prev:
                            path = list(range(prev - 1, tgt - 1, -1))
                            self._state_machine._move_dir = "down"
                        else:
                            path = []
                            self._state_machine._move_dir = "stop"
                        self._state_machine.set_travel_plan(path)
                        self._state_machine.set_door_close_due(_t.time() + 3.0)
                        if path:
                            self._state_machine.set_next_floor_due(_t.time() + 2.0)
                        mode = str(self.cfg.settings.trigger_mode or "instant")
                        if mode == "cancel" and self._act_running:
                            out_states.append(
                                {"action_id": str(aid), "action_status": "FAILED", "action_type": "writeValue", "result_description": "canceled"}
                            )
                            self._act_running = False
                        elif mode == "pause" and self._act_running:
                            self._act_paused = not self._act_paused
                            out_states.append(
                                {"action_id": str(aid), "action_status": ("PAUSED" if self._act_paused else "RUNNING"), "action_type": "writeValue"}
                            )
                        else:
                            self._act_running = True
                            self._act_paused = False
                            self._act_id = str(aid)
                            self._act_type = "writeValue"
                            out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "writeValue"})
                    else:
                        out_states.append(
                            {"action_id": str(aid), "action_status": "FAILED", "action_type": "writeValue", "result_description": "invalidFloor"}
                        )
                else:
                    continue
            elif at == "openDoor":
                self._state_machine.open_door()
                self._state_machine.stop_movement()
                mode = str(self.cfg.settings.trigger_mode or "instant")
                if mode == "cancel" and self._act_running:
                    self._state_machine.close_door()
                    out_states.append(
                        {"action_id": str(aid), "action_status": "FAILED", "action_type": "openDoor", "result_description": "canceled"}
                    )
                    self._act_running = False
                elif mode == "pause" and self._act_running:
                    self._act_paused = not self._act_paused
                    out_states.append(
                        {"action_id": str(aid), "action_status": ("PAUSED" if self._act_paused else "RUNNING"), "action_type": "openDoor"}
                    )
                else:
                    self._act_running = True
                    self._act_paused = False
                    self._act_remaining = float(self.cfg.settings.action_time)
                    self._last_trigger_time = _t.time()
                    self._act_id = str(aid)
                    self._act_type = "openDoor"
                    out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "openDoor"})
            elif at == "closeDoor":
                self._state_machine.close_door()
                self._state_machine.stop_movement()
                mode = str(self.cfg.settings.trigger_mode or "instant")
                if mode == "cancel" and self._act_running:
                    out_states.append(
                        {"action_id": str(aid), "action_status": "FAILED", "action_type": "closeDoor", "result_description": "canceled"}
                    )
                    self._act_running = False
                elif mode == "pause" and self._act_running:
                    self._act_paused = not self._act_paused
                    out_states.append(
                        {"action_id": str(aid), "action_status": ("PAUSED" if self._act_paused else "RUNNING"), "action_type": "closeDoor"}
                    )
                else:
                    self._act_running = True
                    self._act_paused = False
                    self._act_remaining = float(self.cfg.settings.action_time)
                    self._last_trigger_time = _t.time()
                    self._act_id = str(aid)
                    self._act_type = "closeDoor"
                    out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "closeDoor"})
            elif at == "moveToFloor":
                tgt = None
                for p in params:
                    k = p.get("key", p.get("key", ""))
                    if k == "floor":
                        try:
                            tgt = int(p.get("value"))
                        except Exception:
                            tgt = None
                        break
                if tgt is not None and tgt in self._state_machine.floors:
                    prev = self._state_machine.current_floor
                    path: list[int] = []
                    if tgt > prev:
                        path = list(range(prev + 1, tgt + 1))
                        self._state_machine._move_dir = "up"
                    elif tgt < prev:
                        path = list(range(prev - 1, tgt - 1, -1))
                        self._state_machine._move_dir = "down"
                    else:
                        path = []
                        self._state_machine._move_dir = "stop"
                    self._state_machine.set_travel_plan(path)
                    self._state_machine.set_door_close_due(_t.time() + 3.0)
                    if path:
                        self._state_machine.set_next_floor_due(_t.time() + 2.0)
                    mode = str(self.cfg.settings.trigger_mode or "instant")
                    if mode == "cancel" and self._act_running:
                        out_states.append(
                            {"action_id": str(aid), "action_status": "FAILED", "action_type": "moveToFloor", "result_description": "canceled"}
                        )
                        self._act_running = False
                    elif mode == "pause" and self._act_running:
                        self._act_paused = not self._act_paused
                        out_states.append(
                            {"action_id": str(aid), "action_status": ("PAUSED" if self._act_paused else "RUNNING"), "action_type": "moveToFloor"}
                        )
                    else:
                        self._act_running = True
                        self._act_paused = False
                        self._act_id = str(aid)
                        self._act_type = "moveToFloor"
                        out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "moveToFloor"})
                else:
                    out_states.append(
                        {"action_id": str(aid), "action_status": "FAILED", "action_type": "moveToFloor", "result_description": "invalidFloor"}
                    )
        if out_states:
            self._state["action_states"] = out_states
            self._publish_state(self._state)


def run(dir_path: str) -> None:
    cfg = load_config(Path(dir_path))
    sim = ElevatorSim(cfg)
    sim.start()
    try:
        while True:
            _t.sleep(1)
    except KeyboardInterrupt:
        sim.stop()
