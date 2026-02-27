import paho.mqtt.client as mqtt
import mysql.connector
import time

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
        cursor.execute("INSERT INTO events (type, message, created_at) VALUES (%s, %s, NOW())",
                       (event_type, message))
        db.commit()
    except Exception as e:
        print(f"Log event failed: {e}")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT")
        log_event("system", "MQTT to DB started")
        client.subscribe(BASE_TOPIC + "#")
    else:
        print(f"MQTT connect failed, rc={rc}")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode(errors='ignore').strip()
    if not payload:
        return
    print(f"→ {topic}: {payload}")
    rel_topic = topic[len(BASE_TOPIC):]
    try:
        if rel_topic == "environment/temperature":
            env["temperature"] = float(payload)
        elif rel_topic == "environment/humidity":
            env["humidity"] = float(payload)
        elif rel_topic == "environment/pressure":
            env["pressure"] = float(payload)
            if all(v is not None for v in env.values()):
                cursor.execute(
                    "INSERT INTO sensors (temperature, humidity, pressure, created_at) VALUES (%s, %s, %s, NOW())",
                    (env["temperature"], env["humidity"], env["pressure"])
                )
                db.commit()
                env["temperature"] = None
                env["humidity"] = None
                env["pressure"] = None
        elif rel_topic == "security/motion":
            cursor.execute("UPDATE security_status SET motion = %s WHERE id = 1", (payload,))
            db.commit()
            if payload == "DETECTED":
                log_event("alert", "Motion detected!")
        elif rel_topic == "security/door":
            cursor.execute("UPDATE security_status SET door = %s WHERE id = 1", (payload,))
            db.commit()
            if "OPEN" in payload.upper():
                log_event("alert", "Door opened!")
        elif rel_topic == "security/face":
            cursor.execute("UPDATE security_status SET face = %s WHERE id = 1", (payload,))
            db.commit()
            if payload == "Authorized":
                log_event("access", "Face authorized")
        elif rel_topic == "security/lock":
            cursor.execute("UPDATE security_status SET lock_status = %s WHERE id = 1", (payload,))
            db.commit()
        elif rel_topic == "access/log":
            cursor.execute(
                "INSERT INTO access_logs (method, result, source, created_at) VALUES (%s, %s, %s, NOW())",
                ("RFID+Face", payload, "pi")
            )
            db.commit()
            log_event("access", payload)
        elif rel_topic in ("error", "security/face_event", "security/lock_event"):
            log_event("system" if "error" in rel_topic else "detection", payload)
    except Exception as e:
        log_event("error", f"MQTT processing error: {str(e)}")
        print("Error:", e)

client = mqtt.Client(client_id="mqtt_to_db")
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