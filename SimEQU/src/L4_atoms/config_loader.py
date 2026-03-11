from __future__ import annotations
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional


@dataclass
class MqttConfig:
    host: str
    port: int
    vda_interface: str


@dataclass
class EquipmentIdentity:
    manufacturer: str
    serial_number: str
    vda_version: str
    vda_full_version: str


@dataclass
class EquipmentSettings:
    name: str
    ip: str
    site: Optional[list[dict[str, Any]]] = None
    map_id: Optional[str] = None
    state_frequency: int = 1
    action_time: float = 0.0
    trigger_mode: str = "instant"


@dataclass
class EquipmentConfig:
    mqtt: MqttConfig
    identity: EquipmentIdentity
    settings: EquipmentSettings


def load_config(dir_path: Path) -> EquipmentConfig:
    fp = Path(dir_path) / "config.json"
    data = json.loads(fp.read_text(encoding="utf-8"))
    mqtt = MqttConfig(
        host=str(data.get("mqtt", {}).get("host", "127.0.0.1")),
        port=int(str(data.get("mqtt", {}).get("port", 9527))),
        vda_interface=str(data.get("mqtt", {}).get("vda_interface", "uequip")),
    )
    ident = EquipmentIdentity(
        manufacturer=str(data.get("manufacturer", data.get("identity", {}).get("manufacturer", "SimEquip"))),
        serial_number=str(data.get("serial_number", data.get("identity", {}).get("serial_number", data.get("name", "Device")))),
        vda_version=str(data.get("vda_version", data.get("identity", {}).get("vda_version", "v2"))),
        vda_full_version=str(data.get("vda_full_version", data.get("identity", {}).get("vda_full_version", "2.0.0"))),
    )
    settings = EquipmentSettings(
        name=str(data.get("name", data.get("settings", {}).get("name", ident.serial_number))),
        ip=str(data.get("ip", data.get("settings", {}).get("ip", "0.0.0.0"))),
        site=(data.get("site") or data.get("settings", {}).get("site")),
        map_id=(data.get("map_id") or data.get("settings", {}).get("map_id") or "default"),
        state_frequency=int(data.get("state_frequency", data.get("settings", {}).get("state_frequency", 1))),
        action_time=float(data.get("action_time", data.get("settings", {}).get("action_time", 0.0))),
        trigger_mode=str(data.get("trigger_mode", data.get("settings", {}).get("trigger_mode", "instant"))),
    )
    return EquipmentConfig(mqtt=mqtt, identity=ident, settings=settings)
