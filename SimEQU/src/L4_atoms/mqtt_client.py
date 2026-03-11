from __future__ import annotations
import json
from typing import Any, Optional

import paho.mqtt.client as mqtt


def create_client() -> mqtt.Client:
    c = mqtt.Client(protocol=mqtt.MQTTv5)
    c.reconnect_delay_set(min_delay=1, max_delay=10)
    return c


def connect(client: mqtt.Client, host: str, port: int, keepalive: int = 60) -> None:
    client.connect(host, port, keepalive=keepalive)


def start_loop(client: mqtt.Client) -> None:
    client.loop_start()


def stop_loop(client: mqtt.Client) -> None:
    client.loop_stop()


def disconnect(client: mqtt.Client) -> None:
    client.disconnect()


def set_will(
    client: mqtt.Client,
    topic: str,
    payload: dict,
    qos: int = 1,
    retain: bool = False,
) -> None:
    will_payload = json.dumps(
        {
            "headerId": 999999999,
            "manufacturer": payload.get("manufacturer", ""),
            "serialNumber": payload.get("serial_number", ""),
            "timestamp": payload.get("timestamp", 0),
            "version": payload.get("version", ""),
            "connectionState": "CONNECTIONBROKEN",
        },
        ensure_ascii=False,
    )
    client.will_set(topic, payload=will_payload, qos=qos, retain=retain)


def set_on_message(client: mqtt.Client, callback) -> None:
    client.on_message = callback


def set_on_connect(client: mqtt.Client, callback) -> None:
    client.on_connect = callback


def publish(
    client: Optional[mqtt.Client],
    topic: str,
    payload: Any,
    qos: int = 1,
    retain: bool = False,
) -> None:
    if client is None:
        return
    if isinstance(payload, dict):
        payload = json.dumps(payload, ensure_ascii=False)
    client.publish(topic, payload=payload, qos=qos, retain=retain)


def subscribe(client: mqtt.Client, topic: str, qos: int = 1) -> None:
    client.subscribe(topic, qos=qos)
