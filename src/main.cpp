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

const char *ssid = "NOS-ADA4";
const char *password = "R6H44EEE";
const char *mqtt_server = "192.168.1.25";

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
#define TOPIC_CONTROL_FAN BASE_TOPIC "control/fan"
#define TOPIC_FAN BASE_TOPIC "environment/fan"
#define TOPIC_CONTROL_LIGHT BASE_TOPIC "control/light"
#define TOPIC_LIGHT BASE_TOPIC "environment/light"
#define TOPIC_ERROR BASE_TOPIC "error"
#define HEARTBEAT_TIME 20000
uint32_t lastHeartbeat = 0;

/* ===== OLED ===== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

/* ===== ICONS (8x8) ===== */
const unsigned char PROGMEM icon_wifi_on[] = {
    0x00, 0x3c, 0x42, 0x81, 0x3c, 0x42, 0x18, 0x18};
const unsigned char PROGMEM icon_wifi_off[] = {
    0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81};
const unsigned char PROGMEM icon_mqtt_on[] = {
    0xff, 0x81, 0xbd, 0x81, 0xff, 0x81, 0xbd, 0xff};
const unsigned char PROGMEM icon_mqtt_off[] = {
    0xff, 0x81, 0xa5, 0xd1, 0xd9, 0xa5, 0x81, 0xff};

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
bool buzzerActive = false;

/* ===== LEDs ===== */
#define B_LED_PIN 21
#define Y_LED_PIN 20
#define RGB_LED_PIN 17
bool rgbLedOn = false;
#define TEMP_HOT 32.0

/* ===== FAN ===== */
#define FAN_PIN 16
bool fanOn = false;
bool manualFanMode = false;
static bool lastFanPublished = false;
bool autoModeEnabled = true;
uint32_t lastManualOffTime = 0;
const uint32_t MANUAL_OFF_HOLD_MS = 600000;

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
#define LOCK_OPEN_A 0
#define LOCK_CLOSE_A 180
#define OPEN_DUR 10000
Servo doorServo;
uint32_t lockOpenTime = 0;
bool lockIsOpen = false;
static bool lastLockPublished = false;

/* ===== HUSKYLENS ===== */
uint32_t lastFaceTime = 0;
const uint32_t FACE_WINDOW = 3000;
bool faceAuthorized = false;
static bool lastFacePublished = false;

/* ===== PIR ===== */
#define PIR_PIN 19
static const uint32_t PIR_CALIBRATION_TIME = 60000;
static const uint32_t PIR_IRQ_DEBOUNCE = 100;
static bool lastMotionPublished = false;

/* ===== SECURITY SYSTEM ===== */
bool securityArmed = true;
uint32_t armedTime = 0;
const uint32_t EXIT_DELAY_MS = 15000;

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
    client.subscribe(TOPIC_CONTROL_FAN);
    client.subscribe(TOPIC_CONTROL_LIGHT);
  }
  else
  {
    if (millis() - start > 30000)
      systemStatus = "MQTT Timeout";
    else
      systemStatus = "MQTT Failed";
  }
}

/* ===== Handles WiFi auto-reconnection if connection drops ===== */
void handleWiFi()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    static uint32_t lastWiFiRetry = 0;
    if (millis() - lastWiFiRetry > 10000)
    {
      lastWiFiRetry = millis();
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      systemStatus = "WiFi Reconnecting";
    }
  }
}

/* ===== Keeps the MQTT client processing loops and handles periodic heartbeats ===== */
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
    if (millis() - lastHeartbeat > HEARTBEAT_TIME)
    {
      lastHeartbeat = millis();
      client.publish(TOPIC_STATUS, "online", true);
    }
  }
}

