import time
import paho.mqtt.client as mqtt
from huskylib import HuskyLensLibrary

BROKER = "127.0.0.1"
USERNAME = "pico"
PASSWORD = "123mqtt456b"
FACE_TOPIC = "smarthome/pico/security/face_event"

KNOWN_FACE_ID = 1

PORT = "/dev/ttyUSB0"

client = mqtt.Client("HuskyLensPi")
client.username_pw_set(USERNAME, PASSWORD)
client.connect(BROKER, 1883, 60)
client.loop_start()

hl = HuskyLensLibrary("SERIAL", PORT)
time.sleep(2)
print("Knock test:", hl.knock())

hl.algorthim("ALGORITHM_FACE_RECOGNITION")
print("Face recognition включён")

last_print = 0
while True:
    results = hl.requestAll()
    current = False
    for res in results:
        if hasattr(res, 'ID') and res.ID == KNOWN_FACE_ID:
            current = True
            break
    if current and (time.time() - last_print > 3):
        print("Known face detected!")
        last_print = time.time()
        client.publish(FACE_TOPIC, "Known face detected!", retain=True)
    time.sleep(0.2)
