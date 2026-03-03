from __future__ import annotations
import json
import uuid
from typing import Optional
from pathlib import Path

import paho.mqtt.client as mqtt
try:
    from SimVehicleSys.config.settings import get_config  # type: ignore
except Exception:  # pragma: no cover
    get_config = None  # type: ignore

from .agv_manager import AGVManager
from .schemas import DynamicConfigUpdate, Position


class MQTTBridge:
    def __init__(self, config_path: Path, ws_broadcast, loop, agv_manager: Optional[AGVManager] = None):
        self.config_path = config_path
        self.ws_broadcast = ws_broadcast  # async function to broadcast json
        self.loop = loop
        self.agv_manager = agv_manager
        self.client: Optional[mqtt.Client] = None
        self._clients: list[mqtt.Client] = []
        # 初始化占位，真正值在 _load_config 中从集中设置读取
        self.host: str = ""
        self.port: int = 0
        self.vda_interface: str = ""
        self._brokers: list[tuple[str, int]] = []
        self._equip_interface: str = "uequip"
        self._load_config()

    def _dedup_brokers(self, brokers: list[tuple[str, int]]) -> list[tuple[str, int]]:
        dedup: list[tuple[str, int]] = []
        seen = set()
        for h, p in brokers:
            key = (str(h).strip(), int(p))
            if not key[0]:
                continue
            if key in seen:
                continue
            seen.add(key)
            dedup.append(key)
        return dedup

    def _load_config(self) -> None:
        import os
        import yaml
        host = ""
        port = 0
        vda_interface = ""
        if callable(get_config):
            try:
                cfg = get_config()
                host = str(cfg.mqtt_broker.host)
                port = int(str(cfg.mqtt_broker.port))
                vda_interface = str(cfg.mqtt_broker.vda_interface)
            except Exception:
                host, port, vda_interface = "", 0, ""
        
        # Load from config file (SimAGV/config.yaml) if available
        if self.config_path and self.config_path.exists():
            try:
                with open(self.config_path, "r", encoding="utf-8") as f:
                    file_cfg = yaml.safe_load(f)
                    if file_cfg:
                        if "mqtt_broker" in file_cfg:
                            mb = file_cfg["mqtt_broker"]
                            if "host" in mb:
                                host = str(mb["host"])
                            if "port" in mb:
                                port = int(mb["port"])
                        if "vehicle" in file_cfg:
                            veh = file_cfg["vehicle"]
                            if "vda_interface" in veh:
                                vda_interface = str(veh["vda_interface"])
            except Exception as e:
                print(f"MQTTBridge failed to load config from {self.config_path}: {e}")

        if not host:
            host = str(os.getenv("SIMAGV_MQTT_HOST", "127.0.0.1"))
        if not port:
            try:
                port = int(str(os.getenv("SIMAGV_MQTT_PORT", "1883")))
            except Exception:
                port = 1883
        if not vda_interface:
            vda_interface = str(os.getenv("SIMAGV_MQTT_VDA_INTERFACE", "uagv"))
        self.host = host
        self.port = port
        self.vda_interface = vda_interface
        try:
            env_equip_iface = os.getenv("SIMAGV_MQTT_EQUIP_INTERFACE")
            if env_equip_iface:
                self._equip_interface = str(env_equip_iface)
        except Exception:
            pass
        brokers: list[tuple[str, int]] = [(self.host, self.port)]
        extra_host = str(os.getenv("SIMAGV_MQTT_EXTRA_HOST", "")).strip()
        extra_port_raw = str(os.getenv("SIMAGV_MQTT_EXTRA_PORT", "")).strip()
        if extra_host:
            try:
                extra_port = int(extra_port_raw) if extra_port_raw else 1883
            except Exception:
                extra_port = 1883
            brokers.append((extra_host, extra_port))
        local_default = ("127.0.0.1", 1883)
        if local_default not in brokers:
            brokers.append(local_default)
        self._brokers = self._dedup_brokers(brokers)

    def refresh_brokers(self) -> None:
        current = list(self._brokers)
        try:
            self._load_config()
        except Exception:
            pass
        target = self._dedup_brokers(list(self._brokers))
        if target == current:
            return
        self._brokers = target
        if self._clients:
            try:
                self.stop()
            except Exception:
                pass
            try:
                self.start()
            except Exception:
                pass

    def start(self) -> None:
        self._clients = []
        self.client = None
        for host, port in list(self._brokers):
            client = mqtt.Client(client_id=str(uuid.uuid4()), protocol=mqtt.MQTTv5)
            client.reconnect_delay_set(min_delay=1, max_delay=10)
            client.on_message = self._on_message
            setattr(client, "_simagv_broker", f"{host}:{port}")
            try:
                client.connect(host, port, keepalive=60)
            except Exception as e:
                print(f"MQTTBridge connect failed ({host}:{port}): {e}")
                continue
            try:
                client.subscribe(f"{self.vda_interface}/#", qos=1)
            except Exception:
                pass
            try:
                client.subscribe(f"{self._equip_interface}/#", qos=1)
            except Exception:
                pass
            try:
                client.loop_start()
                self._clients.append(client)
            except Exception:
                try:
                    client.disconnect()
                except Exception:
                    pass
        if self._clients:
            self.client = self._clients[0]

    def stop(self) -> None:
        try:
            for c in list(self._clients):
                try:
                    c.loop_stop()
                except Exception:
                    pass
                try:
                    c.disconnect()
                except Exception:
                    pass
        except Exception:
            pass
        self._clients = []
        self.client = None

    @staticmethod
    def _get(payload: dict, snake: str, camel: str, default=None):
        return payload.get(snake, payload.get(camel, default))

    def _sink_state_into_manager(self, payload: dict) -> None:
        # 提取序列号（支持 snake/camel）
        serial = str(self._get(payload, "serial_number", "serialNumber", "")).strip()
        if not serial or not self.agv_manager:
            return
        # 位置（支持 snake/camel）
        agv_pos = self._get(payload, "agv_position", "agvPosition", {}) or {}
        x = self._get(agv_pos, "x", "x", None)
        y = self._get(agv_pos, "y", "y", None)
        theta = self._get(agv_pos, "theta", "theta", None)
        map_id = self._get(agv_pos, "map_id", "mapId", None)
        # 电量（支持 snake/camel）
        batt = self._get(payload, "battery_state", "batteryState", {}) or {}
        battery_charge = self._get(batt, "battery_charge", "batteryCharge", None)

        dyn = DynamicConfigUpdate()
        try:
            if map_id is not None:
                dyn.current_map = str(map_id)
        except Exception:
            pass
        try:
            if battery_charge is not None:
                # 转为 int 百分比
                dyn.battery_level = int(float(battery_charge))
        except Exception:
            pass
        pos = None
        try:
            if x is not None or y is not None or theta is not None:
                pos = Position(
                    x=float(x) if x is not None else 0.0,
                    y=float(y) if y is not None else 0.0,
                    theta=float(theta) if theta is not None else None,
                )
        except Exception:
            pos = None
        if pos:
            dyn.position = pos
        try:
            self.agv_manager.update_dynamic(serial, dyn)
        except Exception as e:
            # 保守容错，不影响消息转发
            print(f"MQTTBridge state sink failed for {serial}: {e}")

    def _on_message(self, client: mqtt.Client, userdata, msg: mqtt.MQTTMessage):
        topic = msg.topic
        payload_raw = msg.payload.decode("utf-8", errors="ignore")
        try:
            payload = json.loads(payload_raw)
        except Exception:
            payload = payload_raw
        # type by last path segment
        topic_type = topic.rsplit("/", 1)[-1] if "/" in topic else topic
        broker = ""
        try:
            broker = str(getattr(client, "_simagv_broker", "") or "")
        except Exception:
            broker = ""
        message = {"type": f"mqtt_{topic_type}", "topic": topic, "payload": payload, "broker": broker}
        try:
            import asyncio
            asyncio.run_coroutine_threadsafe(self.ws_broadcast(message), self.loop)
        except Exception as e:
            print(f"MQTTBridge forward failed: {e}")
        # 将 state 消息同步到后端实例状态
        try:
            if isinstance(payload, dict) and str(topic_type).lower() == "state":
                self._sink_state_into_manager(payload)
        except Exception:
            pass
