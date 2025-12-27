import paho.mqtt.client as mqtt
import mysql.connector
import time

MQTT_BROKER = "127.0.0.1"
MQTT_USER = "pico"
MQTT_PASS = "123mqtt456b"
BASE_TOPIC = "smarthome/pico/"

DB_CONFIG = {
    "host": "localhost",
    "user": "smarthome",
    "password": "123root456maria",
    "database": "smarthome"
}

db = mysql.connector.connect(**DB_CONFIG)
cursor = db.cursor()

env_data = {"temperature": None, "humidity": None, "pressure": None}
last_env_time = 0
ENV_WINDOW = 10

def on_message(client, userdata, msg):
    global env_data, last_env_time
    topic = msg.topic
    payload = msg.payload.decode()
    print(f"{topic} → {payload}")

    try:
        if topic.startswith(BASE_TOPIC + "environment/"):
            field = topic.split("/")[-1]
            value = float(payload) if field != "pressure" else int(payload)
            env_data[field] = value
            last_env_time = time.time()

            if all(v is not None for v in env_data.values()):
                if time.time() - last_env_time < ENV_WINDOW:
                    cursor.execute("""
                        INSERT INTO sensors (temperature, humidity, pressure)
                        VALUES (%s, %s, %s)
                    """, (env_data["temperature"], env_data["humidity"], env_data["pressure"]))
                    db.commit()
                env_data = {"temperature": None, "humidity": None, "pressure": None}

        elif topic.startswith(BASE_TOPIC + "security/"):
            field = topic.split("/")[-1]
            mapping = {"motion": "motion", "door": "door", "face": "face", "lock": "lock_status"}
            db_field = mapping.get(field)
            if db_field:
                cursor.execute(f"INSERT INTO security_status ({db_field}) VALUES (%s)", (payload,))
                db.commit()

    except Exception as e:
        print(f"Error: {e}")

client = mqtt.Client(client_id="mqtt2db", protocol=mqtt.MQTTv5)
client.username_pw_set(MQTT_USER, MQTT_PASS)
client.on_message = on_message
client.connect(MQTT_BROKER, 1883)
client.subscribe("smarthome/pico/#")
print("MQTT → DB started")
client.loop_forever()
