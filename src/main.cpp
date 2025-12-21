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

/* ===== BUZZER ===== */
#define BUZZER_PIN 15
#define BUZZER_DUR 500
#define BUZZER_FREQ 1000
uint32_t buzzerOffTime = 0;
bool buzzerActive = 0;

/* ===== LEDs ===== */
#define B_LED_PIN 16
#define Y_LED_PIN 17
#define TEMP_HOT 28.0 // °C

/* ===== PIR ===== */
#define PIR_PIN 22
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

  /* BUZZER */
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  /* LEDs */
  pinMode(B_LED_PIN, OUTPUT);
  pinMode(Y_LED_PIN, OUTPUT);
  digitalWrite(B_LED_PIN, LOW);
  digitalWrite(Y_LED_PIN, LOW);

  /* OLED */
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("SSD1306 not found!");
    while (true)
      ;
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
    while (true)
      ;
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
  /* ===== Handle PIR event + buzzer + blue LED ====== */
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
        "Temp: %.1f C | Hum: %.1f %% | Press: %.0f hPa | %s\n",
        temp, hum, press,
        motionDetected ? "MOVEMENT" : "quiet");
  }
}
