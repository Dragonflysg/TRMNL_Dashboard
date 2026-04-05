"""
Publish a message to the TRMNL e-ink dashboard via MQTT.

Install dependency:
    pip install paho-mqtt

Usage:
    python publish_dashboard.py
"""

import json
import paho.mqtt.client as mqtt

# ============================================================
# CONFIGURATION - Edit these to match your setup
# ============================================================

MQTT_BROKER = "10.0.0.246"   # <-- your Raspberry Pi's IP
MQTT_PORT   = 1883
MQTT_TOPIC  = "dashboard/epaper"

# Optional auth (leave as None if no auth)
MQTT_USER = None
MQTT_PASS = None

# ============================================================
# DASHBOARD MESSAGE
# ============================================================

message = {
    "headline": "Word of the Day : Ambivalent",
    "body": "1st line\nsecondline\nthirdline\n4thline\n5thline\n6th line\n7th line\n8th line\n9thline\n10thline\n11thline\n12thline",
    "footer": "Updated: 6:46 PM"   # optional, remove this line if not needed
}

# ============================================================
# PUBLISH
# ============================================================

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

if MQTT_USER:
    client.username_pw_set(MQTT_USER, MQTT_PASS)

client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)

payload = json.dumps(message)
result = client.publish(MQTT_TOPIC, payload)
result.wait_for_publish()

print(f"Published to {MQTT_TOPIC}:")
print(json.dumps(message, indent=2))

client.disconnect()