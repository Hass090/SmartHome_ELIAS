import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion
import mysql.connector

# ================= CONFIG =================
MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
MQTT_USER = "pico"
MQTT_PASS = "123mqtt456b"
BASE_TOPIC = "smarthome/pico/"

DB_CONFIG = {
    "host": "localhost",
    "user": "smarthome",
    "password": "123root456maria",
    "database": "smarthome_db",
    "autocommit": True
}

# Connection to DB
try:
    db = mysql.connector.connect(**DB_CONFIG)
    cursor = db.cursor()
    print("DB connected")
except mysql.connector.Error as err:
    print(f"DB connection failed: {err}")
    exit(1)

env = {"temperature": None, "humidity": None, "pressure": None}

def log_event(event_type, message):
    try:
        cursor.execute("INSERT INTO events (type, message) VALUES (%s, %s)", (event_type, message))
        db.commit()
    except Exception as e:
        print(f"Log event failed: {e}")

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("Connected to MQTT")
        log_event("system", "MQTT to DB restarted")
        client.subscribe(BASE_TOPIC + "#")
        print("Subscribed to " + BASE_TOPIC + "#")
    else:
        print(f"MQTT connect failed, rc={rc}")

def on_message(client, userdata, msg):
    print(f"Received message on topic: {msg.topic} with payload: {msg.payload.decode(errors='ignore').strip()}")

    topic = msg.topic
    payload = msg.payload.decode(errors='ignore').strip()
    if not payload:
        return
    rel = topic[len(BASE_TOPIC):]
    try:
        # === ENVIRONMENT ===
        if rel == "environment/temperature":
            env["temperature"] = float(payload)
        elif rel == "environment/humidity":
            env["humidity"] = float(payload)
        elif rel == "environment/pressure":
            env["pressure"] = float(payload)
        if rel == "environment/pressure" and all(v is not None for v in env.values()):
            cursor.execute(
                "INSERT INTO sensors (temperature, humidity, pressure) VALUES (%s, %s, %s)",
                (env["temperature"], env["humidity"], env["pressure"])
            )
            db.commit()
            env.update({k: None for k in env})

        # === SECURITY ===
        elif rel == "security/motion":
            cursor.execute("UPDATE security_status SET motion = %s WHERE id = 1", (payload,))
            db.commit()
            if payload == "DETECTED":
                log_event("alert", "Motion detected!")
        elif rel == "security/door":
            cursor.execute("UPDATE security_status SET door = %s WHERE id = 1", (payload,))
            db.commit()
            if "OPEN" in payload.upper():
                log_event("alert", "Door/window opened!")
        elif rel == "security/face":
            cursor.execute("UPDATE security_status SET face = %s WHERE id = 1", (payload,))
            db.commit()
        elif rel == "security/lock":
            cursor.execute("UPDATE security_status SET lock_status = %s WHERE id = 1", (payload,))
            db.commit()

        # === ACCESS LOGS ===
        elif rel == "access/log":
            cursor.execute(
                "INSERT INTO access_logs (method, result, source) VALUES (%s, %s, %s)",
                ("RFID+Face", payload, "pico")
            )
            db.commit()
            log_event("access", payload)

        # === ERRORS ===
        elif rel == "error":
            log_event("system_error", payload)
        elif rel == "security/face_event":
            log_event("detection", payload)
        elif rel == "security/lock_event":
            log_event("system", payload)

    except Exception as e:
        log_event("error", f"MQTT error: {str(e)}")
        print(f"Error: {e}")

client = mqtt.Client(CallbackAPIVersion.VERSION2, client_id="mqtt2db")
client.username_pw_set(MQTT_USER, MQTT_PASS)
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(MQTT_BROKER, MQTT_PORT)
    client.loop_forever()
except KeyboardInterrupt:
    print("\nStopped by user")
finally:
    cursor.close()
    db.close()

