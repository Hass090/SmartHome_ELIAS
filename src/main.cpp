#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include "HUSKYLENS.h"

HUSKYLENS huskylens;

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

/* ===== HUSKYLENS ===== */
const int KNOWN_FACE_ID = 1;
uint32_t lastFaceTime = 0;
static uint32_t lastFacePrint = 0;
const uint32_t FACE_WINDOW = 3000;
const uint32_t FACE_PRINT_DELAY = 3000;
bool faceAuthorized = false;

/* ===== PIR ===== */
#define PIR_PIN 19
static const uint32_t PIR_CALIBRATION_TIME = 60000;
static const uint32_t PIR_IRQ_DEBOUNCE = 100;

/* ===== Update timing ===== */
static const uint32_t SENSOR_UPDATE_TIME = 2000;

/* ===== PIR ISR state ===== */
volatile bool motionIRQ = false;
volatile uint32_t lastIRQTime = 0;

/* ===== App state ===== */
bool motionDetected = false;
uint32_t lastMotionTime = 0;
uint32_t lastSensorUpdate = 0;

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

void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println("Smart Home Elias");

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

  // Huskylens
  if (!huskylens.begin(Wire))
  {
    Serial.println("Huskylens not detected!");
    allOk = false;
  }
  else
  {
    huskylens.writeAlgorithm(ALGORITHM_FACE_RECOGNITION);
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
    Serial.println("### All sensors and modules DETECTED and ready! ###");
  }
  else
  {
    Serial.println("### Some components FAILED - check wiring/power ###");
  }
  // Shows whether the components passed the test (output to OLED)
  if (oledOk)
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Home Elias");
    display.setCursor(0, 20);
    if (allOk)
    {
      display.println("ALL COMPONENTS OK!");
    }
    else
    {
      display.println("CHECK WIRING!");
    }
    display.setCursor(0, 40);
    display.println("PIR calibrating...");
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
  /* Handle PIR event + buzzer + blue LED */
  if (motionIRQ)
  {
    motionIRQ = false;
    motionDetected = true;
    lastMotionTime = millis();

    /* turn on the buzzer and blue LED */
    digitalWrite(B_LED_PIN, HIGH);
    tone(BUZZER_PIN, BUZZER_FREQ);
    buzzerActive = true;
    buzzerOffTime = millis() + BUZZER_DUR;
  }
  /* turn off the buzzer and blue LED */
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

  if (reedState == LOW && !doorOpen)
  {
    doorOpen = true;
    lastDoorTime = millis();

    tone(BUZZER_PIN, BUZZER_FREQ);
    buzzerActive = true;
    buzzerOffTime = millis() + BUZZER_DUR * 2;
  }

  // reset indication after 0.5 seconds
  if (doorOpen && millis() - lastDoorTime > 500)
  {
    doorOpen = false;
  }

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
      }
      else
      {
        Serial.println("RFID OK but no recent face - ACCESS DENIED");
        tone(BUZZER_PIN, 500, 800);
      }
    }
    else
    {
      Serial.println("Unknown card - ACCESS DENIED");
      tone(BUZZER_PIN, 300, 1000);
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
  // Automatically close the lock after 3 sec
  if (lockIsOpen && millis() - lockOpenTime >= OPEN_DUR)
  {
    doorServo.write(LOCK_CLOSE_A);
    lockIsOpen = false;
    Serial.println("Lock closed automatically");
  }

  // Huskylens processing

  bool currentFaceDetected = false;

  if (huskylens.request())
  {
    if (huskylens.isLearned() && huskylens.available())
    {
      while (huskylens.available())
      {
        HUSKYLENSResult result = huskylens.read();
        if (result.command == COMMAND_RETURN_BLOCK && result.ID == KNOWN_FACE_ID)
        {
          currentFaceDetected = true;
          break;
        }
      }
    }
  }

  // Updating the global state
  if (currentFaceDetected)
  {
    faceAuthorized = true;
    lastFaceTime = millis();

    if (millis() - lastFacePrint >= FACE_PRINT_DELAY)
    {
      Serial.println("Known face detected!");
      lastFacePrint = millis();
    }
  }
  else
  {
    // If more than 3 seconds have passed since the last detection - reset
    if (millis() - lastFaceTime > FACE_WINDOW)
    {
      faceAuthorized = false;
    }
  }

  if (millis() - lastSensorUpdate >= SENSOR_UPDATE_TIME)
  {
    lastSensorUpdate = millis();

    float temp = bme.readTemperature();
    float hum = bme.readHumidity();
    float press = bme.readPressure() / 100.0F;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Smart Home Elias");

    display.setCursor(0, 8);
    display.printf("Temp:  %.1f C\n", temp);

    display.setCursor(0, 16);
    display.printf("Hum:   %.1f %%\n", hum);

    display.setCursor(0, 24);
    display.printf("Press: %.0f hPa\n", press);

    display.setCursor(0, 32);
    display.printf("Movements: %s\n", motionDetected ? "DETECTED!" : "All quiet");

    display.setCursor(0, 40);
    display.printf("Door: %s\n", doorOpen ? "OPEN!" : "closed");

    display.setCursor(0, 48);
    display.printf("FaceID: %s\n", faceAuthorized ? "Face OK" : "No face");

    display.display();

    /*High temperature(=>28) = Yellow LED turns on*/
    if (temp > TEMP_HOT)
    {
      digitalWrite(Y_LED_PIN, HIGH);
    }
    else
    {
      digitalWrite(Y_LED_PIN, LOW);
    }

    Serial.printf(
        "Temp: %.1f C | Hum: %.1f %% | Press: %.0f hPa | Movements: %s | Door: %s | FaceID: %s\n",
        temp, hum, press,
        motionDetected ? "DETECTED!" : "All quiet",
        doorOpen ? "OPEN!" : "closed",
        faceAuthorized ? "Face OK" : "No face");
  }
}
