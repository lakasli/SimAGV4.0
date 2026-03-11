from __future__ import annotations
import json
import threading
import time
from typing import Any, Optional

import paho.mqtt.client as mqtt

from ..L4_atoms import (
    config_loader,
    mqtt_client as mqtt_utils,
    timestamp,
    topic_builder,
)
from ..L4_atoms.logger import get_logger


logger = get_logger(__name__)


class EquipmentDevice:
    def __init__(self, cfg: config_loader.EquipmentConfig) -> None:
        self.cfg = cfg
        base = topic_builder.build_base_topic(
            cfg.mqtt.vda_interface,
            cfg.identity.vda_version,
            cfg.identity.manufacturer,
            cfg.identity.serial_number,
        )
        self.topic_connection = topic_builder.build_connection_topic(base)
        self.topic_state = topic_builder.build_state_topic(base)
        self.topic_factsheet = topic_builder.build_factsheet_topic(base)
        self.topic_actions = topic_builder.build_actions_topic(base)
        self.client: Optional[mqtt.Client] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = False
        self._header_id = 0

    def _build_connection(self, state: str) -> dict:
        return {
            "header_id": self._header_id,
            "timestamp": timestamp.get_timestamp(),
            "version": self.cfg.identity.vda_full_version,
            "manufacturer": self.cfg.identity.manufacturer,
            "serial_number": self.cfg.identity.serial_number,
            "connection_state": state,
        }

    def _build_base_state(self) -> dict:
        return {
            "header_id": self._header_id,
            "timestamp": timestamp.get_timestamp(),
            "version": self.cfg.identity.vda_full_version,
            "manufacturer": self.cfg.identity.manufacturer,
            "serial_number": self.cfg.identity.serial_number,
            "driving": False,
            "operating_mode": "MANUAL",
            "node_states": [],
            "edge_states": [],
            "last_node_id": str(self.cfg.settings.site[0] if self.cfg.settings.site else ""),
            "order_id": "",
            "order_update_id": 0,
            "last_node_sequence_id": 0,
            "action_states": [],
            "information": [],
            "loads": [],
            "battery_state": {"battery_charge": 100, "charging": False},
            "safety_state": {"e_stop": "NONE", "field_violation": False},
            "paused": False,
            "new_base_request": False,
            "agv_position": None,
            "velocity": None,
            "zone_set_id": str(self.cfg.settings.map_id or "default"),
            "waiting_for_interaction_zone_release": False,
        }

    def _build_factsheet(self, device_type: str) -> dict:
        return {
            "header_id": 0,
            "timestamp": timestamp.get_timestamp(),
            "version": self.cfg.identity.vda_full_version,
            "manufacturer": self.cfg.identity.manufacturer,
            "serial_number": self.cfg.identity.serial_number,
            "typeSpecification": {
                "seriesName": self.cfg.settings.name,
                "seriesDescription": self.cfg.settings.name,
                "agvKinematic": "STEER",
                "agvClass": device_type,
                "maxLoadMass": 0.0,
                "localizationTypes": ["SLAM"],
                "navigationTypes": ["VIRTUAL_LINE_GUIDED"],
            },
            "physicalParameters": {
                "speedMin": 0.0,
                "speedMax": 0.0,
                "accelerationMax": 0.0,
                "decelerationMax": 0.0,
                "heightMin": 0.0,
                "heightMax": 0.0,
                "width": 1.0,
                "length": 1.0,
            },
            "protocolLimits": None,
            "protocolFeatures": None,
            "agvGeometry": None,
            "loadSpecification": None,
            "localizationParameters": None,
        }

    def _create_client(self) -> mqtt.Client:
        c = mqtt_utils.create_client()
        mqtt_utils.set_will(
            c,
            self.topic_connection,
            {
                "manufacturer": self.cfg.identity.manufacturer,
                "serial_number": self.cfg.identity.serial_number,
                "timestamp": timestamp.get_timestamp(),
                "version": self.cfg.identity.vda_full_version,
            },
            qos=1,
            retain=False,
        )
        mqtt_utils.set_on_message(c, self._on_message)
        return c

    def _connect(self) -> None:
        self.client = self._create_client()
        mqtt_utils.connect(self.client, self.cfg.mqtt.host, int(self.cfg.mqtt.port), keepalive=60)
        mqtt_utils.start_loop(self.client)
        mqtt_utils.subscribe(self.client, self.topic_actions, qos=1)

    def _publish_connection_online(self) -> None:
        payload = self._build_connection("ONLINE")
        mqtt_utils.publish(self.client, self.topic_connection, payload, qos=1, retain=False)

    def _publish_factsheet(self, device_type: str) -> None:
        payload = self._build_factsheet(device_type)
        mqtt_utils.publish(self.client, self.topic_factsheet, payload, qos=1, retain=True)

    def _publish_state(self, state_payload: dict) -> None:
        self._header_id += 1
        state_payload["header_id"] = self._header_id
        state_payload["timestamp"] = timestamp.get_timestamp()
        mqtt_utils.publish(self.client, self.topic_state, state_payload, qos=1, retain=False)

    def _on_message(self, client: mqtt.Client, userdata: Any, msg: mqtt.MQTTMessage) -> None:
        try:
            data = json.loads(msg.payload.decode("utf-8", errors="ignore"))
        except Exception:
            return
        self._handle_instant_actions(data)

    def _handle_instant_actions(self, data: dict) -> None:
        pass

    def start(self) -> None:
        self._connect()
        self._header_id = 0
        self._publish_connection_online()
        self._start_thread()

    def stop(self) -> None:
        self._stop = True
        try:
            if self._thread and self._thread.is_alive():
                self._thread.join(timeout=1.0)
        except Exception:
            pass
        try:
            if self.client:
                mqtt_utils.stop_loop(self.client)
                mqtt_utils.disconnect(self.client)
        except Exception:
            pass

    def _start_thread(self) -> None:
        self._stop = False
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()

    def _run_loop(self) -> None:
        while not self._stop:
            time.sleep(1.0)
