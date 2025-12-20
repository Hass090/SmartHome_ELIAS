#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 

#define I2C_SDA 12
#define I2C_SCL 13
#define PIR_PIN 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BME280 bme; // I2C
bool motionDetected = false;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Smart Home - BME280 + OLED + PIR");

  pinMode(PIR_PIN, INPUT);
  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  
    Serial.println(F("SSD1306 not found!"));
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Smart Home PAP");
  display.display();

  // BME280
  if (!bme.begin(0x77)) { 
    Serial.println("BME280 not found!");
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("BME280 error");
    display.display();
    while (1);
  }

  Serial.println("BME280, OLED and PIR are included!");
}

void loop() {
  motionDetected = digitalRead(PIR_PIN);

  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();
  float press = bme.readPressure() / 100.0F;    

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Smart Home PAP");

  display.setCursor(0, 20);
  display.print("Temp: "); display.print(temp, 1); display.println(" \xF8""C");

  display.setCursor(0, 30);
  display.print("Hum:  "); display.print(hum, 1); display.println(" %");

  display.setCursor(0, 40);
  display.print("Press: "); display.print(press, 0); display.println(" hPa");

  display.setCursor(0, 50);
  if (motionDetected) {
    display.println("MOVEMENT DETECTED!");
  } else {
    display.println("All quiet");
  }
  display.display();

  Serial.printf("Temp: %.1f °C | Hum: %.1f %% | Press: %.0f hPa\n", temp, hum, press);
  Serial.println(motionDetected ? "MOVEMENT DETECTED!" : "All quiet");

  delay(2000);
}