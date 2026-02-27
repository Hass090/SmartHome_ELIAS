#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <cmath>
using std::abs;

#define MOTION_DETECTED "DETECTED"
#define MOTION_QUIET "quiet"
#define DOOR_OPEN "OPEN"
#define DOOR_CLOSED "closed"
#define FACE_AUTH "Authorized"
#define FACE_NONE "none"
#define LOCK_OPEN "Unlocked"
#define LOCK_CLOSED "locked"
#define ACCESS_GRANTED "GRANTED"
#define ACCESS_DENIED_NO_FACE "DENIED: No face"
#define ACCESS_DENIED_UNKNOWN "DENIED: Unknown RFID"

const char *ssid = "Clickfiel 2.4GHz";
const char *password = "clickfiel2001";
const char *mqtt_server = "192.168.1.145";

WiFiClient wificlient;
PubSubClient client(wificlient);

/* ===== BME ===== */
float temp = 0.0;
float hum = 0.0;
float press = 0.0;
static const uint32_t ENV_UPDATE_TIME = 15000;
uint32_t lastEnvPublish = 0;

/* ===== MQTT ===== */
const char *mqtt_username = "pico";
const char *mqtt_password = "123mqtt456b";
#define BASE_TOPIC "smarthome/pico/"
#define TOPIC_TEMP BASE_TOPIC "environment/temperature"
#define TOPIC_HUM BASE_TOPIC "environment/humidity"
#define TOPIC_PRESS BASE_TOPIC "environment/pressure"
#define TOPIC_MOTION BASE_TOPIC "security/motion"
#define TOPIC_DOOR BASE_TOPIC "security/door"
#define TOPIC_FACE BASE_TOPIC "security/face"
#define TOPIC_LOCK BASE_TOPIC "security/lock"
#define TOPIC_STATUS BASE_TOPIC "status/connection"
#define TOPIC_ACCESS BASE_TOPIC "access/log"
#define TOPIC_CONTROL_DOOR BASE_TOPIC "control/door"
#define TOPIC_FACE_EVENT BASE_TOPIC "security/face_event"
#define TOPIC_ERROR BASE_TOPIC "error"
// Heartbeat to keep MQTT alive and confirm status
#define HEARTBEAT_TIME 20000
uint32_t lastHeartbeat = 0;

/* ===== OLED ===== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
/* ===== I2C ===== */
#define I2C_SDA 12
#define I2C_SCL 13
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BME280 bme;

/* ===== BUZZER ===== */
#define BUZZER_PIN 15
#define BUZZER_DUR 500
#define BUZZER_FREQ 1000
uint32_t buzzerOffTime = 0;
bool buzzerActive = 0;

/* ===== LEDs ===== */
#define B_LED_PIN 21
#define Y_LED_PIN 20
#define TEMP_HOT 24.0 // °C

/* ===== REED ===== */
#define REED_PIN 9
bool doorOpen = false;
uint32_t lastDoorTime = 0;
static bool lastReedState = HIGH;
static uint32_t lastReedChange = 0;
const uint32_t REED_DEBOUNCE = 1000;
static bool lastDoorPublished = false;

/* ===== RFID ===== */
#define RFID_SS_PIN 5
#define RFID_RST_PIN 22
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);
byte authorizedUID[] = {0x43, 0x1E, 0x8D, 0x97};

/* ===== SERVOMOTOR ===== */
#define SERVO_PIN 18
#define LOCK_OPEN_A 90
#define LOCK_CLOSE_A 0
#define OPEN_DUR 10000
Servo doorServo;
uint32_t lockOpenTime = 0;
bool lockIsOpen = false;
static bool lastLockPublished = false;

/* ===== HUSKYLENS ===== */
uint32_t lastFaceTime = 0;
const uint32_t FACE_WINDOW = 3000;
bool faceAuthorized = false;
static bool lastFacePublished = false; // добавлено для публикации состояния лица

/* ===== PIR ===== */
#define PIR_PIN 19
static const uint32_t PIR_CALIBRATION_TIME = 60000;
static const uint32_t PIR_IRQ_DEBOUNCE = 100;
static bool lastMotionPublished = false;

/* ===== Update timing ===== */
static const uint32_t SENSOR_UPDATE_TIME = 2000;

/* ===== PIR ISR state ===== */
volatile bool motionIRQ = false;
volatile uint32_t lastIRQTime = 0;

/* ===== App state ===== */
bool motionDetected = false;
uint32_t lastMotionTime = 0;
uint32_t lastSensorUpdate = 0;
String systemStatus = "OK";

/* ===== PIR interrupt ===== */
void pirISR()
{
  uint32_t now = millis();
  if (now - lastIRQTime > PIR_IRQ_DEBOUNCE)
  {
    motionIRQ = true;
    lastIRQTime = now;
  }
}