/* ===== Processes incoming MQTT messages from subscribed control topics ===== */
void callback(char *topic, byte *payload, unsigned int length)
{
  String message = "";
  for (unsigned int i = 0; i < length; i++)
    message += (char)payload[i];
  String topicStr = String(topic);

  // Control door lock actuator via remote commands
  if (topicStr == TOPIC_CONTROL_DOOR)
  {
    if (message == "OPEN")
    {
      Serial.println("Open the door from App - Disarming system");
      doorServo.write(LOCK_OPEN_A);
      lockIsOpen = true;
      lockOpenTime = millis();
      securityArmed = false; // Trust app command to automatically disarm security grid

      if (client.connected())
      {
        client.publish(BASE_TOPIC "security/state", "DISARMED", true);
      }
    }
    else if (message == "CLOSED" || message == "close")
    {
      doorServo.write(LOCK_CLOSE_A);
      lockIsOpen = false;
    }
  }
  // Face recognition event triggered from Raspberry Pi 5
  else if (topicStr == TOPIC_FACE_EVENT)
  {
    if (message == "Known face detected!")
    {
      faceAuthorized = true;
      lastFaceTime = millis();
    }
  }
  // Fan state and mode automation handler
  else if (topicStr == TOPIC_CONTROL_FAN)
  {
    if (message == "AUTO")
    {
      autoModeEnabled = true;
      manualFanMode = false;
      lastManualOffTime = 0;
      client.publish(TOPIC_FAN, fanOn ? "ON" : "OFF", true);
      client.publish(BASE_TOPIC "environment/fan_mode", "AUTO", true);
    }
    else if (message == "MANUAL")
    {
      autoModeEnabled = false;
      manualFanMode = true;
      client.publish(TOPIC_FAN, fanOn ? "ON" : "OFF", true);
      client.publish(BASE_TOPIC "environment/fan_mode", "MANUAL", true);
    }
    else if (!autoModeEnabled)
    {
      if (message == "ON")
      {
        digitalWrite(FAN_PIN, HIGH);
        digitalWrite(Y_LED_PIN, HIGH);
        fanOn = true;
        client.publish(TOPIC_FAN, "ON", true);
      }
      else if (message == "OFF")
      {
        digitalWrite(FAN_PIN, LOW);
        digitalWrite(Y_LED_PIN, LOW);
        fanOn = false;
        lastManualOffTime = millis();
        client.publish(TOPIC_FAN, "OFF", true);
      }
    }
  }
  // Light fixture control topic
  else if (topicStr == TOPIC_CONTROL_LIGHT)
  {
    if (message == "ON")
    {
      digitalWrite(RGB_LED_PIN, HIGH);
      rgbLedOn = true;
      client.publish(TOPIC_LIGHT, "ON", true);
    }
    else if (message == "OFF")
    {
      digitalWrite(RGB_LED_PIN, LOW);
      rgbLedOn = false;
      client.publish(TOPIC_LIGHT, "OFF", true);
    }
  }
}

/* ===== Reads data from BME280 sensor and publishes values to MQTT broker ===== */
void handleEnvironment()
{
  if (!client.connected())
    return;
  if (millis() - lastEnvPublish >= ENV_UPDATE_TIME)
  {
    lastEnvPublish = millis();
    temp = bme.readTemperature();
    hum = bme.readHumidity();
    press = bme.readPressure() / 100.0F;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.1f", temp);
    client.publish(TOPIC_TEMP, buffer, true);
    snprintf(buffer, sizeof(buffer), "%.1f", hum);
    client.publish(TOPIC_HUM, buffer, true);
    snprintf(buffer, sizeof(buffer), "%.0f", press);
    client.publish(TOPIC_PRESS, buffer, true);
  }
}

