# SmartHome_ELIAS (Core & Hardware)

An open-source smart home automation ecosystem powered by **Raspberry Pi 5** and **Raspberry Pi Pico 2W**. This repository contains the microcontroller firmware, backend scripts, and database schemas.

## Features
* **Multi-Factor Authentication:** Secure entry system combining RFID (MFRC522) tokens with AI Face Recognition via HuskyLens.
* **Smart Security Grid:** Motion tracking (PIR) and magnetic door contact (Reed Switch) with dynamic arming profiles and automated countdown delays.
* **Climate Automation:** Autonomous HVAC tracking utilizing a BME280 sensor to drive automated fan states.
* **OLED Dashboard:** Real-time hardware status bar with dynamic WiFi/MQTT connection state icons.

## Hardware Components
* Raspberry Pi Pico 2W & Raspberry Pi 5
* HuskyLens AI Camera
* MFRC522 RFID Module & SG90 Servo Motor
* BME280 Sensor & SSD1306 OLED Display (128x64)
* PIR Motion Sensor, Magnetic Reed Switch, Buzzer & LEDs

## Broject Structure
* `/src` - Main C++ PlatformIO/Arduino code for Pico 2W.
* `/server/scripts` - Python MQTT-to-DB bridges and backend scripts.
* `/server/db` - SQL Schemas for database initializations.

## Getting Started
1. **Database:** Import the SQL schema into your local MySQL instance.
2. **Backend:** Launch the Flask server and MQTT bridge using Python.
3. **Firmware:** Flash the Pico 2W firmware using PlatformIO (ensure all required Adafruit, MFRC522, and Servo libraries are installed).

## License
This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**. 

### What this means:
* **Source Code Must Be Shared:** Anyone modifying or distributing this software must open-source their changes under the same GPL-3.0 license.
* **No Closed-Source Commercialization:** Copying this code into a closed, proprietary application for commercial sales is strictly prohibited.
* **Attribution:** Any derivative works must credit the original author (**Hass**).

See the [LICENSE](LICENSE) file for the full text.
