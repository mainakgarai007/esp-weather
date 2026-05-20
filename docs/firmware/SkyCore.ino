/*
 * SkyCore ESP8266 Firmware v1.1.0
 * Production-Ready Weather & Environment Display System
 * Board: ESP8266 NodeMCU
 *
 * Required Libraries (install via Arduino Library Manager):
 *   - ESP8266WiFi          (built-in with ESP8266 Arduino core)
 *   - ESP8266WebServer     (built-in with ESP8266 Arduino core)
 *   - ArduinoOTA           (built-in with ESP8266 Arduino core)
 *   - NTPClient            (by Fabrice Weinberg, v3.2.1+)
 *   - WiFiUdp              (built-in with ESP8266 Arduino core)
 *   - ArduinoJson          (by Benoit Blanchon, v6.x or v7.x)
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
#define FIRMWARE_VERSION      "1.1.0"
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

#define DEFAULT_BRIGHTNESS    80

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
    float feelsLike   = 0.0f;
    float humidity    = 0.0f;
    float pressure    = 0.0f;
    float windSpeed   = 0.0f;
    float visibility  = 0.0f;  // km
    float uvIndex     = 0.0f;
    int   weatherId   = 800;
    int   aqi         = 1;
    bool  valid       = false;
    char  condition[32]   = "Clear";
    char  description[64] = "Clear sky";
    char  windDir[8]      = "N";
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
    uint8_t brightness  = DEFAULT_BRIGHTNESS;
    bool animations     = true;
    bool buzzerEnabled  = true;
    bool nightMute      = true;
    bool holiday        = false;
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

struct PixelState {
    uint8_t      r = 0;
    uint8_t      g = 0;
    uint8_t      b = 0;
    const char*  effect  = "solid";
    const char*  meaning = "Idle";
    const char*  reason  = "Idle";
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
PixelState      pixel1State;
PixelState      pixel2State;

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

uint32_t errorCount = 0;

unsigned long lastWeatherUpdate  = 0;
unsigned long lastToggle         = 0;
unsigned long lastNtpSync        = 0;
unsigned long lastPixelUpdate    = 0;
unsigned long lastDevPrint       = 0;
unsigned long updateStateStart   = 0;
unsigned long apiFlashUntil      = 0;

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
const char*   weatherLabel(WeatherCond cond);
const char*   seasonLabel(SeasonCond cond);
const char*   warmLabel(WarmLedMode mode);
const char*   yellowLabel(YellowLedMode mode);
const char*   extraLabel(ExtraLedMode mode);
const char*   wifiQualityLabel(WiFiQuality quality);
const char*   internetQualityLabel(InternetQuality quality);
const char*   apiStatusLabel(ApiStatus status);
const char*   updateStateLabel(UpdateState state);
String        windDirFromDeg(int deg);
void          setPixelState(PixelState &state, uint8_t r, uint8_t g, uint8_t b,
                            const char* effect, const char* meaning, const char* reason);
void          handleRoot();
void          handleSave();
void          handleStatus();
void          handlePair();
void          handleApiConfig();
void          handleRestart();
void          handleReset();
void          handleWifiReset();

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n\n=== SkyCore v1.1.0 Booting ==="));

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

    analogWriteRange(255);

    // NeoPixel boot animation (fast rainbow)
    pixels.begin();
    pixels.setBrightness(DEFAULT_BRIGHTNESS);
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
    pixels.setBrightness(config.brightness);

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
        server.on("/api/restart", HTTP_POST, handleRestart);
        server.on("/api/reset",   HTTP_POST, handleReset);
        server.on("/api/wifi-reset", HTTP_POST, handleWifiReset);
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
            setPixelState(pixel1State, breath / 2, 0, breath, "breathing", "WiFi Setup", "Portal mode active");
            setPixelState(pixel2State, breath / 2, 0, breath, "breathing", "WiFi Setup", "Portal mode active");
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
        config.brightness = doc["brightness"] | DEFAULT_BRIGHTNESS;
        config.animations = doc["animations"] | true;
        config.buzzerEnabled = doc["buzzer"] | true;
        config.nightMute = doc["nightMute"] | true;
        config.holiday = doc["holiday"] | false;
        if (config.brightness == 0 || config.brightness > 255) config.brightness = DEFAULT_BRIGHTNESS;
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
    doc["brightness"] = config.brightness;
    doc["animations"] = config.animations;
    doc["buzzer"]     = config.buzzerEnabled;
    doc["nightMute"]  = config.nightMute;
    doc["holiday"]    = config.holiday;
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
        errorCount++;
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
            "<label>SkyCore Pairing Key</label>"
            "<input name='pairKey' placeholder='SKY-XXXX-XXXX' maxlength='15'>"
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
    if (server.hasArg("pairKey")) {
        server.arg("pairKey").toCharArray(config.pairKey, sizeof(config.pairKey));
    }

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
    StaticJsonDocument<1536> doc;

    JsonObject device = doc.createNestedObject("device");
    device["name"]        = config.deviceName;
    device["pairKey"]     = config.pairKey;
    device["firmware"]    = FIRMWARE_VERSION;
    device["uptime"]      = millis() / 1000;
    device["freeHeap"]    = ESP.getFreeHeap();
    device["rssi"]        = WiFi.RSSI();
    device["ip"]          = WiFi.localIP().toString();
    device["devMode"]     = devMode;
    device["recovery"]    = recoveryMode;
    device["wifiSetup"]   = wifiSetupMode;
    device["nightMode"]   = isNight;
    device["errorCount"]  = errorCount;
    device["weatherValid"]= weather.valid;

    JsonObject network = doc.createNestedObject("network");
    network["wifiQuality"]     = wifiQualityLabel(wifiQuality);
    network["internetQuality"] = internetQualityLabel(inetQuality);
    network["apiStatus"]       = apiStatusLabel(apiStatus);

    JsonObject weatherObj = doc.createNestedObject("weather");
    weatherObj["temp"]        = weather.temperature;
    weatherObj["feelsLike"]   = weather.feelsLike;
    weatherObj["humidity"]    = weather.humidity;
    weatherObj["pressure"]    = weather.pressure;
    weatherObj["windSpeed"]   = weather.windSpeed;
    weatherObj["windDir"]     = weather.windDir;
    weatherObj["aqi"]         = weather.aqi;
    weatherObj["uvIndex"]     = weather.uvIndex;
    weatherObj["visibility"]  = weather.visibility;
    weatherObj["weatherId"]   = weather.weatherId;
    weatherObj["condition"]   = weather.condition;
    weatherObj["description"] = weather.description;
    weatherObj["season"]      = seasonLabel(seasonCond);
    weatherObj["moonPhase"]   = warmLabel(warmMode);
    weatherObj["dayNight"]    = isNight ? "Night" : "Day";
    weatherObj["weekend"]     = (yellowMode == YL_SATURDAY || yellowMode == YL_SUNDAY);
    weatherObj["holiday"]     = config.holiday;
    weatherObj["weatherMode"] = weatherLabel(weatherCond);

    JsonObject leds = doc.createNestedObject("leds");
    leds["displayMode"] = (displayMode == MODE_WEATHER) ? "weather" : "season";
    leds["weather"]     = weatherLabel(weatherCond);
    leds["season"]      = seasonLabel(seasonCond);
    leds["warmMode"]    = warmLabel(warmMode);
    leds["yellowMode"]  = yellowLabel(yellowMode);
    leds["extraMode"]   = extraLabel(extraMode);
    leds["brightness"]  = config.brightness;
    leds["animations"]  = config.animations;
    leds["buzzer"]      = config.buzzerEnabled;
    leds["nightMute"]   = config.nightMute;

    JsonObject pixelsObj = doc.createNestedObject("pixels");
    JsonObject p1 = pixelsObj.createNestedObject("pixel1");
    JsonObject p2 = pixelsObj.createNestedObject("pixel2");
    char p1Hex[8];
    char p2Hex[8];
    snprintf(p1Hex, sizeof(p1Hex), "#%02X%02X%02X", pixel1State.r, pixel1State.g, pixel1State.b);
    snprintf(p2Hex, sizeof(p2Hex), "#%02X%02X%02X", pixel2State.r, pixel2State.g, pixel2State.b);
    p1["color"]   = p1Hex;
    p1["effect"]  = pixel1State.effect;
    p1["meaning"] = pixel1State.meaning;
    p1["reason"]  = pixel1State.reason;
    p2["color"]   = p2Hex;
    p2["effect"]  = pixel2State.effect;
    p2["meaning"] = pixel2State.meaning;
    p2["reason"]  = pixel2State.reason;

    JsonObject status = doc.createNestedObject("status");
    status["updateState"] = updateStateLabel(updateState);

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
    StaticJsonDocument<512> req;
    if (deserializeJson(req, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* key = req["key"];
    if (!key || strcmp(key, config.pairKey) != 0) {
        server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    if (req.containsKey("city"))       strlcpy(config.city,   req["city"],   sizeof(config.city));
    if (req.containsKey("apiKey"))     strlcpy(config.apiKey, req["apiKey"], sizeof(config.apiKey));
    if (req.containsKey("lat"))        config.lat = req["lat"];
    if (req.containsKey("lon"))        config.lon = req["lon"];
    if (req.containsKey("deviceName")) {
        strlcpy(config.deviceName, req["deviceName"], sizeof(config.deviceName));
        WiFi.hostname(config.deviceName);
    }
    if (req.containsKey("pairKey")) {
        strlcpy(config.pairKey, req["pairKey"], sizeof(config.pairKey));
    }
    if (req.containsKey("brightness")) {
        int b = req["brightness"];
        if (b < 5) b = 5;
        if (b > 255) b = 255;
        config.brightness = (uint8_t)b;
        pixels.setBrightness(config.brightness);
    }
    if (req.containsKey("animations")) config.animations = req["animations"];
    if (req.containsKey("buzzer"))     config.buzzerEnabled = req["buzzer"];
    if (req.containsKey("nightMute"))  config.nightMute = req["nightMute"];
    if (req.containsKey("holiday")) {
        config.holiday = req["holiday"];
        yellowMode = config.holiday ? YL_FESTIVAL : parseDayOfWeek(timeClient.getDay());
    }
    saveConfig();
    server.send(200, "application/json", "{\"success\":true}");
}

void handleRestart() {
    StaticJsonDocument<64> req;
    if (deserializeJson(req, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* key = req["key"];
    if (!key || strcmp(key, config.pairKey) != 0) {
        server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    server.send(200, "application/json", "{\"success\":true}");
    delay(300);
    ESP.restart();
}

void handleReset() {
    StaticJsonDocument<64> req;
    if (deserializeJson(req, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* key = req["key"];
    if (!key || strcmp(key, config.pairKey) != 0) {
        server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    LittleFS.remove(CONFIG_FILE);
    server.send(200, "application/json", "{\"success\":true}");
    delay(300);
    ESP.restart();
}

void handleWifiReset() {
    StaticJsonDocument<64> req;
    if (deserializeJson(req, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* key = req["key"];
    if (!key || strcmp(key, config.pairKey) != 0) {
        server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    config.wifiSSID[0] = '\\0';
    config.wifiPass[0] = '\\0';
    saveConfig();
    server.send(200, "application/json", "{\"success\":true}");
    delay(300);
    ESP.restart();
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
            weather.feelsLike   = doc["main"]["feels_like"] | weather.temperature;
            weather.humidity    = doc["main"]["humidity"] | 0.0f;
            weather.pressure    = doc["main"]["pressure"] | 0.0f;
            weather.windSpeed   = doc["wind"]["speed"]    | 0.0f;
            weather.weatherId   = doc["weather"][0]["id"] | 800;
            weather.visibility  = (doc["visibility"] | 10000) / 1000.0f;
            int windDeg         = doc["wind"]["deg"] | 0;
            String dir          = windDirFromDeg(windDeg);
            dir.toCharArray(weather.windDir, sizeof(weather.windDir));
            strlcpy(weather.condition,
                    doc["weather"][0]["main"] | "Clear",
                    sizeof(weather.condition));
            strlcpy(weather.description,
                    doc["weather"][0]["description"] | "Clear sky",
                    sizeof(weather.description));
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
            apiFlashUntil = millis() + ((apiStatus == AS_WORKING) ? 500 : 900);

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
        yellowMode = config.holiday ? YL_FESTIVAL : parseDayOfWeek(dow);
        warmMode   = parseMoonPhase(epoch, isNight);
    }

    if (apiStatus == AS_FAILED) {
        errorCount++;
        updateState = US_FAIL;
        setExtraMode(EL_ERROR);
        buzzerWarning();
    } else {
        updateState = US_SUCCESS;
        setExtraMode(EL_SUCCESS);
        if (weatherCond == WC_HEAVY_RAIN || weatherCond == WC_THUNDERSTORM ||
            weatherCond == WC_HEAT_WARNING) {
            buzzerWarning();
        }
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
    const unsigned long SYNODIC = 2551443UL; // 29.53059 days × 86400 s/day
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
    bool anim = config.animations;

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
                digitalWrite(PIN_BLUE, anim ? ((now / 1000) % 2 ? HIGH : LOW) : HIGH);
                break;
            case WC_HEAVY_RAIN:
                digitalWrite(PIN_BLUE, anim ? ((now / 100) % 2 ? HIGH : LOW) : HIGH);
                break;
            case WC_HEAT_WARNING:
                digitalWrite(PIN_ORANGE, anim ? ((now / 100) % 2 ? HIGH : LOW) : HIGH);
                break;
            case WC_THUNDERSTORM:
                // RED + BLUE slow blink – RED is normally LOW; drive it manually here
                {
                    bool st = anim ? (now / 2000) % 2 : true;
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
    bool anim = config.animations;
    switch (warmMode) {
        case WL_DAY:
            analogWrite(PIN_WARM, 70);    // Dim  ~27%
            break;
        case WL_NIGHT:
            analogWrite(PIN_WARM, 255);   // Bright 100%
            break;
        case WL_CRESCENT:
            digitalWrite(PIN_WARM, anim ? ((now / 2000) % 2 ? HIGH : LOW) : HIGH);
            break;
        case WL_FULL_MOON:
            digitalWrite(PIN_WARM, anim ? ((now / 1000) % 2 ? HIGH : LOW) : HIGH);
            break;
        case WL_NEW_MOON:
            digitalWrite(PIN_WARM, anim ? ((now / 200)  % 2 ? HIGH : LOW) : HIGH);
            break;
    }
}

void updateYellowLed() {
    unsigned long now = millis();
    bool anim = config.animations;
    switch (yellowMode) {
        case YL_WEEKDAY:
            digitalWrite(PIN_YELLOW, LOW);
            break;
        case YL_SATURDAY:
            digitalWrite(PIN_YELLOW, anim ? ((now / 1000) % 2 ? HIGH : LOW) : HIGH);
            break;
        case YL_SUNDAY:
            digitalWrite(PIN_YELLOW, anim ? ((now / 200) % 2 ? HIGH : LOW) : HIGH);
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
        setPixelState(pixel1State, alt ? 0 : 200, 0, alt ? 200 : 0,
                      "alternate", "Recovery Mode", "Recovery boot active");
        setPixelState(pixel2State, alt ? 200 : 0, 0, alt ? 0 : 200,
                      "alternate", "Recovery Mode", "Recovery boot active");
        pixels.show();
        return;
    }

    bool anim = config.animations;
    auto blink = [&](unsigned long periodMs) -> bool {
        return anim ? ((now / periodMs) % 2) : true;
    };
    auto pulse = [&](int base, int amp, float speed) -> int {
        int val = !anim ? (base + amp) : (int)(base + amp * sin(now / speed));
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        return val;
    };

    // ── Pixel 1: Environment Status ───────────────────────────
    uint8_t p1r = 0, p1g = 20, p1b = 20;
    const char* p1Effect = "solid";
    const char* p1Meaning = "Environment Normal";
    const char* p1Reason = "Normal conditions";

    if (weatherCond == WC_THUNDERSTORM) {
        int v = pulse(120, 120, 120.0f);
        p1r = v; p1g = 0; p1b = v;
        p1Effect = anim ? "rapid-pulse" : "solid";
        p1Meaning = "Thunderstorm";
        p1Reason = "Severe thunderstorm detected";

    } else if (weather.aqi >= 5) {
        bool on = blink(100);
        p1r = on ? 255 : 0; p1g = 0; p1b = 0;
        p1Effect = anim ? "fast-blink" : "solid";
        p1Meaning = "AQI Very Dangerous";
        p1Reason = "Air quality hazardous";

    } else if (weather.aqi == 4) {
        bool on = blink(500);
        p1r = on ? 255 : 0; p1g = 0; p1b = 0;
        p1Effect = anim ? "blink" : "solid";
        p1Meaning = "AQI Dangerous";
        p1Reason = "Air quality unhealthy";

    } else if (weather.aqi == 3) {
        p1r = 255; p1g = 120; p1b = 0;
        p1Effect = "solid";
        p1Meaning = "AQI Bad";
        p1Reason = "Air quality poor";

    } else if (weather.aqi == 2) {
        p1r = 200; p1g = 200; p1b = 0;
        p1Effect = "solid";
        p1Meaning = "AQI Moderate";
        p1Reason = "Moderate air quality";

    } else {
        // AQI good → environmental metrics
        float uv = weather.uvIndex;
        float wind = weather.windSpeed;
        float vis = weather.visibility;

        if (uv >= 11) {
            bool on = blink(150);
            p1r = on ? 255 : 0; p1g = on ? 120 : 0; p1b = 0;
            p1Effect = anim ? "fast-blink" : "solid";
            p1Meaning = "UV Extreme";
            p1Reason = "UV index ≥ 11";

        } else if (wind >= 17) {
            bool on = blink(120);
            p1r = 0; p1g = on ? 200 : 0; p1b = on ? 200 : 0;
            p1Effect = anim ? "fast-blink" : "solid";
            p1Meaning = "Storm Wind";
            p1Reason = "Wind speed severe";

        } else if (vis > 0 && vis < 1.0f) {
            bool on = blink(400);
            p1r = on ? 220 : 0; p1g = on ? 220 : 0; p1b = on ? 220 : 0;
            p1Effect = anim ? "blink" : "solid";
            p1Meaning = "Heavy Fog";
            p1Reason = "Visibility < 1 km";

        } else if (uv >= 8) {
            int v = pulse(140, 80, 450.0f);
            p1r = v; p1g = v / 2; p1b = 0;
            p1Effect = anim ? "pulse" : "solid";
            p1Meaning = "UV High";
            p1Reason = "UV index 8–10";

        } else if (wind >= 10) {
            bool on = blink(400);
            p1r = 0; p1g = on ? 190 : 0; p1b = on ? 190 : 0;
            p1Effect = anim ? "blink" : "solid";
            p1Meaning = "Strong Wind";
            p1Reason = "Wind speed strong";

        } else if (vis > 0 && vis < 5.0f) {
            int v = pulse(120, 80, 700.0f);
            p1r = v; p1g = v; p1b = v;
            p1Effect = anim ? "pulse" : "solid";
            p1Meaning = "Medium Fog";
            p1Reason = "Visibility 1–5 km";

        } else if (uv >= 3) {
            p1r = 120; p1g = 60; p1b = 0;
            p1Effect = "solid";
            p1Meaning = "UV Moderate";
            p1Reason = "UV index 3–7";

        } else if (wind >= 5) {
            int v = pulse(90, 60, 800.0f);
            p1r = 0; p1g = v; p1b = v;
            p1Effect = anim ? "pulse" : "solid";
            p1Meaning = "Wind Medium";
            p1Reason = "Wind speed 5–10 m/s";

        } else if (vis > 0 && vis < 10.0f) {
            p1r = 160; p1g = 160; p1b = 160;
            p1Effect = "dim";
            p1Meaning = "Light Fog";
            p1Reason = "Visibility 5–10 km";

        } else {
            float hum = weather.humidity;
            if (hum < 30) {
                int v = pulse(30, 40, 1000.0f);
                p1r = 0; p1g = 0; p1b = v;
                p1Effect = anim ? "pulse" : "solid";
                p1Meaning = "Low Humidity";
                p1Reason = "Humidity < 30%";
            } else if (hum < 60) {
                p1r = 0; p1g = 0; p1b = 150;
                p1Effect = "solid";
                p1Meaning = "Medium Humidity";
                p1Reason = "Humidity 30–60%";
            } else if (hum < 80) {
                int v = pulse(80, 90, 2000.0f);
                p1r = v; p1g = 0; p1b = v;
                p1Effect = anim ? "slow-pulse" : "solid";
                p1Meaning = "High Humidity";
                p1Reason = "Humidity 60–80%";
            } else {
                int v = pulse(90, 90, 400.0f);
                p1r = v; p1g = 0; p1b = v;
                p1Effect = anim ? "fast-pulse" : "solid";
                p1Meaning = "Very High Humidity";
                p1Reason = "Humidity > 80%";
            }
        }
    }

    pixels.setPixelColor(0, pixels.Color(p1r, p1g, p1b));
    setPixelState(pixel1State, p1r, p1g, p1b, p1Effect, p1Meaning, p1Reason);

    // ── Pixel 2: Device Status ────────────────────────────────
    uint8_t p2r = 0, p2g = 0, p2b = 30;
    const char* p2Effect = "solid";
    const char* p2Meaning = "Device Idle";
    const char* p2Reason = "Idle";

    if (devMode) {
        int hue = (int)((now / 10) % 256);
        uint32_t col = colorFromHSV(hue, 255, 200);
        p2r = (col >> 16) & 0xFF;
        p2g = (col >> 8) & 0xFF;
        p2b = col & 0xFF;
        p2Effect = "rainbow";
        p2Meaning = "Developer Mode";
        p2Reason = "Dev mode active";

    } else if (recoveryMode) {
        bool alt = blink(500);
        p2r = alt ? 200 : 0;
        p2g = 0;
        p2b = alt ? 0 : 200;
        p2Effect = anim ? "alternate" : "solid";
        p2Meaning = "Recovery Mode";
        p2Reason = "Recovery boot";

    } else if (apiStatus == AS_FAILED) {
        int slot = (now / 300) % 8;
        bool on = (slot < 6 && slot % 2 == 0);
        p2r = on ? 200 : 0; p2g = 0; p2b = 0;
        p2Effect = anim ? "triple-blink" : "solid";
        p2Meaning = "API Failed";
        p2Reason = "Weather API unreachable";

    } else if (wifiQuality == WQ_DISCONNECTED) {
        bool on = blink(500);
        p2r = on ? 200 : 0; p2g = 0; p2b = 0;
        p2Effect = anim ? "blink" : "solid";
        p2Meaning = "WiFi Disconnected";
        p2Reason = "No WiFi connection";

    } else if (inetQuality == IQ_NONE) {
        bool on = blink(500);
        p2r = on ? 200 : 0; p2g = 0; p2b = 0;
        p2Effect = anim ? "blink" : "solid";
        p2Meaning = "No Internet";
        p2Reason = "Internet unreachable";

    } else if (updateState == US_UPDATING) {
        bool on = blink(100);
        p2r = on ? 255 : 0; p2g = on ? 255 : 0; p2b = on ? 255 : 0;
        p2Effect = anim ? "fast-blink" : "solid";
        p2Meaning = "Updating";
        p2Reason = "Firmware update in progress";

    } else if (updateState == US_SUCCESS) {
        p2r = 0; p2g = 200; p2b = 0;
        p2Effect = "solid";
        p2Meaning = "Update Success";
        p2Reason = "Firmware updated";

    } else if (updateState == US_FAIL) {
        bool on = blink(300);
        p2r = on ? 200 : 0; p2g = 0; p2b = 0;
        p2Effect = anim ? "blink" : "solid";
        p2Meaning = "Update Failed";
        p2Reason = "Firmware update error";

    } else if (apiFlashUntil > now && apiStatus == AS_WORKING) {
        p2r = 0; p2g = 200; p2b = 0;
        p2Effect = "flash";
        p2Meaning = "API OK";
        p2Reason = "Weather update received";

    } else if (apiFlashUntil > now && apiStatus == AS_SLOW) {
        p2r = 200; p2g = 200; p2b = 0;
        p2Effect = "flash";
        p2Meaning = "API Slow";
        p2Reason = "Weather update slow";

    } else if (wifiQuality == WQ_WEAK) {
        int v = pulse(80, 60, 600.0f);
        p2r = v; p2g = v; p2b = 0;
        p2Effect = anim ? "pulse" : "solid";
        p2Meaning = "WiFi Weak";
        p2Reason = "Signal weak";

    } else if (wifiQuality == WQ_VERY_WEAK) {
        int v = pulse(80, 60, 600.0f);
        p2r = v; p2g = v / 2; p2b = 0;
        p2Effect = anim ? "pulse" : "solid";
        p2Meaning = "WiFi Very Weak";
        p2Reason = "Signal very weak";

    } else {
        switch (inetQuality) {
            case IQ_GOOD: {
                int v = pulse(80, 60, 800.0f);
                p2r = 0; p2g = 0; p2b = v;
                p2Effect = anim ? "pulse" : "solid";
                p2Meaning = "Internet Good";
                p2Reason = "Low latency";
                break;
            }
            case IQ_SLOW: {
                int v = pulse(80, 60, 800.0f);
                p2r = v / 2; p2g = 0; p2b = v;
                p2Effect = anim ? "pulse" : "solid";
                p2Meaning = "Internet Slow";
                p2Reason = "Moderate latency";
                break;
            }
            case IQ_BAD: {
                bool on = blink(400);
                p2r = on ? 200 : 0; p2g = on ? 80 : 0; p2b = 0;
                p2Effect = anim ? "blink" : "solid";
                p2Meaning = "Internet Bad";
                p2Reason = "High latency";
                break;
            }
            default:
                break;
        }
    }

    pixels.setPixelColor(1, pixels.Color(p2r, p2g, p2b));
    setPixelState(pixel2State, p2r, p2g, p2b, p2Effect, p2Meaning, p2Reason);
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
            displayMode = (displayMode == MODE_WEATHER) ? MODE_SEASON : MODE_WEATHER;
            digitalWrite(PIN_RED, displayMode == MODE_SEASON ? HIGH : LOW);
            lastToggle = millis();
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
    if (!config.buzzerEnabled) return;
    if (config.nightMute && isNight) return; // Auto-mute 22:00 – 07:00
    tone(PIN_BUZZER, freq, durMs);
}

void buzzerWarning() {
    if (!config.buzzerEnabled) return;
    if (config.nightMute && isNight) return;
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
        yellowMode = config.holiday ? YL_FESTIVAL : parseDayOfWeek(timeClient.getDay());
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
    Serial.printf("  Errors    : %lu\n",  (unsigned long)errorCount);
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
    // Mix hardware entropy (RANDOM_REG32 = ESP8266 hardware RNG) with chip IDs
    // to produce an unpredictable SKY-XXXX-XXXX pair key.
    uint32_t a = RANDOM_REG32 ^ ESP.getChipId()      ^ (uint32_t)(micros() * 1234567UL);
    uint32_t b = RANDOM_REG32 ^ ESP.getFlashChipId() ^ (uint32_t)(millis() * 987654321UL);
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

const char* weatherLabel(WeatherCond cond) {
    switch (cond) {
        case WC_HOT:           return "Hot";
        case WC_COLD:          return "Cold";
        case WC_RAIN:          return "Rain";
        case WC_CLOUDY:        return "Cloudy";
        case WC_HEAVY_RAIN:    return "Heavy Rain";
        case WC_HEAT_WARNING:  return "Heat Warning";
        case WC_THUNDERSTORM:  return "Thunderstorm";
        case WC_CLEAR:
        default:               return "Clear";
    }
}

const char* seasonLabel(SeasonCond cond) {
    switch (cond) {
        case SC_SUMMER:  return "Summer";
        case SC_WINTER:  return "Winter";
        case SC_MONSOON: return "Monsoon";
        default:         return "Unknown";
    }
}

const char* warmLabel(WarmLedMode mode) {
    switch (mode) {
        case WL_DAY:       return "Day";
        case WL_NIGHT:     return "Night";
        case WL_CRESCENT:  return "Crescent";
        case WL_FULL_MOON: return "Full Moon";
        case WL_NEW_MOON:  return "New Moon";
        default:           return "Day";
    }
}

const char* yellowLabel(YellowLedMode mode) {
    switch (mode) {
        case YL_WEEKDAY:  return "Weekday";
        case YL_SATURDAY: return "Saturday";
        case YL_SUNDAY:   return "Sunday";
        case YL_FESTIVAL: return "Holiday";
        default:          return "Weekday";
    }
}

const char* extraLabel(ExtraLedMode mode) {
    switch (mode) {
        case EL_LOADING: return "Loading";
        case EL_SUCCESS: return "Success";
        case EL_ERROR:   return "Error";
        default:         return "Success";
    }
}

const char* wifiQualityLabel(WiFiQuality quality) {
    switch (quality) {
        case WQ_CONNECTED:   return "connected";
        case WQ_WEAK:        return "weak";
        case WQ_VERY_WEAK:   return "very-weak";
        case WQ_DISCONNECTED:return "disconnected";
        default:             return "disconnected";
    }
}

const char* internetQualityLabel(InternetQuality quality) {
    switch (quality) {
        case IQ_GOOD: return "good";
        case IQ_SLOW: return "slow";
        case IQ_BAD:  return "bad";
        case IQ_NONE: return "none";
        default:      return "none";
    }
}

const char* apiStatusLabel(ApiStatus status) {
    switch (status) {
        case AS_WORKING: return "working";
        case AS_SLOW:    return "slow";
        case AS_FAILED:  return "failed";
        default:         return "failed";
    }
}

const char* updateStateLabel(UpdateState state) {
    switch (state) {
        case US_IDLE:     return "idle";
        case US_UPDATING: return "updating";
        case US_SUCCESS:  return "success";
        case US_FAIL:     return "fail";
        default:          return "idle";
    }
}

String windDirFromDeg(int deg) {
    static const char* dirs[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int idx = (int)((deg + 22) / 45) % 8;
    return String(dirs[idx]);
}

void setPixelState(PixelState &state, uint8_t r, uint8_t g, uint8_t b,
                   const char* effect, const char* meaning, const char* reason) {
    state.r = r;
    state.g = g;
    state.b = b;
    state.effect = effect;
    state.meaning = meaning;
    state.reason = reason;
}
