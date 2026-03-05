import paho.mqtt.client as mqtt
import mysql.connector
import logging
import firebase_admin
from firebase_admin import credentials, messaging

# ------------------ Settings ------------------
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

# Path to Firebase service account key
FIREBASE_CRED_PATH = '/home/hass/smarthomeapp-elias-firebase-adminsdk-fbsvc-d5527a5fbc.json'

# ------------------ Initialization ------------------
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger(__name__)

try:
    db = mysql.connector.connect(**DB_CONFIG)
    cursor = db.cursor()
    logger.info("DB connected")
except mysql.connector.Error as err:
    logger.error(f"DB connection failed: {err}")
    exit(1)

try:
    cred = credentials.Certificate(FIREBASE_CRED_PATH)
    firebase_admin.initialize_app(cred)
    logger.info("Firebase Admin initialized")
except Exception as e:
    logger.error(f"Firebase init failed: {e}")
    exit(1)

env = {"temperature": None, "humidity": None, "pressure": None}

# ------------------ Helper functions ------------------
def log_event(event_type, message):
    """Log event and return its ID (for push data)"""
    try:
        cursor.execute(
            "INSERT INTO events (type, message, created_at) VALUES (%s, %s, NOW())",
            (event_type, message)
        )
        db.commit()
        cursor.execute("SELECT LAST_INSERT_ID()")
        return cursor.fetchone()[0]
    except Exception as e:
        logger.error(f"Log event failed: {e}")
        return None

def send_broadcast_push(title, body, data=None):
    """Send push to ALL registered devices (broadcast to all logged-in users)"""
    try:
        cursor.execute("SELECT DISTINCT fcm_token FROM push_tokens WHERE fcm_token IS NOT NULL")
        tokens = [row[0] for row in cursor.fetchall()]

        if not tokens:
            logger.warning("No active FCM tokens found")
            return

        message = messaging.MulticastMessage(
            notification=messaging.Notification(title=title, body=body),
            tokens=tokens,
            data=data or {}
        )

        response = messaging.send_each_for_multicast(message)
        logger.info(f"Broadcast push → {response.success_count} success / {response.failure_count} failed")

        if response.failure_count > 0:
            for idx, resp in enumerate(response.responses):
                if not resp.success:
                    logger.warning(f"Failed token {tokens[idx][:20]}...: {resp.exception}")

    except Exception as e:
        logger.error(f"Broadcast push failed: {e}")

# ------------------ MQTT callbacks ------------------
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        logger.info("Connected to MQTT broker")
        log_event("system", "MQTT → DB bridge started")
        client.subscribe(BASE_TOPIC + "#")
    else:
        logger.error(f"MQTT connection failed, rc={rc}")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode(errors='ignore').strip()
    if not payload:
        return

    logger.info(f"→ {topic}: {payload}")
    rel_topic = topic[len(BASE_TOPIC):].lower()
    payload_upper = payload.upper()

    event_id = None

    try:
        # Environment sensors
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
            env["temperature"] = env["humidity"] = env["pressure"] = None

        # Motion detection
        elif rel_topic == "security/motion":
            cursor.execute("UPDATE security_status SET motion = %s WHERE id = 1", (payload,))
            db.commit()
            if payload_upper == "DETECTED":
                event_id = log_event("alert", "Motion detected!")
                send_broadcast_push(
                    title="Motion Detected!",
                    body="Movement detected in monitored area",
                    data={"type": "motion", "event_id": str(event_id) if event_id else ""}
                )

        # Door status
        elif rel_topic == "security/door":
            cursor.execute("UPDATE security_status SET door = %s WHERE id = 1", (payload.lower(),))
            db.commit()
            if "OPEN" in payload_upper:
                event_id = log_event("alert", "Door opened!")
                send_broadcast_push(
                    title="Door Opened!",
                    body="Front door has been opened",
                    data={"type": "door", "event_id": str(event_id) if event_id else ""}
                )
            else:
                event_id = log_event("alert", "Door closed!")
                send_broadcast_push(
                    title="Door Closed",
                    body="Door returned to closed state",
                    data={"type": "door", "event_id": str(event_id) if event_id else ""}
                )

        # Face recognition
        elif rel_topic == "security/face":
            cursor.execute("UPDATE security_status SET face = %s WHERE id = 1", (payload,))
            db.commit()
            if payload_upper == "AUTHORIZED":
                event_id = log_event("access", "Face authorized")
                send_broadcast_push(
                    title="Access Granted",
                    body="Face recognized — welcome!",
                    data={"type": "face", "event_id": str(event_id) if event_id else ""}
                )

        # Lock status
        elif rel_topic == "security/lock":
            cursor.execute("UPDATE security_status SET lock_status = %s WHERE id = 1", (payload,))
            db.commit()

        # Access log (RFID + Face)
        elif rel_topic == "access/log":
            cursor.execute(
                "INSERT INTO access_logs (method, result, source, created_at) VALUES (%s, %s, %s, NOW())",
                ("RFID+Face", payload, "pi")
            )
            db.commit()
            log_event("access", payload)

        # Errors and system events
        elif rel_topic in ("error", "security/face_event", "security/lock_event"):
            log_event("system" if "error" in rel_topic else "detection", payload)

        # Control commands
        elif rel_topic == "control/door":
            log_event("control", f"Door command: {payload}")

    except Exception as e:
        log_event("error", f"MQTT processing error: {str(e)}")
        logger.error(f"Processing error: {e}")

# ------------------ Start MQTT client ------------------
client = mqtt.Client(client_id="mqtt_to_db_bridge")
client.username_pw_set(MQTT_USER, MQTT_PASS)
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(MQTT_BROKER, MQTT_PORT)
    client.loop_forever()
except KeyboardInterrupt:
    logger.info("Stopped by user")
except Exception as e:
    logger.error(f"Connection error: {e}")
finally:
    cursor.close()
    db.close()
    logger.info("Resources closed")