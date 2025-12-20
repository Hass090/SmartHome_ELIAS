#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>

/* ===== OLED ===== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

/* ===== I2C ===== */
#define I2C_SDA 12
#define I2C_SCL 13

/* ===== PIR ===== */
#define PIR_PIN 15
static const uint32_t PIR_CALIBRATION_TIME = 60000;
static const uint32_t PIR_IRQ_DEBOUNCE = 100;

/* ===== Update timing ===== */
static const uint32_t SENSOR_UPDATE_TIME = 2000;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BME280 bme;

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

void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println("Smart Home - BME280 + OLED + PIR (ISR)");

  /* PIR */
  pinMode(PIR_PIN, INPUT_PULLDOWN);

  /* I2C */
  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();

  /* OLED */
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("SSD1306 not found!");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Smart Home PAP");
  display.println("PIR calibrating...");
  display.display();

  /* BME280 */
  if (!bme.begin(0x77))
  {
    Serial.println("BME280 not found!");
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("BME280 error");
    display.display();
    while (true);
  }

  /* PIR calibration */
  Serial.println("PIR calibration...");
  delay(PIR_CALIBRATION_TIME);

  /* Attach interrupt */
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);

  Serial.println("System ready.");
}

void loop()
{
  /* ===== Handle PIR event ===== */
  if (motionIRQ)
  {
    motionIRQ = false;
    motionDetected = true;
    lastMotionTime = millis();
  }

  /* Resetting the movement indication after 2 sec */
  if (motionDetected && millis() - lastMotionTime > 2000)
  {
    motionDetected = false;
  }

  if (millis() - lastSensorUpdate >= SENSOR_UPDATE_TIME)
  {
    lastSensorUpdate = millis();

    float temp = bme.readTemperature();
    float hum = bme.readHumidity();
    float press = bme.readPressure() / 100.0F;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Smart Home PAP");

    display.setCursor(0, 18);
    display.printf("Temp:  %.1f C\n", temp);

    display.setCursor(0, 28);
    display.printf("Hum:   %.1f %%\n", hum);

    display.setCursor(0, 38);
    display.printf("Press: %.0f hPa\n", press);

    display.setCursor(0, 52);
    display.println(motionDetected ? "MOVEMENT!" : "All quiet");

    display.display();

    Serial.printf(
        "Temp: %.1f C | Hum: %.1f %% | Press: %.0f hPa | %s\n",
        temp, hum, press,
        motionDetected ? "MOVEMENT" : "quiet");
  }
}
