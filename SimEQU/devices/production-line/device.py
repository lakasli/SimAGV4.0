from __future__ import annotations
import json
import time as _t
from pathlib import Path
from typing import Any, List

from SimEQU.src.L3_molecules.base_device import EquipmentDevice
from SimEQU.src.L4_atoms.config_loader import EquipmentConfig, load_config


class ProductionLineSim(EquipmentDevice):
    def __init__(self, cfg: EquipmentConfig) -> None:
        super().__init__(cfg)
        self._state = self._build_base_state()
        self._act_running = False
        self._act_paused = False
        self._act_remaining = float(self.cfg.settings.action_time)
        self._last_trigger_time = 0.0
        self._act_id: str | None = None
        self._act_type: str | None = None
        self._production_status: str = "stopped"
        self._product_status: str = "none"
        self._fault_status: str = "normal"
        self._output_count: int = 0
        self._efficiency: float = 0.0
        self._runtime: float = 0.0
        self._info_type: str = "PRODUCTION_LINE_STATE"
        self._info_desc: str | None = "STATE"
        self._info_level: str = "INFO"
        self._references_by_state: dict[str, list[dict]] = {
            "running": [
                {"reference_key": "name", "reference_value": "state"},
                {"reference_key": "value", "reference_value": "state:running"},
            ],
            "stopped": [
                {"reference_key": "name", "reference_value": "state"},
                {"reference_key": "value", "reference_value": "state:stopped"},
            ],
            "fault": [
                {"reference_key": "name", "reference_value": "state"},
                {"reference_key": "value", "reference_value": "state:fault"},
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
            for k in ("running", "stopped", "fault"):
                v = refs.get(k) or refs.get(k.upper())
                if isinstance(v, list):
                    self._references_by_state[k] = v
        except Exception:
            pass

    def _build_information(self) -> list[dict]:
        refs: list[dict] = []
        refs.append({"referenceKey": "name", "referenceValue": "state"})
        if self._production_status == "running":
            refs.append({"referenceKey": "value", "referenceValue": "state:running"})
        elif self._production_status == "fault":
            refs.append({"referenceKey": "value", "referenceValue": "state:fault"})
        else:
            refs.append({"referenceKey": "value", "referenceValue": "state:stopped"})
        refs.append({"referenceKey": "name", "referenceValue": "product"})
        refs.append({"referenceKey": "value", "referenceValue": f"product:{self._product_status}"})
        refs.append({"referenceKey": "name", "referenceValue": "output"})
        refs.append({"referenceKey": "value", "referenceValue": f"output:{self._output_count}"})
        return [
            {
                "infoType": self._info_type,
                "infoDescription": self._info_desc,
                "infoLevel": self._info_level,
                "infoReferences": refs,
            }
        ]

    def _start_thread(self) -> None:
        self._publish_factsheet("PRODUCTION_LINE")
        self._publish_state(self._state)
        super()._start_thread()

    def _run_loop(self) -> None:
        freq = max(1, int(self.cfg.settings.state_frequency))
        interval = 1.0 / freq

        while not self._stop:
            now = _t.time()

            if self._production_status == "running":
                self._runtime += interval
                self._efficiency = min(100.0, self._efficiency + 0.1)

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
                                        "action_status": "FINISHED",
                                        "action_type": self._act_type,
                                    }
                                ]
                                self._publish_state(self._state)
                            self._act_running = False
                    else:
                        if not self._act_paused:
                            self._act_remaining = max(0.0, float(self._act_remaining) - interval)
                        if self._act_remaining <= 0.0:
                            if self._act_id and self._act_type:
                                self._state["action_states"] = [
                                    {"action_id": self._act_id, "action_status": "FINISHED", "action_type": self._act_type}
                                ]
                                self._publish_state(self._state)
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
                self._publish_factsheet("PRODUCTION_LINE")
                out_states.append({"action_id": str(aid), "action_status": "FINISHED", "action_type": "factsheetRequest"})
                continue
            if at == "writeValue":
                cmd = None
                for p in params:
                    k = p.get("key", p.get("key", ""))
                    if k == "command":
                        cmd = str(p.get("value", ""))
                        break
                if cmd == "cmd:start":
                    self._production_status = "running"
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
                        self._act_remaining = float(self.cfg.settings.action_time)
                        self._last_trigger_time = _t.time()
                        self._act_id = str(aid)
                        self._act_type = "writeValue"
                        out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "writeValue"})
                elif cmd == "cmd:stop":
                    self._production_status = "stopped"
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
                        self._act_remaining = float(self.cfg.settings.action_time)
                        self._last_trigger_time = _t.time()
                        self._act_id = str(aid)
                        self._act_type = "writeValue"
                        out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "writeValue"})
                elif cmd == "cmd:reset":
                    self._output_count = 0
                    self._efficiency = 0.0
                    self._runtime = 0.0
                    self._fault_status = "normal"
                    out_states.append({"action_id": str(aid), "action_status": "FINISHED", "action_type": "writeValue"})
                else:
                    continue
            elif at == "startProduction":
                self._production_status = "running"
                mode = str(self.cfg.settings.trigger_mode or "instant")
                if mode == "cancel" and self._act_running:
                    out_states.append(
                        {"action_id": str(aid), "action_status": "FAILED", "action_type": "startProduction", "result_description": "canceled"}
                    )
                    self._act_running = False
                elif mode == "pause" and self._act_running:
                    self._act_paused = not self._act_paused
                    out_states.append(
                        {"action_id": str(aid), "action_status": ("PAUSED" if self._act_paused else "RUNNING"), "action_type": "startProduction"}
                    )
                else:
                    self._act_running = True
                    self._act_paused = False
                    self._act_remaining = float(self.cfg.settings.action_time)
                    self._last_trigger_time = _t.time()
                    self._act_id = str(aid)
                    self._act_type = "startProduction"
                    out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "startProduction"})
            elif at == "stopProduction":
                self._production_status = "stopped"
                mode = str(self.cfg.settings.trigger_mode or "instant")
                if mode == "cancel" and self._act_running:
                    out_states.append(
                        {"action_id": str(aid), "action_status": "FAILED", "action_type": "stopProduction", "result_description": "canceled"}
                    )
                    self._act_running = False
                elif mode == "pause" and self._act_running:
                    self._act_paused = not self._act_paused
                    out_states.append(
                        {"action_id": str(aid), "action_status": ("PAUSED" if self._act_paused else "RUNNING"), "action_type": "stopProduction"}
                    )
                else:
                    self._act_running = True
                    self._act_paused = False
                    self._act_remaining = float(self.cfg.settings.action_time)
                    self._last_trigger_time = _t.time()
                    self._act_id = str(aid)
                    self._act_type = "stopProduction"
                    out_states.append({"action_id": str(aid), "action_status": "RUNNING", "action_type": "stopProduction"})
            elif at == "resetCounter":
                self._output_count = 0
                self._efficiency = 0.0
                self._runtime = 0.0
                out_states.append({"action_id": str(aid), "action_status": "FINISHED", "action_type": "resetCounter"})
        if out_states:
            self._state["action_states"] = out_states
            self._publish_state(self._state)


def run(dir_path: str) -> None:
    cfg = load_config(Path(dir_path))
    sim = ProductionLineSim(cfg)
    sim.start()
    try:
        while True:
            _t.sleep(1)
    except KeyboardInterrupt:
        sim.stop()