/* ===== Controls perimeter alarms, processing updates from PIR, Reed Switch, and RFID ===== */
void handleSecurity()
{
  // Periodically synchronizes structural states to MQTT topics on modification
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

  // Handle triggered motion sensor interrupts (Alarms sound only when armed and exit window has passed)
  if (motionIRQ)
  {
    motionIRQ = false;
    motionDetected = true;
    lastMotionTime = millis();
    if (securityArmed && (millis() - armedTime > EXIT_DELAY_MS))
    {
      digitalWrite(B_LED_PIN, HIGH);
      tone(BUZZER_PIN, BUZZER_FREQ);
      buzzerActive = true;
      buzzerOffTime = millis() + BUZZER_DUR;
    }
  }

  if (buzzerActive && millis() >= buzzerOffTime)
  {
    noTone(BUZZER_PIN);
    digitalWrite(B_LED_PIN, LOW);
    buzzerActive = false;
  }

  if (motionDetected && millis() - lastMotionTime > 2000)
  {
    motionDetected = false;
  }

  // Monitor physical alignment of the door using a magnetic reed switch contact
  int reedState = digitalRead(REED_PIN);
  if (reedState != lastReedState)
    lastReedChange = millis();

  if (millis() - lastReedChange > REED_DEBOUNCE)
  {
    bool newDoorOpen = (reedState == LOW);
    if (newDoorOpen != doorOpen)
    {
      doorOpen = newDoorOpen;
      if (client.connected())
      {
        client.publish(TOPIC_DOOR, doorOpen ? DOOR_OPEN : DOOR_CLOSED, true);
        lastDoorPublished = doorOpen;
      }
      // Trigger instant siren if door is opened while armed after exit cooldown expires
      if (doorOpen && securityArmed && (millis() - armedTime > EXIT_DELAY_MS))
      {
        tone(BUZZER_PIN, BUZZER_FREQ);
        buzzerActive = true;
        buzzerOffTime = millis() + (BUZZER_DUR * 4);
      }
    }
  }
  lastReedState = reedState;

  // Process RFID access card interactions for authorization profiles
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
  {
    if (checkUID())
    {
      if (securityArmed)
      {
        // Perimeter is ARMED: Requires matching RFID token AND a validated face pattern to disarm
        if (faceAuthorized && (millis() - lastFaceTime < FACE_WINDOW))
        {
          Serial.println("ACCESS GRANTED - DISARMING SYSTEM");
          securityArmed = false;
          doorServo.write(LOCK_OPEN_A);
          lockIsOpen = true;
          lockOpenTime = millis();
          tone(BUZZER_PIN, 1500, 200);
          delay(200);
          tone(BUZZER_PIN, 2000, 200);
          delay(200);
          tone(BUZZER_PIN, 1500, 200);
          if (client.connected())
          {
            client.publish(TOPIC_ACCESS, ACCESS_GRANTED, true);
            client.publish(BASE_TOPIC "security/state", "DISARMED", true);
          }
        }
        else
        {
          Serial.println("RFID OK but no recent face - ACCESS DENIED");
          tone(BUZZER_PIN, 500, 800);
          if (client.connected())
            client.publish(TOPIC_ACCESS, ACCESS_DENIED_NO_FACE, true);
        }
      }
      else
      {
        // Perimeter is DISARMED: Swiping matching RFID acts as lock trigger and arms security network
        Serial.println("SYSTEM ARMING (15 SECONDS DELAY)...");
        securityArmed = true;
        armedTime = millis(); // Initialize exit timer window
        doorServo.write(LOCK_CLOSE_A);
        lockIsOpen = false;

        // Sound arming notification tone sequence
        tone(BUZZER_PIN, 2000, 200);
        delay(200);
        tone(BUZZER_PIN, 1500, 200);

        if (client.connected())
        {
          client.publish(BASE_TOPIC "security/state", "ARMING", true);
        }
      }
    }
    else
    {
      Serial.println("Unknown card - ACCESS DENIED");
      tone(BUZZER_PIN, 300, 1000);
      if (client.connected())
        client.publish(TOPIC_ACCESS, ACCESS_DENIED_UNKNOWN, true);
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }

  // Automatically latch lock mechanisms closed after predefined open frame duration
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

  if (faceAuthorized && millis() - lastFaceTime > FACE_WINDOW)
  {
    faceAuthorized = false;
  }
}

/* ===== Handles UI updates on the SSD1306 OLED, formatting system states and dashboard values ===== */
void handleDisplay()
{
  if (millis() - lastSensorUpdate >= SENSOR_UPDATE_TIME)
  {
    lastSensorUpdate = millis();
    temp = bme.readTemperature();
    hum = bme.readHumidity();
    press = bme.readPressure() / 100.0F;

    display.clearDisplay();

    // --- 1. STATUS HEADER ---
    if (WiFi.status() == WL_CONNECTED)
    {
      display.drawBitmap(0, 0, icon_wifi_on, 8, 8, SSD1306_WHITE);
    }
    else
    {
      display.drawBitmap(0, 0, icon_wifi_off, 8, 8, SSD1306_WHITE);
    }

    if (client.connected())
    {
      display.drawBitmap(12, 0, icon_mqtt_on, 8, 8, SSD1306_WHITE);
    }
    else
    {
      display.drawBitmap(12, 0, icon_mqtt_off, 8, 8, SSD1306_WHITE);
    }

    display.setCursor(34, 0);
    display.print("HOME.ELIAS");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // --- 2. ENVIRONMENT PANEL ---
    display.setCursor(0, 14);
    display.printf("T:%.1f H:%.0f%% P:%.0f", temp, hum, press);

    // --- 3. SECURITY SYSTEM STATE ---
    String secStatus = "OFF";
    if (securityArmed)
    {
      if (millis() - armedTime > EXIT_DELAY_MS)
      {
        secStatus = "ARMED";
      }
      else
      {
        uint32_t timeLeft = (EXIT_DELAY_MS - (millis() - armedTime)) / 1000;
        secStatus = "ARMING " + String(timeLeft) + "s";
      }
    }
    display.setCursor(0, 26);
    display.printf("Security: %s", secStatus.c_str());

    // --- 4. PERIMETER DETECTORS ---
    display.setCursor(0, 36);
    display.printf("Door: %s", doorOpen ? "OPEN!" : "Closed");

    display.setCursor(72, 36);
    display.printf(" Mot: %s", motionDetected ? "YES" : "No");

    // --- 5. BIOMETRIC AUTHENTICATION ---
    display.setCursor(0, 46);
    display.printf("Face ID: %s", faceAuthorized ? "OK (Auth)" : "None");

    // --- 6. HVAC CONTROL FOOTER ---
    display.drawLine(0, 55, 128, 55, SSD1306_WHITE);
    display.setCursor(0, 57);
    display.printf("Fan: %s | Mode: %s", fanOn ? "ON" : "OFF", autoModeEnabled ? "AUTO" : "MAN");

    display.display();

    // Climate automation logic rules (Auto-switch control loops)
    bool autoShouldOn = (temp > TEMP_HOT);
    if (autoModeEnabled)
    {
      bool should = autoShouldOn;
      digitalWrite(FAN_PIN, should ? HIGH : LOW);
      digitalWrite(Y_LED_PIN, should ? HIGH : LOW);
      fanOn = should;
    }

    if (fanOn != lastFanPublished && client.connected())
    {
      client.publish(TOPIC_FAN, fanOn ? "ON" : "OFF", true);
      lastFanPublished = fanOn;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);

  WiFi.begin(ssid, password);
  uint32_t wifiTimeout = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < wifiTimeout)
  {
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    reconnect();
  }
  else
  {
    systemStatus = "WiFi Failed";
  }

  pinMode(PIR_PIN, INPUT_PULLDOWN);
  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(B_LED_PIN, OUTPUT);
  digitalWrite(B_LED_PIN, LOW);
  pinMode(Y_LED_PIN, OUTPUT);
  digitalWrite(Y_LED_PIN, LOW);
  pinMode(RGB_LED_PIN, OUTPUT);
  digitalWrite(RGB_LED_PIN, LOW);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  pinMode(REED_PIN, INPUT_PULLUP);

  doorServo.attach(SERVO_PIN);
  doorServo.write(LOCK_CLOSE_A);
  delay(500);

  if (WiFi.status() == WL_CONNECTED)
  {
    client.publish(TOPIC_FAN, fanOn ? "ON" : "OFF", true);
    client.publish(TOPIC_LIGHT, rgbLedOn ? "ON" : "OFF", true);
  }

  bool allOk = true;
  bool oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOk)
    allOk = false;
  if (!bme.begin(0x77))
    allOk = false;

  SPI.setRX(4);
  SPI.setTX(3);
  SPI.setSCK(2);
  SPI.begin();
  mfrc522.PCD_Init();
  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (version == 0x00 || version == 0xFF)
    allOk = false;

  if (!allOk)
    systemStatus = "Components Failed";

  if (oledOk)
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // --- 1. STATUS HEADER ---
    if (WiFi.status() == WL_CONNECTED)
    {
      display.drawBitmap(0, 0, icon_wifi_on, 8, 8, SSD1306_WHITE);
    }
    else
    {
      display.drawBitmap(0, 0, icon_wifi_off, 8, 8, SSD1306_WHITE);
    }

    if (client.connected())
    {
      display.drawBitmap(12, 0, icon_mqtt_on, 8, 8, SSD1306_WHITE);
    }
    else
    {
      display.drawBitmap(12, 0, icon_mqtt_off, 8, 8, SSD1306_WHITE);
    }

    display.setCursor(34, 0);
    display.print("HOME.ELIAS");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // --- 2. INITIALIZATION STATE ---
    display.setCursor(0, 22);
    if (allOk)
    {
      display.print("SYSTEM STATUS: OK");
    }
    else
    {
      display.print("SYSTEM ERROR!");
    }

    display.setCursor(0, 36);
    display.print(allOk ? "All components ready." : "Check wiring!");

    display.drawLine(0, 49, 128, 49, SSD1306_WHITE);

    // --- 3. PIR CALIBRATION SEQUENCER WITH COUNTDOWN ---
    int countdownSeconds = PIR_CALIBRATION_TIME / 1000;

    for (int i = countdownSeconds; i > 0; i--)
    {
      display.fillRect(0, 51, 128, 13, SSD1306_BLACK);

      display.setCursor(0, 54);
      display.printf("PIR Calibrating: %ds", i);
      display.display();

      delay(1000);
    }

    display.fillRect(0, 51, 128, 13, SSD1306_BLACK);
    display.setCursor(0, 54);
    display.print("System Ready!");
    display.display();
    delay(1000);
  }
  else
  {
    delay(PIR_CALIBRATION_TIME);
  }

  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);
}

void loop()
{
  handleWiFi();
  handleMQTT();
  handleEnvironment();
  handleSecurity();
  handleDisplay();

  static bool errorPublished = false;
  if (client.connected() && systemStatus == "Components Failed" && !errorPublished)
  {
    client.publish(TOPIC_ERROR, "Some components failed initialization", true);
    errorPublished = true;
  }

  static bool wifiErrorPublished = false;
  if (client.connected() && systemStatus == "WiFi Failed" && !wifiErrorPublished)
  {
    client.publish(TOPIC_ERROR, "WiFi connection failed on startup", true);
    wifiErrorPublished = true;
  }
}