/* ==== Checks if the UID of the read card matches the authorized one ===== */
bool checkUID()
{
  if (mfrc522.uid.size == sizeof(authorizedUID))
  {
    return memcmp(mfrc522.uid.uidByte, authorizedUID, sizeof(authorizedUID)) == 0;
  }
  return false;
}

/* ==== Reconnecting to MQTT if the connection is lost ===== */
void reconnect()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  Serial.print("\nMQTT reconnect... ");
  unsigned long start = millis();
  if (client.connect("PicoClient", mqtt_username, mqtt_password, TOPIC_STATUS, 1, true, "offline"))
  {
    Serial.println("OK");
    client.publish(TOPIC_STATUS, "online", true);
    client.subscribe(TOPIC_CONTROL_DOOR);
    client.subscribe(TOPIC_FACE_EVENT);
    Serial.println("Subscribed to: " + String(TOPIC_CONTROL_DOOR) + " and " + String(TOPIC_FACE_EVENT));
  }
  else
  {
    if (millis() - start > 30000)
    {
      Serial.println("Timeout");
      systemStatus = "MQTT Timeout";
    }
    else
    {
      Serial.print("Failed: ");
      Serial.println(client.state());
      systemStatus = "MQTT Failed";
    }
  }
}

void handleWiFi()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    static uint32_t lastWiFiRetry = 0;
    if (millis() - lastWiFiRetry > 10000)
    {
      lastWiFiRetry = millis();
      Serial.println("WiFi lost - reconnecting...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      systemStatus = "WiFi Reconnecting";
    }
  }
}

void handleMQTT()
{
  client.loop();
  if (!client.connected())
  {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 5000)
    {
      lastRetry = millis();
      reconnect();
    }
  }
  else
  {
    // Heartbeat
    if (millis() - lastHeartbeat > HEARTBEAT_TIME)
    {
      lastHeartbeat = millis();
      client.publish(TOPIC_STATUS, "online", true);
    }
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  String message = "";
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }
  Serial.print("MQTT callback - topic: ");
  Serial.print(topic);
  Serial.print(" | message: ");
  Serial.println(message);

  String topicStr = String(topic);

  if (topicStr == TOPIC_CONTROL_DOOR)
  {
    if (message == "OPEN")
    {
      Serial.println("Open the door");
      doorServo.write(LOCK_OPEN_A);
      lockIsOpen = true;
      lockOpenTime = millis();
      client.publish(TOPIC_DOOR, DOOR_OPEN, true);
    }
    else if (message == "CLOSED" || message == "close")
    {
      Serial.println("Close the door");
      doorServo.write(LOCK_CLOSE_A);
      lockIsOpen = false;
      client.publish(TOPIC_DOOR, DOOR_CLOSED, true);
    }
    else
    {
      Serial.println("Unknown command: " + message);
    }
  }
  else if (topicStr == TOPIC_FACE_EVENT)
  {
    if (message == "Known face detected!")
    {
      faceAuthorized = true;
      lastFaceTime = millis();
      Serial.println("Face authorized from Pi5!");
    }
  }
}

void handleEnvironment()
{
  if (client.connected() && millis() - lastEnvPublish >= ENV_UPDATE_TIME)
  {
    lastEnvPublish = millis();
    float newTemp = bme.readTemperature();
    float newHum = bme.readHumidity();
    float newPress = bme.readPressure() / 100.0F;
    // Only ship if the change is more than the threshold.
    if (abs(newTemp - temp) > 0.3f || abs(newHum - hum) > 1.0f || abs(newPress - press) > 2.0f)
    {
      temp = newTemp;
      hum = newHum;
      press = newPress;
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "%.1f", temp);
      client.publish(TOPIC_TEMP, buffer, true);
      snprintf(buffer, sizeof(buffer), "%.1f", hum);
      client.publish(TOPIC_HUM, buffer, true);
      snprintf(buffer, sizeof(buffer), "%.0f", press);
      client.publish(TOPIC_PRESS, buffer, true);
    }
  }
}

