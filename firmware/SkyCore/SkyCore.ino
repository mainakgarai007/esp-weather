/*
 * SkyCore ESP8266 Firmware v1.0.0
 * Production-Ready Weather & Environment Display System
 * Board: ESP8266 NodeMCU
 *
 * Required Libraries (install via Arduino Library Manager):
 *   - ESP8266WiFi          (built-in with ESP8266 Arduino core)
 *   - ESP8266WebServer     (built-in with ESP8266 Arduino core)
 *   - ArduinoOTA           (built-in with ESP8266 Arduino core)
 *   - NTPClient            (by Fabrice Weinberg, v3.x)
 *   - WiFiUdp              (built-in with ESP8266 Arduino core)
 *   - ArduinoJson          (by Benoit Blanchon, v6.x)
 *   - Adafruit_NeoPixel    (by Adafruit)
 *   - LittleFS             (built-in with ESP8266 Arduino core)
 *   - ESP8266HTTPClient    (built-in with ESP8266 Arduino core)
 *   - DNSServer            (built-in with ESP8266 Arduino core)
 *
 * Hardware Notes:
 *   - GPIO0  (GREEN)  and GPIO2 (BLUE) are boot-sensitive; outputs are safe after boot
 *   - GPIO3  (EXTRA)  shares the UART RX line; disable with Serial.end() if conflicts arise
 *   - GPIO15 (BUZZER) must be LOW at boot; code ensures this via OUTPUT + LOW in setup()
 *   - GPIO16 (BUTTON) has an internal pull-down; press connects to VCC → reads HIGH
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>

// ============================================================
//  PIN DEFINITIONS
// ============================================================
#define PIN_RED     5
#define PIN_ORANGE  4
#define PIN_GREEN   0
#define PIN_BLUE    2
#define PIN_PIXEL   14
#define PIN_WARM    12
#define PIN_YELLOW  13
#define PIN_BUZZER  15
#define PIN_BUTTON  16
#define PIN_EXTRA   3

#define PIXEL_COUNT 2

// ============================================================
//  CONSTANTS
// ============================================================
#define OTA_PASSWORD          "skycore2024"
#define SETUP_SSID            "SkyCore_Setup"
#define SETUP_PASSWORD        "12345678"
#define CONFIG_FILE           "/config.json"

#define WEATHER_UPDATE_MS     600000UL   // 10 minutes
#define TOGGLE_INTERVAL_MS    2000UL     // Season/Weather toggle
#define BUTTON_HOLD_MS        3000UL
#define BUTTON_DEBOUNCE_MS    50UL
#define MULTI_PRESS_WINDOW_MS 400UL
#define NTP_SYNC_INTERVAL_MS  60000UL

#define NIGHT_START_HOUR      22
#define NIGHT_END_HOUR        7

// ============================================================
//  ENUMERATIONS
// ============================================================
enum DisplayMode    { MODE_WEATHER, MODE_SEASON };
enum WeatherCond    { WC_HOT, WC_COLD, WC_RAIN, WC_CLOUDY,
                      WC_HEAVY_RAIN, WC_HEAT_WARNING, WC_THUNDERSTORM, WC_CLEAR };
enum SeasonCond     { SC_SUMMER, SC_WINTER, SC_MONSOON };
enum WarmLedMode    { WL_DAY, WL_NIGHT, WL_CRESCENT, WL_FULL_MOON, WL_NEW_MOON };
enum YellowLedMode  { YL_WEEKDAY, YL_SATURDAY, YL_SUNDAY, YL_FESTIVAL };
enum ExtraLedMode   { EL_LOADING, EL_SUCCESS, EL_ERROR };
enum WiFiQuality    { WQ_CONNECTED, WQ_WEAK, WQ_VERY_WEAK, WQ_DISCONNECTED };
enum InternetQuality{ IQ_GOOD, IQ_SLOW, IQ_BAD, IQ_NONE };
enum ApiStatus      { AS_WORKING, AS_SLOW, AS_FAILED };
enum UpdateState    { US_IDLE, US_UPDATING, US_SUCCESS, US_FAIL };

// ============================================================
//  DATA STRUCTS
// ============================================================
struct WeatherData {
    float temperature = 0.0f;
    float humidity    = 0.0f;
    float windSpeed   = 0.0f;
    float uvIndex     = 0.0f;
    int   weatherId   = 800;
    int   aqi         = 1;
    bool  valid       = false;
};

struct DeviceConfig {
    char wifiSSID[64]   = "";
    char wifiPass[64]   = "";
    char apiKey[64]     = "";
    char deviceName[32] = "SkyCore";
    char pairKey[20]    = "";
    char city[32]       = "London";
    float lat           = 51.5f;
    float lon           = -0.12f;
};

struct ButtonState {
    bool          lastRaw       = LOW;
    bool          lastDebounced = LOW;
    unsigned long lastChange    = 0;
    unsigned long pressStart    = 0;
    unsigned long lastRelease   = 0;
    int           pressCount    = 0;
    bool          holdFired     = false;
};

// ============================================================
//  GLOBAL OBJECTS
// ============================================================
Adafruit_NeoPixel pixels(PIXEL_COUNT, PIN_PIXEL, NEO_GRB + NEO_KHZ800);
ESP8266WebServer  server(80);
DNSServer         dnsServer;
WiFiUDP           ntpUDP;
NTPClient         timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// ============================================================
//  GLOBAL STATE
// ============================================================
DeviceConfig    config;
WeatherData     weather;
ButtonState     btn;

DisplayMode     displayMode  = MODE_WEATHER;
WeatherCond     weatherCond  = WC_CLEAR;
SeasonCond      seasonCond   = SC_SUMMER;
WarmLedMode     warmMode     = WL_DAY;
YellowLedMode   yellowMode   = YL_WEEKDAY;
ExtraLedMode    extraMode    = EL_SUCCESS;
WiFiQuality     wifiQuality  = WQ_DISCONNECTED;
InternetQuality inetQuality  = IQ_NONE;
ApiStatus       apiStatus    = AS_FAILED;
UpdateState     updateState  = US_IDLE;

bool  devMode       = false;
bool  recoveryMode  = false;
bool  wifiSetupMode = false;
bool  isNight       = false;

unsigned long lastWeatherUpdate  = 0;
unsigned long lastToggle         = 0;
unsigned long lastNtpSync        = 0;
unsigned long lastPixelUpdate    = 0;
unsigned long lastDevPrint       = 0;
unsigned long updateStateStart   = 0;

// ============================================================
//  FORWARD DECLARATIONS
// ============================================================
void          loadConfig();
void          saveConfig();
void          startSetupMode();
void          connectWiFi();
void          setupOTA();
void          fetchWeather();
void          updateLEDs();
void          updateWarmLed();
void          updateYellowLed();
void          updateExtraLed();
void          setExtraMode(ExtraLedMode mode);
void          updatePixels();
void          handleButton();
void          processPressCount(int count);
bool          isNightTime();
void          updateNightMode();
void          buzzerTone(int freq, int durMs);
void          buzzerWarning();
void          devModeLoop();
void          blinkLed(int pin, int times, int onMs, int offMs);
String        generatePairKey();
uint32_t      colorFromHSV(int hue, int sat, int val);
WeatherCond   parseWeatherCondition(int id, float temp, float wind);
SeasonCond    parseSeasonFromMonth(int month);
WarmLedMode   parseMoonPhase(unsigned long epoch, bool nightTime);
YellowLedMode parseDayOfWeek(int dow);
void          handleRoot();
void          handleSave();
void          handleStatus();
void          handlePair();
void          handleApiConfig();

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n\n=== SkyCore v1.0.0 Booting ==="));

    // Configure all output pins
    const int outPins[] = {
        PIN_RED, PIN_ORANGE, PIN_GREEN, PIN_BLUE,
        PIN_WARM, PIN_YELLOW, PIN_BUZZER, PIN_EXTRA
    };
    for (int pin : outPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    pinMode(PIN_BUTTON, INPUT);

    // NeoPixel boot animation (fast rainbow)
    pixels.begin();
    pixels.setBrightness(80);
    pixels.clear();
    pixels.show();
    for (int i = 0; i < 256; i += 4) {
        pixels.setPixelColor(0, colorFromHSV(i, 255, 200));
        pixels.setPixelColor(1, colorFromHSV((i + 128) % 256, 255, 200));
        pixels.show();
        delay(8);
    }
    pixels.clear();
    pixels.show();

    // Check button hold at boot → recovery mode
    if (digitalRead(PIN_BUTTON) == HIGH) {
        recoveryMode = true;
        Serial.println(F("[BOOT] Recovery mode activated"));
        setExtraMode(EL_LOADING);
    }

    // Mount LittleFS
    if (!LittleFS.begin()) {
        Serial.println(F("[FS] Format and remount"));
        LittleFS.format();
        LittleFS.begin();
    }

    loadConfig();

    if (strlen(config.wifiSSID) == 0) {
        Serial.println(F("[WIFI] No credentials → setup mode"));
        startSetupMode();
    } else {
        connectWiFi();
    }

    if (!wifiSetupMode) {
        setupOTA();
        timeClient.begin();
        timeClient.update();

        server.on("/",           HTTP_GET,  handleRoot);
        server.on("/api/status", HTTP_GET,  handleStatus);
        server.on("/api/pair",   HTTP_POST, handlePair);
        server.on("/api/config", HTTP_POST, handleApiConfig);
        server.begin();
        Serial.println(F("[HTTP] Server started"));

        setExtraMode(EL_LOADING);
        fetchWeather();
    }
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
    // Setup / captive-portal mode
    if (wifiSetupMode) {
        dnsServer.processNextRequest();
        server.handleClient();
        updateExtraLed();
        // Purple breathing on both pixels to indicate WiFi setup
        unsigned long now = millis();
        if (now - lastPixelUpdate > 20) {
            lastPixelUpdate = now;
            int breath = (int)(100 + 100 * sin(now / 800.0));
            pixels.setPixelColor(0, pixels.Color(breath / 2, 0, breath));
            pixels.setPixelColor(1, pixels.Color(breath / 2, 0, breath));
            pixels.show();
        }
        return;
    }

    ArduinoOTA.handle();
    server.handleClient();

    unsigned long now = millis();

    // NTP sync
    if (now - lastNtpSync > NTP_SYNC_INTERVAL_MS) {
        lastNtpSync = now;
        timeClient.update();
        updateNightMode();
    }

    // Weather refresh every 10 minutes
    if (lastWeatherUpdate == 0 || now - lastWeatherUpdate > WEATHER_UPDATE_MS) {
        lastWeatherUpdate = now;
        fetchWeather();
    }

    // Season / Weather toggle every 2 seconds
    if (now - lastToggle > TOGGLE_INTERVAL_MS) {
        lastToggle = now;
        if (displayMode == MODE_WEATHER) {
            displayMode = MODE_SEASON;
            digitalWrite(PIN_RED, HIGH);
        } else {
            displayMode = MODE_WEATHER;
            digitalWrite(PIN_RED, LOW);
        }
    }

    handleButton();
    updateLEDs();
    updateWarmLed();
    updateYellowLed();
    updateExtraLed();
    updatePixels();

    // Clear transient update state after 1 second
    if ((updateState == US_SUCCESS || updateState == US_FAIL) &&
        now - updateStateStart > 1000) {
        updateState = US_IDLE;
    }

    // Developer mode serial dump every second
    if (devMode && now - lastDevPrint > 1000) {
        lastDevPrint = now;
        devModeLoop();
    }

    // Recovery mode: alternate blue/red on pixels (handled in updatePixels)
}

// ============================================================
//  CONFIG PERSISTENCE
// ============================================================
void loadConfig() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println(F("[CFG] No config file"));
        return;
    }
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return;

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        strlcpy(config.wifiSSID,   doc["ssid"]    | "",        sizeof(config.wifiSSID));
        strlcpy(config.wifiPass,   doc["pass"]    | "",        sizeof(config.wifiPass));
        strlcpy(config.apiKey,     doc["apiKey"]  | "",        sizeof(config.apiKey));
        strlcpy(config.deviceName, doc["name"]    | "SkyCore", sizeof(config.deviceName));
        strlcpy(config.pairKey,    doc["pairKey"] | "",        sizeof(config.pairKey));
        strlcpy(config.city,       doc["city"]    | "London",  sizeof(config.city));
        config.lat = doc["lat"] | 51.5f;
        config.lon = doc["lon"] | -0.12f;
        Serial.printf("[CFG] Loaded: SSID=%s Device=%s\n",
                      config.wifiSSID, config.deviceName);
    } else {
        Serial.println(F("[CFG] Parse error"));
    }
    f.close();
}

void saveConfig() {
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) {
        Serial.println(F("[CFG] Write failed"));
        return;
    }
    StaticJsonDocument<512> doc;
    doc["ssid"]    = config.wifiSSID;
    doc["pass"]    = config.wifiPass;
    doc["apiKey"]  = config.apiKey;
    doc["name"]    = config.deviceName;
    doc["pairKey"] = config.pairKey;
    doc["city"]    = config.city;
    doc["lat"]     = config.lat;
    doc["lon"]     = config.lon;
    serializeJson(doc, f);
    f.close();
    Serial.println(F("[CFG] Saved"));
}

// ============================================================
//  WIFI
// ============================================================
void startSetupMode() {
    wifiSetupMode = true;
    setExtraMode(EL_LOADING);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0));
    WiFi.softAP(SETUP_SSID, SETUP_PASSWORD);
    Serial.printf("[AP] SSID=%s  IP=%s\n",
                  SETUP_SSID, WiFi.softAPIP().toString().c_str());

    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));

    server.on("/",        HTTP_GET,  handleRoot);
    server.on("/save",    HTTP_POST, handleSave);
    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    });
    server.begin();
}

void connectWiFi() {
    Serial.printf("[WIFI] Connecting to %s", config.wifiSSID);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(config.deviceName);
    WiFi.begin(config.wifiSSID, config.wifiPass);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
        Serial.print('.');
        digitalWrite(PIN_EXTRA, !digitalRead(PIN_EXTRA));
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Connected  IP=%s  RSSI=%d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        wifiQuality = WQ_CONNECTED;
        setExtraMode(EL_SUCCESS);
    } else {
        Serial.println(F("[WIFI] Failed → setup mode"));
        setExtraMode(EL_ERROR);
        delay(500);
        startSetupMode();
    }
}

// ============================================================
//  WEB SERVER HANDLERS
// ============================================================
void handleRoot() {
    // Serves both captive portal (setup) and normal status page
    if (wifiSetupMode) {
        server.send(200, "text/html", F(
            "<!DOCTYPE html><html><head>"
            "<meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>SkyCore Setup</title>"
            "<style>"
            "body{font-family:Arial,sans-serif;max-width:460px;margin:30px auto;"
            "padding:20px;background:#0d0d0d;color:#eee}"
            "h1{color:#5bf;margin-bottom:4px}p.sub{color:#888;font-size:.9em;margin-top:0}"
            "label{display:block;margin-top:12px;font-size:.85em;color:#aaa}"
            "input{width:100%;padding:9px;margin-top:4px;background:#1a1a1a;"
            "color:#eee;border:1px solid #333;border-radius:5px;box-sizing:border-box}"
            "button{margin-top:18px;width:100%;background:#5bf;color:#000;"
            "border:none;padding:11px;border-radius:5px;font-size:1em;cursor:pointer}"
            "button:hover{background:#7df}"
            "</style></head><body>"
            "<h1>&#9729; SkyCore</h1>"
            "<p class='sub'>Connect your device to WiFi</p>"
            "<form action='/save' method='POST'>"
            "<label>WiFi Network</label>"
            "<input name='ssid' placeholder='Network name' required>"
            "<label>WiFi Password</label>"
            "<input name='pass' type='password' placeholder='Password'>"
            "<label>OpenWeatherMap API Key</label>"
            "<input name='apiKey' placeholder='Get free key at openweathermap.org' required>"
            "<label>City</label>"
            "<input name='city' placeholder='e.g. London' value='London'>"
            "<label>Device Name</label>"
            "<input name='name' placeholder='SkyCore' value='SkyCore'>"
            "<button type='submit'>Save &amp; Restart</button>"
            "</form></body></html>"));
    } else {
        handleStatus();
    }
}

void handleSave() {
    if (server.hasArg("ssid"))
        server.arg("ssid").toCharArray(config.wifiSSID, sizeof(config.wifiSSID));
    if (server.hasArg("pass"))
        server.arg("pass").toCharArray(config.wifiPass, sizeof(config.wifiPass));
    if (server.hasArg("apiKey"))
        server.arg("apiKey").toCharArray(config.apiKey, sizeof(config.apiKey));
    if (server.hasArg("city"))
        server.arg("city").toCharArray(config.city, sizeof(config.city));
    if (server.hasArg("name"))
        server.arg("name").toCharArray(config.deviceName, sizeof(config.deviceName));

    // Generate pair key on first save
    if (strlen(config.pairKey) == 0)
        generatePairKey().toCharArray(config.pairKey, sizeof(config.pairKey));

    saveConfig();

    server.send(200, "text/html", F(
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<style>body{font-family:Arial;background:#0d0d0d;color:#eee;"
        "text-align:center;padding:50px}</style></head><body>"
        "<h2 style='color:#5bf'>&#10003; Saved!</h2>"
        "<p>SkyCore is restarting and connecting to your WiFi.<br>"
        "This hotspot will disappear shortly.</p>"
        "</body></html>"));
    delay(1200);
    ESP.restart();
}

void handleStatus() {
    StaticJsonDocument<512> doc;
    doc["device"]       = config.deviceName;
    doc["pairKey"]      = config.pairKey;
    doc["firmwareVer"]  = "1.0.0";
    doc["uptime"]       = millis() / 1000;
    doc["freeHeap"]     = ESP.getFreeHeap();
    doc["rssi"]         = WiFi.RSSI();
    doc["ip"]           = WiFi.localIP().toString();
    doc["devMode"]      = devMode;
    doc["nightMode"]    = isNight;
    doc["apiStatus"]    = (int)apiStatus;
    doc["weatherValid"] = weather.valid;
    doc["temp"]         = weather.temperature;
    doc["humidity"]     = weather.humidity;
    doc["windSpeed"]    = weather.windSpeed;
    doc["aqi"]          = weather.aqi;
    doc["uvIndex"]      = weather.uvIndex;
    doc["weatherId"]    = weather.weatherId;

    String out;
    serializeJson(doc, out);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", out);
}

void handlePair() {
    StaticJsonDocument<128> req;
    if (deserializeJson(req, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* key = req["key"];
    if (!key || strcmp(key, config.pairKey) != 0) {
        server.send(401, "application/json", "{\"error\":\"Invalid pair key\"}");
        return;
    }
    StaticJsonDocument<128> res;
    res["success"]  = true;
    res["device"]   = config.deviceName;
    res["pairKey"]  = config.pairKey;
    String out;
    serializeJson(res, out);
    server.send(200, "application/json", out);
}

void handleApiConfig() {
    StaticJsonDocument<256> req;
    if (deserializeJson(req, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* key = req["key"];
    if (!key || strcmp(key, config.pairKey) != 0) {
        server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    if (req.containsKey("city"))   strlcpy(config.city,   req["city"],   sizeof(config.city));
    if (req.containsKey("apiKey")) strlcpy(config.apiKey, req["apiKey"], sizeof(config.apiKey));
    if (req.containsKey("lat"))    config.lat = req["lat"];
    if (req.containsKey("lon"))    config.lon = req["lon"];
    saveConfig();
    server.send(200, "application/json", "{\"success\":true}");
}

// ============================================================
//  WEATHER API
// ============================================================
void fetchWeather() {
    if (strlen(config.apiKey) == 0) {
        Serial.println(F("[API] No API key"));
        apiStatus = AS_FAILED;
        setExtraMode(EL_ERROR);
        return;
    }

    updateState      = US_UPDATING;
    updateStateStart = millis();
    Serial.println(F("[API] Fetching..."));

    WiFiClient client;
    HTTPClient http;

    // --- Current Weather ---
    String url = String(F("http://api.openweathermap.org/data/2.5/weather?q="))
               + config.city
               + F("&appid=") + config.apiKey
               + F("&units=metric");

    unsigned long t0 = millis();
    http.begin(client, url);
    int code = http.GET();
    unsigned long elapsed = millis() - t0;

    if (code == 200) {
        StaticJsonDocument<1024> doc;
        if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
            weather.temperature = doc["main"]["temp"]     | 0.0f;
            weather.humidity    = doc["main"]["humidity"] | 0.0f;
            weather.windSpeed   = doc["wind"]["speed"]    | 0.0f;
            weather.weatherId   = doc["weather"][0]["id"] | 800;
            weather.valid       = true;

            weatherCond = parseWeatherCondition(
                weather.weatherId, weather.temperature, weather.windSpeed);

            // Season from NTP month (fallback to June if NTP not ready)
            unsigned long epoch = timeClient.getEpochTime();
            int month = (epoch > 86400) ? ((epoch / 2592000) % 12) + 1 : 6;
            seasonCond = parseSeasonFromMonth(month);

            apiStatus  = (elapsed < 3000) ? AS_WORKING : AS_SLOW;
            inetQuality = (elapsed < 1000) ? IQ_GOOD :
                          (elapsed < 3000) ? IQ_SLOW  : IQ_BAD;

            Serial.printf("[API] T=%.1fC  H=%.0f%%  W=%.1fm/s  ID=%d  %lums\n",
                weather.temperature, weather.humidity,
                weather.windSpeed,   weather.weatherId, elapsed);
        } else {
            apiStatus = AS_FAILED;
        }
    } else {
        apiStatus = AS_FAILED;
        inetQuality = IQ_NONE;
        Serial.printf("[API] HTTP %d\n", code);
    }
    http.end();

    // --- UV Index ---
    String uvUrl = String(F("http://api.openweathermap.org/data/2.5/uvi?lat="))
                 + config.lat + F("&lon=") + config.lon
                 + F("&appid=") + config.apiKey;
    http.begin(client, uvUrl);
    if (http.GET() == 200) {
        StaticJsonDocument<128> uvDoc;
        if (deserializeJson(uvDoc, http.getString()) == DeserializationError::Ok)
            weather.uvIndex = uvDoc["value"] | 0.0f;
    }
    http.end();

    // --- Air Quality Index ---
    String aqiUrl = String(F("http://api.openweathermap.org/data/2.5/air_pollution?lat="))
                  + config.lat + F("&lon=") + config.lon
                  + F("&appid=") + config.apiKey;
    http.begin(client, aqiUrl);
    if (http.GET() == 200) {
        StaticJsonDocument<512> aqiDoc;
        if (deserializeJson(aqiDoc, http.getString()) == DeserializationError::Ok)
            weather.aqi = aqiDoc["list"][0]["main"]["aqi"] | 1;
    }
    http.end();

    // Update WiFi signal quality
    int rssi = WiFi.RSSI();
    wifiQuality = (rssi > -60)  ? WQ_CONNECTED  :
                  (rssi > -75)  ? WQ_WEAK        :
                  (rssi > -90)  ? WQ_VERY_WEAK   : WQ_DISCONNECTED;

    // Update time-based modes
    unsigned long epoch = timeClient.getEpochTime();
    if (epoch > 86400) {
        isNight    = isNightTime();
        int dow    = timeClient.getDay();
        yellowMode = parseDayOfWeek(dow);
        warmMode   = parseMoonPhase(epoch, isNight);
    }

    if (apiStatus == AS_FAILED) {
        updateState = US_FAIL;
        setExtraMode(EL_ERROR);
        buzzerWarning();
    } else {
        updateState = US_SUCCESS;
        setExtraMode(EL_SUCCESS);
    }
    updateStateStart = millis();
}

// Map OWM condition ID + environmental data to internal WeatherCond
WeatherCond parseWeatherCondition(int id, float temp, float wind) {
    if (id >= 200 && id < 300) return (wind > 10) ? WC_THUNDERSTORM : WC_HEAVY_RAIN;
    if (id >= 300 && id < 400) return WC_RAIN;
    if (id >= 500 && id < 600) return (id >= 502) ? WC_HEAVY_RAIN : WC_RAIN;
    if (id >= 600 && id < 700) return WC_COLD;
    if (id >= 700 && id < 800) return WC_CLOUDY;
    if (id == 800) {
        if (temp > 38) return WC_HEAT_WARNING;
        if (temp > 30) return WC_HOT;
        if (temp < 10) return WC_COLD;
        return WC_CLEAR;
    }
    return (id <= 802) ? WC_CLOUDY : WC_CLOUDY;
}

SeasonCond parseSeasonFromMonth(int month) {
    if (month >= 3  && month <= 5)  return SC_SUMMER;
    if (month >= 6  && month <= 9)  return SC_MONSOON;
    return SC_WINTER;
}

// Approximate lunar phase from epoch (29.53-day synodic period)
WarmLedMode parseMoonPhase(unsigned long epoch, bool nightTime) {
    if (!nightTime) return WL_DAY;
    // Reference new moon: 2000-01-06T18:14 UTC ≈ epoch 947182440
    const unsigned long SYNODIC = 2551443UL;
    long age = (long)((epoch - 947182440UL) % SYNODIC);
    if (age < 0) age += SYNODIC;
    float phase = (float)age / (float)SYNODIC; // 0=new, 0.5=full
    if (phase < 0.05f || phase > 0.95f) return WL_NEW_MOON;
    if (phase > 0.45f && phase < 0.55f) return WL_FULL_MOON;
    return WL_CRESCENT;
}

YellowLedMode parseDayOfWeek(int dow) {
    if (dow == 0) return YL_SUNDAY;
    if (dow == 6) return YL_SATURDAY;
    return YL_WEEKDAY;
}

// ============================================================
//  LED CONTROL  (non-blocking, millis-based)
// ============================================================
void updateLEDs() {
    unsigned long now = millis();

    // Clear secondary LEDs; RED is driven by the display-mode toggle
    digitalWrite(PIN_ORANGE, LOW);
    digitalWrite(PIN_GREEN,  LOW);
    digitalWrite(PIN_BLUE,   LOW);

    if (displayMode == MODE_SEASON) {
        // RED is HIGH (set by toggle logic)
        switch (seasonCond) {
            case SC_SUMMER:  digitalWrite(PIN_ORANGE, HIGH); break;
            case SC_WINTER:  digitalWrite(PIN_GREEN,  HIGH); break;
            case SC_MONSOON: digitalWrite(PIN_BLUE,   HIGH); break;
        }
    } else {
        // RED is LOW (set by toggle logic)
        switch (weatherCond) {
            case WC_HOT:
                digitalWrite(PIN_ORANGE, HIGH);
                break;
            case WC_COLD:
                digitalWrite(PIN_GREEN, HIGH);
                break;
            case WC_RAIN:
                digitalWrite(PIN_BLUE, HIGH);
                break;
            case WC_CLOUDY:
                digitalWrite(PIN_BLUE, (now / 1000) % 2 ? HIGH : LOW); // 0.5 Hz
                break;
            case WC_HEAVY_RAIN:
                digitalWrite(PIN_BLUE, (now / 100) % 2 ? HIGH : LOW);  // 5 Hz
                break;
            case WC_HEAT_WARNING:
                digitalWrite(PIN_ORANGE, (now / 100) % 2 ? HIGH : LOW); // 5 Hz
                break;
            case WC_THUNDERSTORM:
                // RED + BLUE slow blink – RED is normally LOW; drive it manually here
                {
                    bool st = (now / 2000) % 2;
                    digitalWrite(PIN_RED,  st ? HIGH : LOW);
                    digitalWrite(PIN_BLUE, st ? HIGH : LOW);
                }
                break;
            case WC_CLEAR:
            default:
                break;
        }
    }
}

void updateWarmLed() {
    unsigned long now = millis();
    switch (warmMode) {
        case WL_DAY:
            analogWrite(PIN_WARM, 70);    // Dim  ~27%
            break;
        case WL_NIGHT:
            analogWrite(PIN_WARM, 255);   // Bright 100%
            break;
        case WL_CRESCENT:
            digitalWrite(PIN_WARM, (now / 2000) % 2 ? HIGH : LOW); // 0.25 Hz
            break;
        case WL_FULL_MOON:
            digitalWrite(PIN_WARM, (now / 1000) % 2 ? HIGH : LOW); // 0.5 Hz
            break;
        case WL_NEW_MOON:
            digitalWrite(PIN_WARM, (now / 200)  % 2 ? HIGH : LOW); // 2.5 Hz
            break;
    }
}

void updateYellowLed() {
    unsigned long now = millis();
    switch (yellowMode) {
        case YL_WEEKDAY:
            digitalWrite(PIN_YELLOW, LOW);
            break;
        case YL_SATURDAY:
            digitalWrite(PIN_YELLOW, (now / 1000) % 2 ? HIGH : LOW); // slow
            break;
        case YL_SUNDAY:
            digitalWrite(PIN_YELLOW, (now / 200) % 2 ? HIGH : LOW);  // fast
            break;
        case YL_FESTIVAL:
            digitalWrite(PIN_YELLOW, HIGH);
            break;
    }
}

void updateExtraLed() {
    unsigned long now = millis();
    static unsigned long errTimer   = 0;
    static int           errPhase   = 0; // counts half-periods (on/off)
    static bool          errState   = false;

    switch (extraMode) {
        case EL_LOADING:
            digitalWrite(PIN_EXTRA, (now / 100) % 2 ? HIGH : LOW);
            break;
        case EL_SUCCESS:
            digitalWrite(PIN_EXTRA, HIGH);
            break;
        case EL_ERROR:
            // 3 blinks (6 transitions) then 1 s pause
            if (errPhase < 6) {
                if (now - errTimer > 200) {
                    errTimer = now;
                    errState = !errState;
                    digitalWrite(PIN_EXTRA, errState ? HIGH : LOW);
                    errPhase++;
                }
            } else {
                digitalWrite(PIN_EXTRA, LOW);
                if (now - errTimer > 1000) {
                    errPhase = 0;
                    errState = false;
                    errTimer = now;
                }
            }
            break;
    }
}

void setExtraMode(ExtraLedMode mode) {
    extraMode = mode;
}

// ============================================================
//  NEOPIXEL SYSTEM
// ============================================================
void updatePixels() {
    if (wifiSetupMode) return; // handled in loop()

    unsigned long now = millis();

    // ── Recovery Mode: alternate blue / red ──────────────────
    if (recoveryMode) {
        bool alt = (now / 500) % 2;
        pixels.setPixelColor(0, alt ? pixels.Color(0, 0, 200) : pixels.Color(200, 0, 0));
        pixels.setPixelColor(1, alt ? pixels.Color(200, 0, 0) : pixels.Color(0, 0, 200));
        pixels.show();
        return;
    }

    // ── Pixel 1: Environment Status ───────────────────────────
    uint32_t p1 = pixels.Color(0, 20, 20); // idle default: dim cyan

    if (weatherCond == WC_THUNDERSTORM) {
        // Thunderstorm: highest priority — purple fast pulse
        int v = (int)(128 + 127 * sin(now / 150.0));
        p1 = pixels.Color(v, 0, v);

    } else if (weather.aqi >= 5) {
        // AQI very dangerous: fast red blink
        p1 = ((now / 100) % 2) ? pixels.Color(255, 0, 0) : pixels.Color(0, 0, 0);

    } else if (weather.aqi == 4) {
        // AQI dangerous: slow red blink
        p1 = ((now / 500) % 2) ? pixels.Color(255, 0, 0) : pixels.Color(0, 0, 0);

    } else if (weather.aqi == 3) {
        p1 = pixels.Color(255, 80, 0);   // Bad: orange

    } else if (weather.aqi == 2) {
        p1 = pixels.Color(200, 200, 0);  // Moderate: yellow

    } else {
        // AQI good (1) – show humidity
        float hum = weather.humidity;
        if (hum < 30) {
            int v = (int)(40 + 30 * sin(now / 1000.0));
            p1 = pixels.Color(0, 0, v);                           // Low: blue dim pulse
        } else if (hum < 60) {
            p1 = pixels.Color(0, 0, 150);                         // Medium: blue solid
        } else if (hum < 80) {
            int v = (int)(100 + 80 * sin(now / 2000.0));
            p1 = pixels.Color(v, 0, v);                           // High: purple slow pulse
        } else {
            int v = (int)(100 + 80 * sin(now / 400.0));
            p1 = pixels.Color(v, 0, v);                           // Very high: purple fast
        }

        // Wind – overrides humidity when strong (cyan family)
        float wind = weather.windSpeed;
        if (wind > 17) {
            p1 = ((now / 100) % 2) ? pixels.Color(0, 200, 200) : pixels.Color(0, 0, 0);
        } else if (wind > 10) {
            int v = (int)(120 + 80 * sin(now / 400.0));
            p1 = pixels.Color(0, v, v);
        } else if (wind > 5) {
            int v = (int)(80 + 50 * sin(now / 800.0));
            p1 = pixels.Color(0, v, v);
        }

        // UV – overrides when extreme (orange)
        float uv = weather.uvIndex;
        if (uv >= 11) {
            p1 = ((now / 150) % 2) ? pixels.Color(255, 120, 0) : pixels.Color(0, 0, 0);
        } else if (uv >= 8) {
            int v = (int)(160 + 80 * sin(now / 500.0));
            p1 = pixels.Color(v, v / 2, 0);
        } else if (uv >= 3) {
            p1 = pixels.Color(80, 40, 0); // dim orange
        }
    }

    pixels.setPixelColor(0, p1);

    // ── Pixel 2: Device Status ────────────────────────────────
    uint32_t p2 = pixels.Color(0, 0, 30); // idle default: dim blue

    if (devMode) {
        // Rainbow cycle
        int hue = (int)((now / 10) % 256);
        p2 = colorFromHSV(hue, 255, 200);

    } else if (updateState == US_UPDATING) {
        p2 = ((now / 100) % 2) ? pixels.Color(255, 255, 255) : pixels.Color(0, 0, 0);

    } else if (updateState == US_SUCCESS) {
        p2 = pixels.Color(0, 200, 0);

    } else if (updateState == US_FAIL) {
        p2 = ((now / 300) % 2) ? pixels.Color(200, 0, 0) : pixels.Color(0, 0, 0);

    } else {
        // Normal operation: reflect WiFi + internet + API health
        switch (wifiQuality) {
            case WQ_CONNECTED: {
                if (apiStatus == AS_FAILED) {
                    // API failed: red 3-blink pattern
                    int slot = (now / 300) % 8;
                    p2 = (slot < 6 && slot % 2 == 0)
                         ? pixels.Color(200, 0, 0) : pixels.Color(0, 0, 0);
                } else if (apiStatus == AS_SLOW) {
                    int v = (int)(100 + 80 * sin(now / 600.0));
                    p2 = pixels.Color(v, v, 0); // yellow pulse
                } else {
                    switch (inetQuality) {
                        case IQ_GOOD: {
                            int v = (int)(80 + 60 * sin(now / 800.0));
                            p2 = pixels.Color(0, 0, v);  // blue pulse
                            break;
                        }
                        case IQ_SLOW: {
                            int v = (int)(80 + 60 * sin(now / 800.0));
                            p2 = pixels.Color(v / 2, 0, v); // purple pulse
                            break;
                        }
                        case IQ_BAD:
                            p2 = ((now / 400) % 2)
                                 ? pixels.Color(200, 80, 0) : pixels.Color(0, 0, 0);
                            break;
                        default:
                            p2 = ((now / 500) % 2)
                                 ? pixels.Color(200, 0, 0) : pixels.Color(0, 0, 0);
                    }
                }
                break;
            }
            case WQ_WEAK: {
                int v = (int)(80 + 60 * sin(now / 600.0));
                p2 = pixels.Color(v, v, 0); // yellow pulse
                break;
            }
            case WQ_VERY_WEAK: {
                int v = (int)(80 + 60 * sin(now / 600.0));
                p2 = pixels.Color(v, v / 2, 0); // orange pulse
                break;
            }
            case WQ_DISCONNECTED:
                p2 = ((now / 500) % 2) ? pixels.Color(200, 0, 0) : pixels.Color(0, 0, 0);
                break;
        }
    }

    pixels.setPixelColor(1, p2);
    pixels.show();
}

// ============================================================
//  BUTTON HANDLING
// ============================================================
void handleButton() {
    unsigned long now    = millis();
    bool          rawNow = digitalRead(PIN_BUTTON); // HIGH = pressed (GPIO16 pull-down)

    // Debounce
    if (rawNow != btn.lastRaw) {
        btn.lastChange = now;
        btn.lastRaw    = rawNow;
    }
    if (now - btn.lastChange < BUTTON_DEBOUNCE_MS) return;

    bool pressed = (rawNow == HIGH);

    // Rising edge → press start
    if (pressed && !btn.lastDebounced) {
        btn.pressStart    = now;
        btn.holdFired     = false;
        btn.lastDebounced = true;
    }

    // Hold detection while button is down
    if (pressed && btn.lastDebounced && !btn.holdFired) {
        if (now - btn.pressStart >= BUTTON_HOLD_MS) {
            btn.holdFired  = true;
            btn.pressCount = 0;
            Serial.println(F("[BTN] Hold → WiFi reset"));
            blinkLed(PIN_EXTRA, 6, 80, 80);
            LittleFS.remove(CONFIG_FILE);
            ESP.restart();
        }
    }

    // Falling edge → count press
    if (!pressed && btn.lastDebounced) {
        btn.lastDebounced = false;
        if (!btn.holdFired) {
            btn.pressCount++;
            btn.lastRelease = now;
        }
    }

    // Process accumulated presses after quiet window
    if (btn.pressCount > 0 && !pressed &&
        now - btn.lastRelease > MULTI_PRESS_WINDOW_MS) {
        processPressCount(btn.pressCount);
        btn.pressCount = 0;
    }
}

void processPressCount(int count) {
    Serial.printf("[BTN] %d press(es)\n", count);
    switch (count) {
        case 1:
            Serial.println(F("[BTN] Force weather update"));
            lastWeatherUpdate = 0;
            break;
        case 2:
            Serial.println(F("[BTN] Display mode toggle"));
            // Manually advance the weather condition for demo / manual browse
            weatherCond = (WeatherCond)((int(weatherCond) + 1) % 8);
            break;
        case 5:
            devMode = !devMode;
            Serial.printf("[BTN] Developer mode: %s\n", devMode ? "ON" : "OFF");
            if (!devMode) {
                pixels.setPixelColor(1, pixels.Color(0, 0, 30));
                pixels.show();
            }
            break;
        default:
            break;
    }
}

// ============================================================
//  BUZZER
// ============================================================
void buzzerTone(int freq, int durMs) {
    if (isNight) return; // Auto-mute 22:00 – 07:00
    tone(PIN_BUZZER, freq, durMs);
}

void buzzerWarning() {
    if (isNight) return;
    buzzerTone(1000, 200);
    delay(100);
    buzzerTone(1500, 200);
    delay(100);
    buzzerTone(1000, 200);
}

// ============================================================
//  TIME & NIGHT MODE
// ============================================================
bool isNightTime() {
    if (timeClient.getEpochTime() < 86400) return false;
    int h = timeClient.getHours();
    return (h >= NIGHT_START_HOUR || h < NIGHT_END_HOUR);
}

void updateNightMode() {
    isNight = isNightTime();
    // Refresh moon / day modes
    unsigned long epoch = timeClient.getEpochTime();
    if (epoch > 86400) {
        warmMode   = parseMoonPhase(epoch, isNight);
        yellowMode = parseDayOfWeek(timeClient.getDay());
    }
}

// ============================================================
//  OTA
// ============================================================
void setupOTA() {
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setHostname(config.deviceName);

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[OTA] Start: " + type);
        updateState      = US_UPDATING;
        updateStateStart = millis();
        setExtraMode(EL_LOADING);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println(F("[OTA] Done"));
        updateState      = US_SUCCESS;
        updateStateStart = millis();
        setExtraMode(EL_SUCCESS);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] %3u%%\r", progress / (total / 100));
    });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("[OTA] Error[%u]\n", err);
        updateState      = US_FAIL;
        updateStateStart = millis();
        setExtraMode(EL_ERROR);
    });

    ArduinoOTA.begin();
    Serial.printf("[OTA] Ready  Host=%s\n", config.deviceName);
}

// ============================================================
//  DEVELOPER MODE
// ============================================================
void devModeLoop() {
    Serial.println(F("=== DEV MODE ==========================="));
    Serial.printf("  Device    : %s\n",   config.deviceName);
    Serial.printf("  Pair Key  : %s\n",   config.pairKey);
    Serial.printf("  Free RAM  : %u B\n", ESP.getFreeHeap());
    Serial.printf("  Uptime    : %lu s\n", millis() / 1000);
    Serial.printf("  WiFi RSSI : %d dBm\n", WiFi.RSSI());
    Serial.printf("  WiFi IP   : %s\n",   WiFi.localIP().toString().c_str());
    Serial.printf("  API Status: %d\n",   (int)apiStatus);
    Serial.printf("  Temp      : %.1f C\n",  weather.temperature);
    Serial.printf("  Humidity  : %.0f %%\n", weather.humidity);
    Serial.printf("  Wind      : %.1f m/s\n",weather.windSpeed);
    Serial.printf("  AQI       : %d\n",   weather.aqi);
    Serial.printf("  UV Index  : %.1f\n", weather.uvIndex);
    Serial.printf("  WeatherID : %d\n",   weather.weatherId);
    Serial.printf("  Night     : %s\n",   isNight ? "YES" : "NO");
    Serial.printf("  Dev Mode  : ON\n");
    Serial.println(F("========================================"));
}

// ============================================================
//  HELPERS
// ============================================================
String generatePairKey() {
    // Derive a unique SKY-XXXX-XXXX key from chip IDs and time
    uint32_t a = ESP.getChipId()      ^ (uint32_t)(millis()  * 1234567UL);
    uint32_t b = ESP.getFlashChipId() ^ (uint32_t)(micros()  * 987654UL);
    char key[20];
    snprintf(key, sizeof(key), "SKY-%04X-%04X",
             (unsigned)(a & 0xFFFF), (unsigned)(b & 0xFFFF));
    return String(key);
}

void blinkLed(int pin, int times, int onMs, int offMs) {
    for (int i = 0; i < times; i++) {
        digitalWrite(pin, HIGH);
        delay(onMs);
        digitalWrite(pin, LOW);
        delay(offMs);
    }
}

// HSV → NeoPixel Color  (hue 0-255, sat 0-255, val 0-255)
uint32_t colorFromHSV(int hue, int sat, int val) {
    hue = hue % 256;
    int region    = hue / 43;
    int remainder = (hue - (region * 43)) * 6;
    int p = (val * (255 - sat)) >> 8;
    int q = (val * (255 - ((sat * remainder) >> 8))) >> 8;
    int t = (val * (255 - ((sat * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
        case 0:  return pixels.Color(val, t,   p);
        case 1:  return pixels.Color(q,   val, p);
        case 2:  return pixels.Color(p,   val, t);
        case 3:  return pixels.Color(p,   q,   val);
        case 4:  return pixels.Color(t,   p,   val);
        default: return pixels.Color(val, p,   q);
    }
}
