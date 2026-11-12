#!/usr/bin/env python3
import json
import os
import ssl
import threading
import urllib.request

import paho.mqtt.client as mqtt

with open("/home/elaina/.config/xiaozhi.cfg", encoding="utf-8") as config_file:
    config = json.load(config_file)
interface = next(name for name in os.listdir("/sys/class/net") if name.startswith(("wlan", "wlp")))
with open("/sys/class/net/" + interface + "/address", encoding="ascii") as address_file:
    device_id = address_file.read().strip()
body = json.dumps({"uuid": config["uuid"], "application": {"name": "xiaozhi_linux_k230", "version": "1.0.0"},
                   "ota": {}, "board": {"type": "k230_linux_board", "name": "k230_linux_board"}}).encode()
request = urllib.request.Request(config["xiaozhi"]["xiaozhi_ota_url"], data=body,
                                 headers={"Content-Type": "application/json", "Device-Id": device_id,
                                          "Client-Id": config["uuid"], "Activation-Version": "1",
                                          "User-Agent": "canaan", "Accept-Language": "zh-CN"})
with urllib.request.urlopen(request, timeout=20) as response:
    settings = json.load(response)["mqtt"]
host, separator, port = settings["endpoint"].partition(":")
connected = threading.Event()
result = {"ok": False}

def on_connect(client, userdata, flags, reason_code, properties):
    print("MQTT connection result:", reason_code, flush=True)
    if not reason_code.is_failure:
        result["ok"] = True
        client.subscribe(settings["subscribe_topic"], qos=0)
    connected.set()

def on_disconnect(client, userdata, flags, reason_code, properties):
    print("MQTT disconnected:", reason_code, flush=True)

def on_log(client, userdata, level, message):
    if message.startswith(("Sending CONNECT", "Received CONNACK", "Sending PINGREQ", "Received PINGRESP")):
        print("MQTT event:", message.split(" (", 1)[0], flush=True)

version = mqtt.MQTTv31 if os.environ.get("XIAOZHI_MQTT_V31") else mqtt.MQTTv311
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=settings["client_id"], protocol=version)
client.username_pw_set(settings["username"], settings["password"])
client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_log = on_log
client.connect(host, int(port) if separator else 8883, keepalive=240)
client.loop_start()
if not connected.wait(15) or not result["ok"]:
    client.loop_stop()
    raise SystemExit("MQTT login failed")
print("MQTT authenticated; keeping the diagnostic connection open for 30 seconds", flush=True)
threading.Event().wait(30)
client.disconnect()
client.loop_stop()
print("MQTT diagnostic completed", flush=True)
