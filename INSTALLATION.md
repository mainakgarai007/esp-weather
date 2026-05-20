# SkyCore / LightOS Core — Installation Guide

## 1. Hardware Setup

- ESP8266 NodeMCU board
- LEDs / NeoPixels wired to GPIOs:
  - RED → GPIO5
  - ORANGE → GPIO4
  - GREEN → GPIO0
  - BLUE → GPIO2
  - PIXEL → GPIO14
  - WARM WHITE → GPIO12
  - YELLOW → GPIO13
  - BUZZER → GPIO15
  - BUTTON → GPIO16
  - EXTRA LED → GPIO3

## 2. Firmware Flashing

1. Install Arduino IDE and the ESP8266 board package.
2. Install libraries from the firmware header:
   - NTPClient, ArduinoJson, Adafruit NeoPixel, LittleFS
3. Open `firmware/SkyCore/SkyCore.ino`.
4. Select **NodeMCU 1.0 (ESP-12E)** and correct COM port.
5. Upload the firmware to the board.

## 3. First Boot & WiFi Setup

1. Power the ESP8266.
2. Connect to WiFi: **SkyCore_Setup** (password: **12345678**).
3. Visit **http://192.168.4.1**.
4. Enter:
   - WiFi name + password
   - OpenWeatherMap API key
   - Device name
   - Pairing key from the dashboard onboarding
5. Save and reboot.

## 4. Pair With Dashboard

1. Open the dashboard (GitHub Pages `/docs`).
2. Go to **Pair Device**.
3. Enter the ESP IP address and device key.
4. Click **Connect & Verify**.

Your SkyCore device is now paired and live.