void handleSecurity()
{
  // Publish security events only on change
  if (client.connected())
  {
    if (motionDetected != lastMotionPublished)
    {
      client.publish(TOPIC_MOTION, motionDetected ? MOTION_DETECTED : MOTION_QUIET, true);
      lastMotionPublished = motionDetected;
    }
    if (doorOpen != lastDoorPublished)
    {
      client.publish(TOPIC_DOOR, doorOpen ? DOOR_OPEN : DOOR_CLOSED, true);
      lastDoorPublished = doorOpen;
    }
    if (faceAuthorized != lastFacePublished)
    {
      client.publish(TOPIC_FACE, faceAuthorized ? FACE_AUTH : FACE_NONE, true);
      lastFacePublished = faceAuthorized;
    }
    if (lockIsOpen != lastLockPublished)
    {
      client.publish(TOPIC_LOCK, lockIsOpen ? LOCK_OPEN : LOCK_CLOSED, true);
      lastLockPublished = lockIsOpen;
    }
  }
  /* Handle PIR event + buzzer + blue LED */
  if (motionIRQ)
  {
    motionIRQ = false;
    motionDetected = true;
    lastMotionTime = millis();
    /* Turn on the buzzer and blue LED */
    digitalWrite(B_LED_PIN, HIGH);
    tone(BUZZER_PIN, BUZZER_FREQ);
    buzzerActive = true;
    buzzerOffTime = millis() + BUZZER_DUR;
  }
  /* Turn off the buzzer and blue LED */
  if (buzzerActive && millis() >= buzzerOffTime)
  {
    noTone(BUZZER_PIN);
    digitalWrite(B_LED_PIN, LOW);
    buzzerActive = false;
  }
  /* Resetting the movement indication after 2 sec */
  if (motionDetected && millis() - lastMotionTime > 2000)
  {
    motionDetected = false;
  }
  /* reed + buzzer */
  int reedState = digitalRead(REED_PIN);
  if (reedState != lastReedState)
  {
    lastReedChange = millis();
  }
  if (millis() - lastReedChange > REED_DEBOUNCE)
  {
    bool newDoorOpen = (reedState == LOW);
    if (newDoorOpen != doorOpen)
    {
      doorOpen = newDoorOpen;
      if (client.connected())
      {
        client.publish(TOPIC_DOOR, doorOpen ? DOOR_OPEN : DOOR_CLOSED);
        lastDoorPublished = doorOpen;
      }
      if (doorOpen)
      {
        tone(BUZZER_PIN, BUZZER_FREQ);
        buzzerActive = true;
        buzzerOffTime = millis() + BUZZER_DUR * 2;
      }
    }
  }
  lastReedState = reedState;
  // Checks if a new card has been presented to the reader
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
  {
    // Prints the card's UID to the Serial Monitor
    Serial.print("Card UID: ");
    for (byte i = 0; i < mfrc522.uid.size; i++)
    {
      Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println();
    // Check if this is an authorized card
    if (checkUID())
    {
      // Check if there was a face
      if (faceAuthorized && (millis() - lastFaceTime < FACE_WINDOW))
      {
        Serial.println("ACCESS GRANTED");
        doorServo.write(LOCK_OPEN_A);
        lockIsOpen = true;
        lockOpenTime = millis();
        // Pleasant audio feedback
        tone(BUZZER_PIN, 1500, 200);
        delay(200);
        tone(BUZZER_PIN, 2000, 200);
        delay(200);
        tone(BUZZER_PIN, 1500, 200);
        // Publish to MQTT for DB logging
        if (client.connected())
          client.publish(TOPIC_ACCESS, ACCESS_GRANTED, true);
      }
      else
      {
        Serial.println("RFID OK but no recent face - ACCESS DENIED");
        tone(BUZZER_PIN, 500, 800);
        // Publish denial reason
        if (client.connected())
          client.publish(TOPIC_ACCESS, ACCESS_DENIED_NO_FACE, true);
      }
    }
    else
    {
      Serial.println("Unknown card - ACCESS DENIED");
      tone(BUZZER_PIN, 300, 1000);
      // Publish denial reason
      if (client.connected())
        client.publish(TOPIC_ACCESS, ACCESS_DENIED_UNKNOWN, true);
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
  // Automatically close the lock after OPEN_DUR
  if (lockIsOpen && millis() - lockOpenTime >= OPEN_DUR)
  {
    doorServo.write(LOCK_CLOSE_A);
    lockIsOpen = false;
    Serial.println("Lock closed automatically");
    if (client.connected())
    {
      client.publish(BASE_TOPIC "security/lock_event", "Lock closed automatically", true);
    }
  }
  // Face timeout - reset authorization after window
  if (faceAuthorized && millis() - lastFaceTime > FACE_WINDOW)
  {
    faceAuthorized = false;
  }
}

void handleDisplay()
{
  if (millis() - lastSensorUpdate >= SENSOR_UPDATE_TIME)
  {
    lastSensorUpdate = millis();
    temp = bme.readTemperature();
    hum = bme.readHumidity();
    press = bme.readPressure() / 100.0F;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SmartHome_ELIAS");
    display.setCursor(0, 8);
    display.printf("Temp: %.1f C\n", temp);
    display.setCursor(0, 16);
    display.printf("Hum: %.1f %%\n", hum);
    display.setCursor(0, 24);
    display.printf("Press: %.0f hPa\n", press);
    display.setCursor(0, 32);
    display.printf("Movements: %s\n", motionDetected ? MOTION_DETECTED "!" : MOTION_QUIET);
    display.setCursor(0, 40);
    display.printf("Door: %s\n", doorOpen ? DOOR_OPEN "!" : DOOR_CLOSED);
    display.setCursor(0, 48);
    display.printf("FaceID: %s\n", faceAuthorized ? "Face OK" : "No face");
    display.setCursor(0, 56);
    display.printf("WiFi %s | MQTT %s",
                   WiFi.status() == WL_CONNECTED ? "OK" : "OFF",
                   client.connected() ? "OK" : "OFF");
    display.display();

    /*High temperature(=> TEMP_HOT) = Yellow LED turns on*/
    if (temp > TEMP_HOT)
    {
      digitalWrite(Y_LED_PIN, HIGH);
    }
    else
    {
      digitalWrite(Y_LED_PIN, LOW);
    }

    Serial.printf(
        "Temp: %.1f C | Hum: %.1f %% | Press: %.0f hPa | Movements: %s | Door: %s | FaceID: %s | WiFi: %s | MQTT: %s | Status: %s\n",
        temp, hum, press,
        motionDetected ? MOTION_DETECTED "!" : MOTION_QUIET,
        doorOpen ? DOOR_OPEN "!" : DOOR_CLOSED,
        faceAuthorized ? "Face OK" : "No face",
        WiFi.status() == WL_CONNECTED ? "OK" : "OFF",
        client.connected() ? "OK" : "OFF",
        systemStatus.c_str());
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("SmartHome_ELIAS");

  /* Connecting to Wi-Fi */
  WiFi.begin(ssid, password);
  uint32_t wifiTimeout = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < wifiTimeout)
  {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    reconnect();
  }
  else
  {
    Serial.println("\nWiFi FAILED");
    systemStatus = "WiFi Failed";
  }

  /* PIR */
  pinMode(PIR_PIN, INPUT_PULLDOWN);

  /* I2C */
  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();

  /* BUZZER */
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  /* LEDs */
  pinMode(B_LED_PIN, OUTPUT);
  pinMode(Y_LED_PIN, OUTPUT);
  digitalWrite(B_LED_PIN, LOW);
  digitalWrite(Y_LED_PIN, LOW);

  /* REED */
  pinMode(REED_PIN, INPUT_PULLUP);

  /* SERVO */
  doorServo.attach(SERVO_PIN);
  doorServo.write(LOCK_CLOSE_A);
  delay(500);

  // Try initial MQTT connect if WiFi OK
  if (WiFi.status() == WL_CONNECTED)
  {
    reconnect(); // Initial connect
  }

  /* ===== Final components check ===== */
  Serial.println("Checking all components...");
  bool allOk = true;

  // OLED
  bool oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOk)
  {
    Serial.println("OLED SSD1306 not detected!");
    allOk = false;
  }

  // BME280
  if (!bme.begin(0x77))
  {
    Serial.println("BME280 not detected!");
    allOk = false;
  }

  // RFID
  SPI.setRX(4);
  SPI.setTX(7);
  SPI.setSCK(6);
  SPI.begin();
  mfrc522.PCD_Init();
  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (version == 0x00 || version == 0xFF)
  {
    Serial.println("RFID RC522 not detected or wiring issue!");
    allOk = false;
  }

  if (allOk)
  {
    Serial.println("ALL COMPONENTS OK!");
  }
  else
  {
    Serial.println("CHECK WIRING!");
    systemStatus = "Components Failed";
  }

  // Shows whether the components passed the test (output to OLED)
  if (oledOk)
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("SmartHome_ELIAS");
    display.setCursor(0, 15);
    if (allOk)
    {
      display.println("ALL COMPONENTS OK!");
    }
    else
    {
      display.println("CHECK WIRING!");
    }
    display.setCursor(0, 30);
    display.println("PIR calibrating...");
    display.setCursor(0, 45);
    display.printf("WiFi %s | MQTT %s", WiFi.status() == WL_CONNECTED ? "OK" : "OFF", client.connected() ? "OK" : "N/A");
    display.display();
  }

  // PIR calibration
  Serial.println("PIR calibration...");
  delay(PIR_CALIBRATION_TIME);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);
  Serial.println("System ready.");
}

void loop()
{
  handleWiFi();
  handleMQTT();
  handleEnvironment();
  handleSecurity();
  handleDisplay();

  // If components failed in setup, publish error once connected
  static bool errorPublished = false;
  if (client.connected() && systemStatus == "Components Failed" && !errorPublished)
  {
    client.publish(TOPIC_ERROR, "Some components failed initialization", true);
    errorPublished = true;
  }

  // If WiFi failed in setup, publish error once connected
  static bool wifiErrorPublished = false;
  if (client.connected() && systemStatus == "WiFi Failed" && !wifiErrorPublished)
  {
    client.publish(TOPIC_ERROR, "WiFi connection failed on startup", true);
    wifiErrorPublished = true;
  }
}