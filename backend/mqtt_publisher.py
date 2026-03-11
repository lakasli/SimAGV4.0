from __future__ import annotations
import json
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

import paho.mqtt.client as mqtt

get_config = None  # SimVehicleSys not available
canonicalize_map_id = None  # SimVehicleSys not available

from .schemas import AGVInfo, AGVRuntime
from .schemas import SimSettingsPatch
from typing import Optional as _Opt


def _timestamp() -> str:
    # YYYY-MM-DDTHH:mm:ss.sssZ
    now = datetime.now(timezone.utc)
    return now.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


class MqttPublisher:
    def __init__(self, config_path: Path):
        self.config_path = config_path
        self.client: Optional[mqtt.Client] = None
        self._clients: list[mqtt.Client] = []
        # 初始化占位，真正值在 _load_config 中从集中设置读取
        self.host: str = ""
        self.port: int = 0
        self.vda_interface: str = ""
        self.vda_full_version: str = "2.0.0"
        self._brokers: list[tuple[str, int]] = []
        self._load_config()

    def _load_config(self) -> None:
        import os
        import yaml
        host = ""
        port = 0
        vda_interface = ""
        vda_full_version = ""
        if callable(get_config):
            try:
                cfg = get_config()
                host = str(cfg.mqtt_broker.host)
                port = int(str(cfg.mqtt_broker.port))
                vda_interface = str(cfg.mqtt_broker.vda_interface)
                vda_full_version = str(cfg.vehicle.vda_full_version)
            except Exception:
                host, port, vda_interface, vda_full_version = "", 0, "", ""
        
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
                            if "vda_full_version" in veh:
                                vda_full_version = str(veh["vda_full_version"])
            except Exception as e:
                print(f"MqttPublisher failed to load config from {self.config_path}: {e}")

        if not host:
            host = str(os.getenv("SIMAGV_MQTT_HOST", "127.0.0.1"))
        if not port:
            try:
                port = int(str(os.getenv("SIMAGV_MQTT_PORT", "1883")))
            except Exception:
                port = 1883
        if not vda_interface:
            vda_interface = str(os.getenv("SIMAGV_MQTT_VDA_INTERFACE", "uagv"))
        if not vda_full_version:
            vda_full_version = str(os.getenv("SIMAGV_VDA_FULL_VERSION", "2.0.0"))
        self.host = host
        self.port = port
        self.vda_interface = vda_interface
        self.vda_full_version = vda_full_version
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
        self._brokers = dedup

    def start(self) -> None:
        self._clients = []
        self.client = None
        for host, port in list(self._brokers):
            client = mqtt.Client(client_id=str(uuid.uuid4()), protocol=mqtt.MQTTv5)
            client.reconnect_delay_set(min_delay=1, max_delay=10)
            setattr(client, "_simagv_broker", f"{host}:{port}")
            try:
                client.connect(host, port, keepalive=60)
                client.loop_start()
                self._clients.append(client)
            except Exception as e:
                print(f"MqttPublisher connect failed ({host}:{port}): {e}")
                try:
                    client.loop_stop()
                except Exception:
                    pass
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

    def _publish_all(self, topic: str, payload: dict, *, qos: int, retain: bool) -> None:
        if not self._clients:
            raise RuntimeError("MQTT publisher not connected")
        last_err: Exception | None = None
        ok = False
        body = json.dumps(payload)
        for c in list(self._clients):
            try:
                info_obj = c.publish(topic, body, qos=qos, retain=retain)
                rc = getattr(info_obj, "rc", mqtt.MQTT_ERR_SUCCESS)
                if rc != mqtt.MQTT_ERR_SUCCESS:
                    raise RuntimeError(f"MQTT publish failed rc={rc}")
                ok = True
            except Exception as e:
                last_err = e
        if not ok:
            raise (last_err or RuntimeError("MQTT publish failed"))

    def publish_init_position(self, info: AGVInfo, rt: AGVRuntime) -> None:
        base = f"{self.vda_interface}/{info.vda_version}/{info.manufacturer}/{info.serial_number}"
        topic = f"{base}/instantActions"
        action_id = f"backend-move-{uuid.uuid4()}"
        # 尝试解析提交坐标所在地图的最近站点，并取其 points.name 作为 lastNodeId
        last_node_label: _Opt[str] = None
       
        # VDA5050 camelCase payload
        payload = {
            "headerId": 0,
            "timestamp": _timestamp(),
            "version": self.vda_full_version,
            "manufacturer": info.manufacturer,
            "serialNumber": info.serial_number,
            "actions": [
                {
                    "actionType": "initPosition",
                    "actionId": action_id,
                    "blockingType": "HARD",
                    "actionParameters": [
                        {"key": "x", "value": rt.position.x},
                        {"key": "y", "value": rt.position.y},
                        {"key": "theta", "value": rt.position.theta},
                        # mapId 仅为 VehicleMap 下的 scene 文件名（不含扩展名）
                        {"key": "mapId", "value": (canonicalize_map_id(getattr(rt, "current_map", None)) if callable(canonicalize_map_id) else (getattr(rt, "current_map", None) or ""))},
                        # 将 lastNodeId 作为额外参数下发，车辆在处理 initPosition 时会直接更新 state.last_node_id
                        *(([{"key": "lastNodeId", "value": last_node_label}] ) if last_node_label else [])
                    ],
                }
            ],
        }
        self._publish_all(topic, payload, qos=1, retain=True)

    def publish_start_station_navigation(self, info: AGVInfo, station_id: str, map_id: Optional[str]) -> None:
        base = f"{self.vda_interface}/{info.vda_version}/{info.manufacturer}/{info.serial_number}"
        topic = f"{base}/instantActions"
        action_id = f"backend-nav-{uuid.uuid4()}"
        payload = {
            "headerId": 0,
            "timestamp": _timestamp(),
            "version": self.vda_full_version,
            "manufacturer": info.manufacturer,
            "serialNumber": info.serial_number,
            "actions": [
                {
                    "actionType": "startStationNavigation",
                    "actionId": action_id,
                    "blockingType": "SOFT",
                    "actionParameters": [
                        {"key": "stationId", "value": station_id},
                        {"key": "mapId", "value": map_id or ""},
                    ],
                }
            ],
        }
        self._publish_all(topic, payload, qos=1, retain=False)

    def publish_order(self, info: AGVInfo, order_payload: dict) -> None:
        """发布 VDA5050 订单到 MQTT `.../order` 主题。

        `order_payload` 应为 camelCase 的 VDA 2.0 订单结构；本方法不转换字段命名。
        """
        base = f"{self.vda_interface}/{info.vda_version}/{info.manufacturer}/{info.serial_number}"
        topic = f"{base}/order"
        self._publish_all(topic, order_payload, qos=1, retain=False)

    def publish_equipment_actions(self, equipment: dict, actions_payload: dict) -> None:
        vda = str(equipment.get("vda_version", "v2"))
        manu = str(equipment.get("manufacturer", ""))
        serial = str(equipment.get("serial_number", ""))
        base = f"uequip/{vda}/{manu}/{serial}"
        topic = f"{base}/instantActions"
        self._publish_all(topic, actions_payload, qos=1, retain=False)

    def publish_instant_actions(self, info: AGVInfo, instant_actions_payload: dict) -> None:
        base = f"{self.vda_interface}/{info.vda_version}/{info.manufacturer}/{info.serial_number}"
        topic = f"{base}/instantActions"
        self._publish_all(topic, instant_actions_payload, qos=1, retain=False)

    def publish_translate(self, info: AGVInfo, *, dist_m: float, vx_mps: float, vy_mps: float) -> None:
        base = f"{self.vda_interface}/{info.vda_version}/{info.manufacturer}/{info.serial_number}"
        topic = f"{base}/instantActions"
        action_id = f"backend-translate-{uuid.uuid4()}"
        payload = {
            "headerId": 0,
            "timestamp": _timestamp(),
            "version": self.vda_full_version,
            "manufacturer": info.manufacturer,
            "serialNumber": info.serial_number,
            "actions": [
                {
                    "actionType": "translate",
                    "actionId": action_id,
                    "blockingType": "SOFT",
                    "actionParameters": [
                        {"key": "dist", "value": float(dist_m)},
                        {"key": "vx", "value": float(vx_mps)},
                        {"key": "vy", "value": float(vy_mps)},
                    ],
                }
            ],
        }
        self._publish_all(topic, payload, qos=1, retain=False)

    def publish_turn(self, info: AGVInfo, *, angle_rad: float, vw_radps: float) -> None:
        base = f"{self.vda_interface}/{info.vda_version}/{info.manufacturer}/{info.serial_number}"
        topic = f"{base}/instantActions"
        action_id = f"backend-turn-{uuid.uuid4()}"
        payload = {
            "headerId": 0,
            "timestamp": _timestamp(),
            "version": self.vda_full_version,
            "manufacturer": info.manufacturer,
            "serialNumber": info.serial_number,
            "actions": [
                {
                    "actionType": "turn",
                    "actionId": action_id,
                    "blockingType": "SOFT",
                    "actionParameters": [
                        {"key": "angle", "value": float(angle_rad)},
                        {"key": "vw", "value": float(vw_radps)},
                    ],
                }
            ],
        }
        self._publish_all(topic, payload, qos=1, retain=False)

    def publish_sim_settings(self, info: AGVInfo, patch: SimSettingsPatch | dict) -> None:
        """发布仿真设置热更新到 MQTT `.../simConfig` 主题。

        载荷为局部更新（仅包含需更新的键）。支持 snake_case（推荐）。
        """
        base = f"{self.vda_interface}/{info.vda_version}/{info.manufacturer}/{info.serial_number}"
        topic = f"{base}/simConfig"
        payload = patch if isinstance(patch, dict) else patch.dict(exclude_none=True)
        self._publish_all(topic, payload, qos=1, retain=True)

    def publish_switch_map(self, info: AGVInfo, *, map_name: str, switch_point: Optional[str] = None, center_x: Optional[float] = None, center_y: Optional[float] = None, initiate_angle: Optional[float] = None) -> None:
        """发布地图切换即时动作到 MQTT `.../instantActions` 主题。

        参数名与设备端约定保持一致：map、switchPoint、center_x、center_y、initiate_angle。
        """
        base = f"{self.vda_interface}/{info.vda_version}/{info.manufacturer}/{info.serial_number}"
        topic = f"{base}/instantActions"
        action_id = f"backend-switchmap-{uuid.uuid4()}"
        params = [{"key": "map", "value": map_name}]
        if switch_point:
            params.append({"key": "switchPoint", "value": switch_point})
        if center_x is not None:
            params.append({"key": "center_x", "value": center_x})
        if center_y is not None:
            params.append({"key": "center_y", "value": center_y})
        if initiate_angle is not None:
            params.append({"key": "initiate_angle", "value": initiate_angle})
        payload = {
            "headerId": 0,
            "timestamp": _timestamp(),
            "version": self.vda_full_version,
            "manufacturer": info.manufacturer,
            "serialNumber": info.serial_number,
            "actions": [
                {
                    "actionType": "switchMap",
                    "actionId": action_id,
                    "actionDescription": "Service switchMap",
                    "blockingType": "HARD",
                    "actionParameters": params,
                }
            ],
        }
        self._publish_all(topic, payload, qos=1, retain=False)
