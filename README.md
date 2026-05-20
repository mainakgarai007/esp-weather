# SkyCore / LightOS Core

SkyCore (LightOS Core) is a production-ready, ESP8266-based ambient environmental operating system. It turns weather, seasons, moon phases, weekends, holidays, and device health into a futuristic LED language — backed by a premium, mobile-first web dashboard that runs entirely on GitHub Pages.

## ✨ Key Features

- **Ambient LED intelligence**: weather, season, rain, heat, day/night, moon phases, weekend/holiday, device state, and internet state.
- **Dual NeoPixels**: environment + device status with pulsing, blinking, and priority logic.
- **Pairing system**: secure pairing key generated in the dashboard.
- **Captive portal setup**: first-boot WiFi, API key, device name, and pairing key.
- **Local-first web dashboard**: works without backend using LocalStorage + PWA cache.
- **Firmware management**: syntax-highlighted firmware viewer, copy + download buttons.
- **Notifications**: thunderstorm, heavy rain, offline, API failure, OTA complete, developer mode.
- **Settings + controls**: brightness, animations, buzzer, night mute, device name, WiFi reset.

## 📂 Repository Structure

```
.
├── docs/                     # GitHub Pages dashboard + PWA
│   ├── css/                  # UI styling
│   ├── js/                   # UI logic
│   ├── firmware/             # Firmware download copy for GH Pages
│   ├── index.html            # Home
│   ├── dashboard.html        # Live dashboard
│   ├── status.html           # Device status
│   ├── pairing.html          # Pairing center
│   ├── pixel-legend.html      # Pixel legend
│   ├── led-legend.html        # LED legend
│   ├── firmware.html         # Firmware manager
│   ├── settings.html         # Device settings
│   ├── update.html           # Update info
│   ├── logs.html             # Logs & charts
│   ├── notifications.html    # Notification settings
│   ├── buttons.html          # Button guide
│   ├── help.html             # Help + FAQs
│   ├── manifest.json         # PWA manifest
│   └── service-worker.js     # PWA cache
├── firmware/
│   └── SkyCore/
│       └── SkyCore.ino       # ESP8266 firmware
├── INSTALLATION.md           # Installation guide
└── DEPLOYMENT.md             # GitHub Pages deployment
```

## 🚀 Quick Start

1. Flash the firmware from `firmware/SkyCore/SkyCore.ino` using Arduino IDE.
2. On first boot, connect to `SkyCore_Setup` WiFi and open `192.168.4.1`.
3. Enter your WiFi details, OpenWeather API key, device name, and pairing key.
4. Open the dashboard in `/docs` (GitHub Pages) and pair using the ESP IP address.

## 📘 Guides

- **Installation**: [INSTALLATION.md](INSTALLATION.md)
- **Deployment**: [DEPLOYMENT.md](DEPLOYMENT.md)

## ✅ Requirements

- ESP8266 NodeMCU
- Arduino IDE + ESP8266 core
- Libraries listed in the firmware header (NTPClient, ArduinoJson, Adafruit NeoPixel, etc.)
- OpenWeatherMap API key

---

SkyCore / LightOS Core — a premium ambient environment OS for smart spaces.
