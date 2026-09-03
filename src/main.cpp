/* ====================================================================
 * ESP32 + 3.5" ILI9488 IPS TFT LCD (8-BIT PARALLEL) WEB DISPLAY
 * Reference: Exact Hardware & Pin Configuration from example.ino
 * Display Controller : ILI9488 3.5" TFT LCD (8-bit Parallel Mode, 320x480 / 480x320)
 * Low-Level Driver   : Direct Zero-Dependency High-Speed GPIO Bus
 * Wi-Fi Credentials  : SSID "sakshyam" | Password "sakshyam"
 * Features           : Web-to-Display Custom Streamer, 4 Layouts, 4 Themes
 * ==================================================================== */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

/* ====================================================================
 * 1. ILI9488 8-BIT PARALLEL PINS (EXACTLY MATCHING example.ino)
 * ==================================================================== */

// Control Pins
#define TFT_RST 18  // Hardware Reset
#define TFT_CS  19  // Chip Select (Active LOW)
#define TFT_RS  21  // Register Select / DC (0 = Command, 1 = Data)
#define TFT_WR  22  // Write Strobe (Active LOW)
#define TFT_RD  23  // Read Strobe (Active LOW)

// 8-Bit Data Bus Pins (D0 to D7)
#define TFT_D0  33
#define TFT_D1  32
#define TFT_D2  13
#define TFT_D3  12
#define TFT_D4  14
#define TFT_D5  27
#define TFT_D6  26
#define TFT_D7  25

const uint8_t dataPins[8] = { TFT_D0, TFT_D1, TFT_D2, TFT_D3,
                             TFT_D4, TFT_D5, TFT_D6, TFT_D7 };

/* ====================================================================
 * 2. WI-FI, SERVER FEED & OTA CREDENTIALS
 * ==================================================================== */
const char* WIFI_SSID     = "sakshyam";
const char* WIFI_PASSWORD = "sakshyam";

// HYDRA Production Consolidated Telemetry Feed URL (per a.md Section 1 & 4)
const char* SERVER_FEED_URL = "https://zenithkandel.com.np/hydra/backend/api/telemetry/feed.php";
// Local Development Fallback
// const char* SERVER_FEED_URL = "http://192.168.1.100/codes/hydra/backend/api/telemetry/feed.php";

const unsigned long SERVER_POLL_INTERVAL_MS = 2500; // Ingest cadence: 2.5s
unsigned long lastServerPollTime = 0;

const char* AP_SSID       = "ESP32-TFT-DISPLAY";
const char* AP_PASSWORD   = "12345678";

const char* MDNS_HOSTNAME = "tft-display";
const char* otaUsername   = "admin";
const char* otaPassword   = "admin";

// Live Server Telemetry Cache (Level 2 City Hub / PEOC Kiosk Mirror)
struct FloodData {
  String name = "MODI KHOLA SURGE";
  String status = "STANDBY";
  float distCm = 0.0;
  float waterDepthCm = 0.0;
  String zone = "WAITING";
  String hazard = "NOMINAL";
  String lastSync = "--:--:--";
} liveFlood;

struct FireData {
  String name = "PINE RIDGE SENTINEL";
  String status = "STANDBY";
  float tempC = 0.0;
  float humidity = 0.0;
  float gasPpm = 0.0;
  String airQuality = "Warming up";
  String hazard = "NOMINAL";
  String lastSync = "--:--:--";
} liveFire;

struct LandslideData {
  String name = "ANNAPURNA ESCARPMENT";
  String status = "STANDBY";
  bool gpsConnected = false;
  bool gpsFix = false;
  int satellites = 0;
  float lat = 0.0;
  float lng = 0.0;
  float altM = 0.0;
  float speedKmh = 0.0;
  float pitch = 0.0;
  float roll = 0.0;
  float accelG = 1.0;
  String hazard = "NOMINAL";
  String lastSync = "--:--:--";
} liveLandslide;

struct SystemSummary {
  String region = "GHANDRUK BASIN, NEPAL";
  String overallStatus = "CONNECTING...";
  String masterStatus = "STANDBY";
  String gsmStatus = "GSM_ONLINE";
  bool isEmergency = false;
  unsigned long lastFetchMillis = 0;
  int lastHttpCode = 0;
  unsigned long lastLatencyMs = 0;
  unsigned long fetchSuccessCount = 0;
  unsigned long fetchFailCount = 0;
} liveSummary;

/* ====================================================================
 * 3. DISPLAY CONFIGURATION & RGB565 COLOR MACROS
 * ==================================================================== */
#define RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_DARK_GRAY   0x1082
#define COLOR_PANEL_BG    0x0841
#define COLOR_RED         RGB565(255, 59, 59)
#define COLOR_GREEN       RGB565(0, 255, 135)
#define COLOR_AMBER       RGB565(255, 184, 0)
#define COLOR_CYAN        RGB565(0, 212, 255)

// Dynamic Screen Dimensions
int screenWidth  = 480;
int screenHeight = 320;
uint8_t currentRotation = 1; // 1 = Landscape (480x320), 0 = Portrait (320x480)

// Active Theme Colors
uint16_t COLOR_BG     = COLOR_BLACK;
uint16_t COLOR_FG     = COLOR_WHITE;
uint16_t COLOR_ACCENT = COLOR_WHITE;
uint16_t COLOR_PANEL  = COLOR_DARK_GRAY;
uint16_t COLOR_BORDER = COLOR_WHITE;

// Display State
String currentTitle      = "HYDRA PEOC COMMAND KIOSK";
String currentMessage    = "Live telemetry streaming continuously from https://zenithkandel.com.np/hydra";
String currentMode       = "KIOSK"; // KIOSK (default live mirror), CARD, HUD, ALERT, TEXT
String currentTheme      = "MONO";  // MONO, CYAN, EMERALD, AMBER
int    currentFontSize   = 2;       // 1 = Small, 2 = Medium, 3 = Large

WebServer server(80);

/* ====================================================================
 * 4. FONT 5x7 BITMAP TABLE (From example.ino)
 * ==================================================================== */
const uint8_t font5x7[96][5] PROGMEM = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x00, 0x08, 0x14, 0x22, 0x41}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x41, 0x22, 0x14, 0x08, 0x00}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x01, 0x01}, {0x3E, 0x41, 0x41, 0x51, 0x32},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x04, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x7F, 0x20, 0x18, 0x20, 0x7F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x03, 0x04, 0x78, 0x04, 0x03},
    {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x00, 0x7F, 0x41, 0x41},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x41, 0x41, 0x7F, 0x00, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F}, {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02}, {0x08, 0x14, 0x54, 0x54, 0x3C},
    {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00}, {0x00, 0x7F, 0x10, 0x28, 0x44},
    {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7F, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, {0x08, 0x1C, 0x2A, 0x08, 0x08},
};

/* ====================================================================
 * 5. LOW-LEVEL 8-BIT PARALLEL DRIVER ROUTINES (From example.ino)
 * ==================================================================== */

inline void writeData8(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(dataPins[i], (data >> i) & 0x01);
  }
  digitalWrite(TFT_WR, LOW);
  digitalWrite(TFT_WR, HIGH);
}

void writeCommand(uint8_t cmd) {
  digitalWrite(TFT_RS, LOW);
  digitalWrite(TFT_CS, LOW);
  writeData8(cmd);
  digitalWrite(TFT_CS, HIGH);
}

void writeDataByte(uint8_t data) {
  digitalWrite(TFT_RS, HIGH);
  digitalWrite(TFT_CS, LOW);
  writeData8(data);
  digitalWrite(TFT_CS, HIGH);
}

void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  writeCommand(0x2A);
  writeDataByte(x0 >> 8);
  writeDataByte(x0);
  writeDataByte(x1 >> 8);
  writeDataByte(x1);
  writeCommand(0x2B);
  writeDataByte(y0 >> 8);
  writeDataByte(y0);
  writeDataByte(y1 >> 8);
  writeDataByte(y1);
  writeCommand(0x2C);
}

void fillScreen(uint16_t color) {
  setAddressWindow(0, 0, screenWidth - 1, screenHeight - 1);
  digitalWrite(TFT_RS, HIGH);
  digitalWrite(TFT_CS, LOW);
  uint8_t hi = color >> 8, lo = color & 0xFF;
  for (uint32_t i = 0; i < (uint32_t)screenWidth * screenHeight; i++) {
    writeData8(hi);
    writeData8(lo);
  }
  digitalWrite(TFT_CS, HIGH);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x >= screenWidth || y >= screenHeight || w <= 0 || h <= 0) return;
  if (x + w > screenWidth)  w = screenWidth - x;
  if (y + h > screenHeight) h = screenHeight - y;
  setAddressWindow(x, y, x + w - 1, y + h - 1);
  digitalWrite(TFT_RS, HIGH);
  digitalWrite(TFT_CS, LOW);
  uint8_t hi = color >> 8, lo = color & 0xFF;
  for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
    writeData8(hi);
    writeData8(lo);
  }
  digitalWrite(TFT_CS, HIGH);
}

void drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  fillRect(x, y, w, 1, color);
}

void drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  fillRect(x, y, 1, h, color);
}

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  drawHLine(x, y, w, color);
  drawHLine(x, y + h - 1, w, color);
  drawVLine(x, y, h, color);
  drawVLine(x + w - 1, y, h, color);
}

void setRotation(uint8_t r) {
  currentRotation = r;
  writeCommand(0x36);
  if (r == 1) {
    writeDataByte(0x28); // Landscape (480x320)
    screenWidth  = 480;
    screenHeight = 320;
  } else {
    writeDataByte(0x48); // Portrait (320x480)
    screenWidth  = 320;
    screenHeight = 480;
  }
}

void tftInit() {
  pinMode(TFT_RST, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_RS, OUTPUT);
  pinMode(TFT_WR, OUTPUT);
  pinMode(TFT_RD, OUTPUT);
  for (int i = 0; i < 8; i++) pinMode(dataPins[i], OUTPUT);

  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_RD, HIGH);
  digitalWrite(TFT_WR, HIGH);

  digitalWrite(TFT_RST, HIGH);
  delay(50);
  digitalWrite(TFT_RST, LOW);
  delay(150);
  digitalWrite(TFT_RST, HIGH);
  delay(150);

  writeCommand(0x01); delay(150);
  writeCommand(0x11); delay(150);
  writeCommand(0x3A); writeDataByte(0x55);
  setRotation(currentRotation);
  writeCommand(0x29); delay(50);

  Serial.println("[OK] ILI9488 initialized in 8-bit Parallel Mode!");
}

/* ====================================================================
 * 6. GRAPHICS & TEXT RENDERING
 * ==================================================================== */

void drawChar(int16_t x, int16_t y, char c, uint16_t color, uint8_t size) {
  if (c < 32 || c > 127) c = ' ';
  uint8_t idx = c - 32;
  for (int col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(&font5x7[idx][col]);
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        if (size == 1) {
          if (x + col >= 0 && x + col < screenWidth && y + row >= 0 && y + row < screenHeight) {
            fillRect(x + col, y + row, 1, 1, color);
          }
        } else {
          fillRect(x + col * size, y + row * size, size, size, color);
        }
      }
    }
  }
}

void drawText(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size) {
  while (*text) {
    drawChar(x, y, *text++, color, size);
    x += 6 * size;
  }
}

void drawText(int16_t x, int16_t y, const String &text, uint16_t color, uint8_t size) {
  drawText(x, y, text.c_str(), color, size);
}

void drawTextCentered(int16_t y, const char *text, uint16_t color, uint8_t size) {
  int16_t x = (screenWidth - strlen(text) * 6 * size) / 2;
  if (x < 0) x = 0;
  drawText(x, y, text, color, size);
}

void drawTextCentered(int16_t y, const String &text, uint16_t color, uint8_t size) {
  drawTextCentered(y, text.c_str(), color, size);
}

void drawWrappedText(int16_t x, int16_t y, int16_t maxW, const String& text, uint16_t color, uint8_t size) {
  int16_t curX = x;
  int16_t curY = y;
  int16_t charW = 6 * size;
  int16_t lineH = 8 * size + 4;
  int len = text.length();
  int i = 0;

  while (i < len) {
    if (text[i] == '\n') {
      curX = x;
      curY += lineH;
      i++;
      continue;
    }
    int nextSpace = text.indexOf(' ', i);
    int nextNewline = text.indexOf('\n', i);
    int wordEnd = len;
    if (nextSpace != -1 && (nextNewline == -1 || nextSpace < nextNewline)) {
      wordEnd = nextSpace;
    } else if (nextNewline != -1) {
      wordEnd = nextNewline;
    }
    int wordLen = wordEnd - i;
    int wordPix = wordLen * charW;

    if (curX + wordPix > x + maxW && curX > x) {
      curX = x;
      curY += lineH;
    }
    for (int j = i; j < wordEnd; j++) {
      drawChar(curX, curY, text[j], color, size);
      curX += charW;
    }
    if (wordEnd < len && text[wordEnd] == ' ') {
      drawChar(curX, curY, ' ', color, size);
      curX += charW;
      i = wordEnd + 1;
    } else {
      i = wordEnd;
    }
  }
}

void applyTheme(String themeName) {
  currentTheme = themeName;
  if (themeName == "CYAN") {
    COLOR_BG     = RGB565(5, 15, 25);
    COLOR_FG     = COLOR_WHITE;
    COLOR_ACCENT = COLOR_CYAN;
    COLOR_PANEL  = RGB565(15, 30, 50);
    COLOR_BORDER = COLOR_CYAN;
  } else if (themeName == "EMERALD") {
    COLOR_BG     = RGB565(5, 20, 10);
    COLOR_FG     = COLOR_WHITE;
    COLOR_ACCENT = COLOR_GREEN;
    COLOR_PANEL  = RGB565(10, 35, 20);
    COLOR_BORDER = COLOR_GREEN;
  } else if (themeName == "AMBER") {
    COLOR_BG     = COLOR_BLACK;
    COLOR_FG     = COLOR_WHITE;
    COLOR_ACCENT = COLOR_AMBER;
    COLOR_PANEL  = RGB565(40, 30, 0);
    COLOR_BORDER = COLOR_AMBER;
  } else { // Default MONO
    COLOR_BG     = COLOR_BLACK;
    COLOR_FG     = COLOR_WHITE;
    COLOR_ACCENT = COLOR_WHITE;
    COLOR_PANEL  = COLOR_DARK_GRAY;
    COLOR_BORDER = COLOR_WHITE;
  }
}

/* ====================================================================
 * 6.5. HYDRA SERVER TELEMETRY INGEST (HTTP/HTTPS GET)
 * ==================================================================== */

void fetchTelemetryFeed() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  http.setTimeout(3500);
  unsigned long startT = millis();
  bool isHttps = String(SERVER_FEED_URL).startsWith("https://");
  int httpCode = 0;
  String payload = "";

  if (isHttps) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    secureClient.setTimeout(3500);
    if (http.begin(secureClient, SERVER_FEED_URL)) {
      httpCode = http.GET();
      if (httpCode > 0) {
        payload = http.getString();
      }
      http.end();
    }
  } else {
    WiFiClient client;
    client.setTimeout(3500);
    if (http.begin(client, SERVER_FEED_URL)) {
      httpCode = http.GET();
      if (httpCode > 0) {
        payload = http.getString();
      }
      http.end();
    }
  }

  liveSummary.lastLatencyMs = millis() - startT;
  liveSummary.lastHttpCode = httpCode;
  liveSummary.lastFetchMillis = millis();

  if (httpCode == 200 && payload.length() > 0) {
    liveSummary.fetchSuccessCount++;
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      JsonObject data = doc["data"];
      liveSummary.region = data["region"].as<const char*>() ? String(data["region"].as<const char*>()) : "ANNAPURNA BASIN";
      String st = data["alert_summary"]["overall_status"] | "NOMINAL";
      liveSummary.overallStatus = st;
      liveSummary.isEmergency = (st == "CRITICAL" || st == "ALERT");

      // Flood Node
      JsonObject flood = data["nodes"]["flood_node"];
      if (!flood.isNull()) {
        liveFlood.status = flood["status"] | "ONLINE";
        liveFlood.distCm = flood["dist_cm"] | 0.0f;
        liveFlood.waterDepthCm = flood["water_depth_cm"] | 0.0f;
        liveFlood.zone = flood["zone"] | "--";
        liveFlood.hazard = flood["hazard_level"] | "NORMAL";
        liveFlood.lastSync = flood["last_sync"] | "--";
      }

      // Fire Node
      JsonObject fire = data["nodes"]["fire_node"];
      if (!fire.isNull()) {
        liveFire.status = fire["status"] | "ONLINE";
        liveFire.tempC = fire["temperatureC"] | 0.0f;
        liveFire.humidity = fire["humidity"] | 0.0f;
        liveFire.gasPpm = fire["gasPPM"] | 0.0f;
        liveFire.airQuality = fire["air_quality"] | "--";
        liveFire.hazard = fire["hazard_level"] | "NORMAL";
        liveFire.lastSync = fire["last_sync"] | "--";
      }

      // Landslide Node
      JsonObject landslide = data["nodes"]["landslide_node"];
      if (!landslide.isNull()) {
        liveLandslide.status = landslide["status"] | "ONLINE";
        liveLandslide.hazard = landslide["hazard_level"] | "NORMAL";
        liveLandslide.lastSync = landslide["last_sync"] | "--";

        JsonObject gps = landslide["gps"];
        if (!gps.isNull()) {
          liveLandslide.gpsConnected = gps["connected"] | false;
          liveLandslide.gpsFix = gps["fix"] | false;
          liveLandslide.satellites = gps["satellites"] | 0;
          liveLandslide.lat = gps["lat"] | 0.0f;
          liveLandslide.lng = gps["lng"] | 0.0f;
          liveLandslide.altM = gps["alt_m"] | 0.0f;
          liveLandslide.speedKmh = gps["speed_kmh"] | 0.0f;
        }

        JsonObject mpu = landslide["mpu"];
        if (!mpu.isNull()) {
          liveLandslide.accelG = mpu["total_accel_g"] | 1.0f;
          liveLandslide.pitch = mpu["pitch"] | 0.0f;
          liveLandslide.roll = mpu["roll"] | 0.0f;
        }
      }

      // Master Node
      JsonObject master = data["nodes"]["master_node"];
      if (!master.isNull()) {
        liveSummary.masterStatus = master["status"] | "ONLINE";
        liveSummary.gsmStatus = master["cellular_gateway"] | "GSM_ONLINE";
      }

      Serial.printf("[SERVER] Telemetry Ingest OK (200, %lums): %s | Status: %s\n",
                    liveSummary.lastLatencyMs, liveSummary.region.c_str(), liveSummary.overallStatus.c_str());
    } else {
      Serial.printf("[SERVER] JSON Deserialization error: %s\n", error.c_str());
    }
  } else {
    liveSummary.fetchFailCount++;
    Serial.printf("[SERVER] Feed fetch failed HTTP %d (%lums)\n", httpCode, liveSummary.lastLatencyMs);
  }
}

/* ====================================================================
 * 7. NATIVE ON-SCREEN DASHBOARD RENDER ROUTINES
 * ==================================================================== */

enum DisplayState {
  STATE_BOOT,
  STATE_NORMAL,
  STATE_ALERT
};

DisplayState currentScreenState = STATE_BOOT;

// 7.1. HYDRA SYSTEM BOOT UP SEQUENCE (Splash & Progress Bar)
void renderBootScreen(int progressPct, const char* statusMsg) {
  fillScreen(COLOR_BLACK);

  // Outer framing
  drawRect(10, 10, screenWidth - 20, screenHeight - 20, COLOR_CYAN);
  drawRect(12, 12, screenWidth - 24, screenHeight - 24, COLOR_WHITE);

  // Stylized Title block
  fillRect(16, 20, screenWidth - 32, 65, RGB565(10, 20, 35));
  drawRect(16, 20, screenWidth - 32, 65, COLOR_CYAN);
  drawTextCentered(30, "H Y D R A", COLOR_WHITE, 3);
  drawTextCentered(58, "AUTONOMOUS DISASTER WARNING NETWORK", COLOR_CYAN, 1);

  // System Specs Diagnostics
  drawText(30, 98,  "> HARDWARE: ARDUINO NANO ESP32 (ESP32-S3)", COLOR_WHITE, 1);
  drawText(30, 115, "> DISPLAY : 3.5\" ILI9488 IPS (8-BIT PARALLEL BUS)", COLOR_WHITE, 1);
  drawText(30, 132, "> NETWORK : IEEE 802.11 b/g/n [sakshyam]", COLOR_WHITE, 1);
  drawText(30, 149, "> GATEWAY : LEVEL 2 REGIONAL PEOC COMMAND KIOSK", COLOR_CYAN, 1);
  drawText(30, 166, "> SERVER  : https://zenithkandel.com.np/hydra", COLOR_WHITE, 1);

  // Progress Bar Frame
  drawRect(30, 195, screenWidth - 60, 24, COLOR_WHITE);
  fillRect(32, 197, screenWidth - 64, 20, COLOR_BLACK);

  // Progress Bar Fill
  int fillW = (int)((screenWidth - 68) * (progressPct / 100.0f));
  if (fillW > 0) {
    fillRect(34, 199, fillW, 16, COLOR_CYAN);
  }

  // Status message below bar
  char pctBuf[16];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", progressPct);
  drawText(30, 228, statusMsg, COLOR_WHITE, 1);
  drawText(screenWidth - 65, 228, pctBuf, COLOR_CYAN, 1);

  // Microcontroller & Firmware info
  drawTextCentered(265, "POKHARA EMERGENCY OPERATIONS CENTER - DISASTER RADAR", COLOR_DARK_GRAY, 1);
  drawTextCentered(282, "INITIALIZING PEOC TELEMETRY ENGINE & FIRMWARE V2.4", COLOR_DARK_GRAY, 1);
}

// 7.2. NORMAL DATA SCREEN (Displays All Stations When Nominal)
void renderNormalDataScreen() {
  fillScreen(COLOR_BG);

  // 1. TOP HEADER (Y: 0 to 24)
  fillRect(0, 0, screenWidth, 24, COLOR_PANEL);
  drawHLine(0, 24, screenWidth, COLOR_BORDER);
  drawText(8, 7, "HYDRA // PEOC REGIONAL COMMAND KIOSK", COLOR_ACCENT, 1);

  String syncStatus = "SYNC: " + String(liveSummary.lastLatencyMs) + "ms | " + 
                      (liveSummary.lastHttpCode == 200 ? "200 OK" : ("HTTP " + String(liveSummary.lastHttpCode)));
  drawText(screenWidth - (syncStatus.length() * 6) - 8, 7, syncStatus.c_str(), 
           liveSummary.lastHttpCode == 200 ? COLOR_GREEN : COLOR_RED, 1);

  // 2. NOMINAL STATUS BANNER (Y: 28 to 54)
  fillRect(6, 28, screenWidth - 12, 26, RGB565(0, 35, 15));
  drawRect(6, 28, screenWidth - 12, 26, COLOR_GREEN);
  drawTextCentered(34, "[ ALL STATIONS NOMINAL - BASIN SECURE ]", COLOR_GREEN, 2);

  // 3. THREE STATION TELEMETRY CARDS (Y: 58 to 242)
  int cardY = 58;
  int cardH = 184;
  int cardW = 152;
  int gap = 6;

  // --- CARD 1: FLOOD NODE (X: 6) ---
  int c1X = 6;
  drawRect(c1X, cardY, cardW, cardH, COLOR_CYAN);
  fillRect(c1X + 2, cardY + 2, cardW - 4, 20, COLOR_PANEL);
  drawHLine(c1X, cardY + 22, cardW, COLOR_CYAN);
  drawText(c1X + 6, cardY + 7, "01 // FLOOD GAUGE", COLOR_CYAN, 1);
  drawText(c1X + cardW - 45, cardY + 7, liveFlood.status.c_str(), COLOR_WHITE, 1);

  drawText(c1X + 8, cardY + 30, "WATER DEPTH:", COLOR_WHITE, 1);
  String floodDepthStr = String(liveFlood.waterDepthCm, 1) + " cm";
  drawText(c1X + 8, cardY + 44, floodDepthStr.c_str(), COLOR_WHITE, 2);

  drawText(c1X + 8, cardY + 74, "CLEARANCE:", COLOR_WHITE, 1);
  String floodClearStr = String(liveFlood.distCm, 1) + " cm";
  drawText(c1X + 8, cardY + 88, floodClearStr.c_str(), COLOR_WHITE, 1);

  drawText(c1X + 8, cardY + 110, "RADAR ZONE:", COLOR_WHITE, 1);
  String zoneTrunc = liveFlood.zone;
  if (zoneTrunc.length() > 14) zoneTrunc = zoneTrunc.substring(0, 14);
  drawText(c1X + 8, cardY + 124, zoneTrunc.c_str(), COLOR_CYAN, 1);

  drawText(c1X + 8, cardY + 146, "HAZARD LEVEL:", COLOR_WHITE, 1);
  drawText(c1X + 8, cardY + 160, liveFlood.hazard.c_str(), COLOR_GREEN, 1);

  // --- CARD 2: FIRE & AIR (X: 164) ---
  int c2X = c1X + cardW + gap;
  drawRect(c2X, cardY, cardW, cardH, COLOR_AMBER);
  fillRect(c2X + 2, cardY + 2, cardW - 4, 20, COLOR_PANEL);
  drawHLine(c2X, cardY + 22, cardW, COLOR_AMBER);
  drawText(c2X + 6, cardY + 7, "02 // FIRE & AIR", COLOR_AMBER, 1);
  drawText(c2X + cardW - 45, cardY + 7, liveFire.status.c_str(), COLOR_WHITE, 1);

  drawText(c2X + 8, cardY + 30, "TEMPERATURE:", COLOR_WHITE, 1);
  String tempStr = String(liveFire.tempC, 1) + " C";
  drawText(c2X + 8, cardY + 44, tempStr.c_str(), COLOR_WHITE, 2);

  drawText(c2X + 8, cardY + 74, "HUMIDITY:", COLOR_WHITE, 1);
  String humStr = String(liveFire.humidity, 1) + " %";
  drawText(c2X + 8, cardY + 88, humStr.c_str(), COLOR_WHITE, 1);

  drawText(c2X + 8, cardY + 110, "GAS POLLUTION:", COLOR_WHITE, 1);
  String gasStr = String(liveFire.gasPpm, 1) + " PPM";
  drawText(c2X + 8, cardY + 124, gasStr.c_str(), COLOR_AMBER, 1);

  drawText(c2X + 8, cardY + 146, "AIR QUALITY:", COLOR_WHITE, 1);
  String airTrunc = liveFire.airQuality;
  if (airTrunc.length() > 14) airTrunc = airTrunc.substring(0, 14);
  drawText(c2X + 8, cardY + 160, airTrunc.c_str(), COLOR_WHITE, 1);

  // --- CARD 3: LANDSLIDE & IMU (X: 322) ---
  int c3X = c2X + cardW + gap;
  drawRect(c3X, cardY, cardW, cardH, COLOR_GREEN);
  fillRect(c3X + 2, cardY + 2, cardW - 4, 20, COLOR_PANEL);
  drawHLine(c3X, cardY + 22, cardW, COLOR_GREEN);
  drawText(c3X + 6, cardY + 7, "03 // LANDSLIDE", COLOR_GREEN, 1);
  drawText(c3X + cardW - 45, cardY + 7, liveLandslide.status.c_str(), COLOR_WHITE, 1);

  drawText(c3X + 8, cardY + 30, "SURFACE ACCEL:", COLOR_WHITE, 1);
  String accelStr = String(liveLandslide.accelG, 2) + " g";
  drawText(c3X + 8, cardY + 44, accelStr.c_str(), COLOR_WHITE, 2);

  drawText(c3X + 8, cardY + 74, "INCLINATION:", COLOR_WHITE, 1);
  String tiltStr = "P:" + String(liveLandslide.pitch, 1) + " R:" + String(liveLandslide.roll, 1);
  drawText(c3X + 8, cardY + 88, tiltStr.c_str(), COLOR_WHITE, 1);

  drawText(c3X + 8, cardY + 110, "GPS STATUS:", COLOR_WHITE, 1);
  String gpsStr = String(liveLandslide.satellites) + " Sats " + (liveLandslide.gpsFix ? "[FIX]" : "[SEARCH]");
  drawText(c3X + 8, cardY + 124, gpsStr.c_str(), liveLandslide.gpsFix ? COLOR_GREEN : COLOR_AMBER, 1);

  drawText(c3X + 8, cardY + 146, "LOCATION:", COLOR_WHITE, 1);
  String locStr = String(liveLandslide.lat, 2) + "N " + String(liveLandslide.lng, 2) + "E";
  drawText(c3X + 8, cardY + 160, locStr.c_str(), COLOR_WHITE, 1);

  // 4. VILLAGE MASTER DISPATCH BAR (Y: 248 to 292)
  drawRect(6, 248, screenWidth - 12, 46, COLOR_BORDER);
  fillRect(8, 250, screenWidth - 16, 42, COLOR_PANEL);
  
  String sirenStr = "VILLAGE SIREN: [ STANDBY OFF ]";
  drawText(14, 256, sirenStr.c_str(), COLOR_GREEN, 1);

  String gsmStr = "GATEWAY: " + liveSummary.gsmStatus;
  drawText(260, 256, gsmStr.c_str(), COLOR_CYAN, 1);

  String regionStr = "BASIN: " + liveSummary.region;
  drawText(14, 274, regionStr.c_str(), COLOR_WHITE, 1);

  String uplinkStr = "UPLINKS: " + String(liveSummary.fetchSuccessCount) + " OK";
  drawText(screenWidth - (uplinkStr.length() * 6) - 14, 274, uplinkStr.c_str(), COLOR_GREEN, 1);

  // 5. FOOTER (Y: 298 to 320)
  fillRect(0, screenHeight - 22, screenWidth, 22, COLOR_PANEL);
  drawHLine(0, screenHeight - 23, screenWidth, COLOR_BORDER);

  String ipStr = "IP: " + WiFi.localIP().toString() + " | http://tft-display.local";
  drawText(8, screenHeight - 15, ipStr.c_str(), COLOR_WHITE, 1);

  char upBuf[32];
  unsigned long sec = millis() / 1000;
  snprintf(upBuf, sizeof(upBuf), "UP: %02lu:%02lu:%02lu", sec / 3600, (sec % 3600) / 60, sec % 60);
  drawText(screenWidth - 85, screenHeight - 15, upBuf, COLOR_ACCENT, 1);
}

// 7.3. EMERGENCY ALERT POP-UP OVERLAY SCREEN (Triggered On Breach)
void renderAlertPopUpScreen() {
  fillScreen(COLOR_BLACK);

  // Flashing strobe double border
  static bool strobe = false;
  strobe = !strobe;
  uint16_t strobeColor = strobe ? COLOR_RED : COLOR_AMBER;
  drawRect(0, 0, screenWidth, screenHeight, strobeColor);
  drawRect(2, 2, screenWidth - 4, screenHeight - 4, strobeColor);

  // 1. TOP DANGER HEADER BANNER (Y: 6 to 52)
  fillRect(6, 6, screenWidth - 12, 46, COLOR_RED);
  drawRect(6, 6, screenWidth - 12, 46, COLOR_WHITE);
  drawTextCentered(14, "! CRITICAL DISASTER EMERGENCY ALERT !", COLOR_WHITE, 2);
  drawTextCentered(34, "IMMEDIATE EVACUATION & DEFENSE PROTOCOL ACTIVE", COLOR_WHITE, 1);

  // 2. CENTRAL ALERT POP-UP MODAL BOX (X: 12, Y: 58, W: 456, H: 176)
  fillRect(12, 58, screenWidth - 24, 176, RGB565(30, 0, 0));
  drawRect(12, 58, screenWidth - 24, 176, COLOR_RED);
  drawRect(14, 60, screenWidth - 28, 172, COLOR_RED);

  drawText(24, 68, ">> ACTIVE EMERGENCY BREACH DIAGNOSTICS:", COLOR_WHITE, 1);
  drawHLine(24, 80, screenWidth - 48, COLOR_RED);

  int lineY = 88;
  // Flood breach line
  if (liveFlood.hazard == "CRITICAL" || liveFlood.hazard == "HIGH" || liveFlood.waterDepthCm > 200) {
    String fMsg = "! FLOOD SURGE BREACH: WATER DEPTH " + String(liveFlood.waterDepthCm, 1) + " cm (CRITICAL OVERFLOW)";
    drawText(24, lineY, fMsg.c_str(), COLOR_RED, 1);
    lineY += 16;
  }

  // Landslide breach line
  if (liveLandslide.hazard == "CRITICAL" || liveLandslide.hazard == "HIGH" || liveLandslide.accelG > 1.2 || abs(liveLandslide.pitch) > 3.0) {
    String lMsg = "! LANDSLIDE DETECTED: ACCEL " + String(liveLandslide.accelG, 2) + "g | TILT P:" + String(liveLandslide.pitch, 1) + " R:" + String(liveLandslide.roll, 1);
    drawText(24, lineY, lMsg.c_str(), COLOR_RED, 1);
    lineY += 16;
  }

  // Fire breach line
  if (liveFire.hazard == "CRITICAL" || liveFire.hazard == "HIGH" || liveFire.gasPpm > 100 || liveFire.tempC > 40) {
    String frMsg = "! FIRE / GAS HAZARD: " + String(liveFire.gasPpm, 1) + " PPM | TEMP: " + String(liveFire.tempC, 1) + " C";
    drawText(24, lineY, frMsg.c_str(), COLOR_RED, 1);
    lineY += 16;
  }

  if (lineY == 88) {
    drawText(24, lineY, "! GENERAL PEOC ALARM DISPATCH TRIGGERED FROM SERVER !", COLOR_RED, 1);
    lineY += 16;
  }

  drawHLine(24, 138, screenWidth - 48, COLOR_DARK_GRAY);

  // Hardware Dispatch Actions
  drawText(24, 146, "VILLAGE SIREN : [ ! RELAY TRIGGERED - RUNNING CONTINUOUSLY ! ]", COLOR_WHITE, 1);
  drawText(24, 162, "CELLULAR SMS  : [ ALERT DISPATCHED VIA SIM800L CELLULAR NET ]", COLOR_CYAN, 1);
  drawText(24, 178, "TARGET REGION : " + liveSummary.region, COLOR_WHITE, 1);
  drawText(24, 194, "ACTION ORDER  : EVACUATE TO DESIGNATED HIGH GROUND IMMEDIATELY", COLOR_AMBER, 1);
  drawText(24, 210, "EMERGENCY SAR : ARMED POLICE FORCE (1114) / RED CROSS (1130)", COLOR_GREEN, 1);

  // 3. LIVE SENSOR MINI-STRIP (Y: 240 to 288)
  drawRect(12, 240, screenWidth - 24, 48, COLOR_BORDER);
  fillRect(14, 242, screenWidth - 28, 44, COLOR_PANEL);

  String fStrip = "FLOOD: " + String(liveFlood.waterDepthCm, 1) + "cm";
  drawText(20, 249, fStrip.c_str(), liveFlood.hazard == "CRITICAL" ? COLOR_RED : COLOR_WHITE, 1);

  String frStrip = "TEMP: " + String(liveFire.tempC, 1) + "C | GAS: " + String(liveFire.gasPpm, 0) + "PPM";
  drawText(160, 249, frStrip.c_str(), liveFire.hazard == "CRITICAL" ? COLOR_RED : COLOR_WHITE, 1);

  String lStrip = "IMU: " + String(liveLandslide.accelG, 2) + "g | " + String(liveLandslide.satellites) + "Sats";
  drawText(330, 249, lStrip.c_str(), liveLandslide.hazard == "CRITICAL" ? COLOR_RED : COLOR_WHITE, 1);

  String subStrip = "PEOC COMMAND KIOSK • SERVER UPLINK: " + String(liveSummary.lastLatencyMs) + "ms • HTTP " + String(liveSummary.lastHttpCode);
  drawText(20, 269, subStrip.c_str(), COLOR_CYAN, 1);

  // 4. FOOTER (Y: 294 to 320)
  fillRect(0, screenHeight - 22, screenWidth, 22, COLOR_PANEL);
  drawHLine(0, screenHeight - 23, screenWidth, COLOR_BORDER);

  String alertFooter = "ALERT OVERLAY ACTIVE • AUTO-DISMISSES WHEN ALL HAZARDS CLEAR";
  drawText(10, screenHeight - 15, alertFooter.c_str(), COLOR_WHITE, 1);

  char upBuf[32];
  unsigned long sec = millis() / 1000;
  snprintf(upBuf, sizeof(upBuf), "UP: %02lu:%02lu:%02lu", sec / 3600, (sec % 3600) / 60, sec % 60);
  drawText(screenWidth - 85, screenHeight - 15, upBuf, COLOR_ACCENT, 1);
}

void renderDisplay() {
  bool isAlert = liveSummary.isEmergency || 
                 (liveFlood.hazard == "CRITICAL" || liveFlood.hazard == "HIGH") ||
                 (liveLandslide.hazard == "CRITICAL" || liveLandslide.hazard == "HIGH") ||
                 (liveFire.hazard == "CRITICAL" || liveFire.hazard == "HIGH");

  if (isAlert) {
    renderAlertPopUpScreen();
  } else {
    renderNormalDataScreen();
  }
}

/* ====================================================================
 * 8. EMBEDDED HIGH-CONTRAST WEB DASHBOARD
 * ==================================================================== */
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ILI9488 3.5" PARALLEL TFT DASHBOARD</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; border-radius: 0 !important; }
    body { background: #000000; color: #FFFFFF; font-family: ui-monospace, Menlo, Consolas, monospace; padding: 16px; line-height: 1.35; }
    .container { max-width: 960px; margin: 0 auto; }
    header { border: 1px solid #FFFFFF; padding: 16px; margin-bottom: 16px; background: #050505; display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 12px; }
    .title-group h1 { font-size: 17px; letter-spacing: 2px; font-weight: 900; text-transform: uppercase; }
    .title-group p { font-size: 11px; color: #888888; letter-spacing: 1px; margin-top: 3px; }
    .badges { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    .badge { display: inline-flex; align-items: center; gap: 6px; padding: 4px 8px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; border: 1px solid #FFFFFF; text-decoration: none; }
    .badge.solid { background: #FFFFFF; color: #000000; }
    .badge.outline { background: #000000; color: #FFFFFF; }
    .badge.link { background: #000000; color: #FFFFFF; cursor: pointer; }
    .badge.link:hover { background: #FFFFFF; color: #000000; }
    .pulse { display: inline-block; width: 8px; height: 8px; background: #000000; animation: blink 1s steps(1) infinite; }
    @keyframes blink { 50% { opacity: 0; } }
    .section-title { font-size: 12px; letter-spacing: 2px; text-transform: uppercase; font-weight: 900; border-left: 4px solid #FFFFFF; padding-left: 8px; margin: 20px 0 10px 0; display: flex; justify-content: space-between; }
    
    .preview-card { border: 2px solid #FFFFFF; background: #080808; padding: 20px; margin-bottom: 16px; }
    .preview-label { font-size: 10px; color: #777; text-transform: uppercase; letter-spacing: 1.5px; font-weight: 700; margin-bottom: 6px; display: flex; justify-content: space-between; }
    .screen-mock { background: #000000; border: 1px solid #444444; padding: 18px; min-height: 120px; }
    .screen-mock h2 { font-size: 20px; font-weight: 900; letter-spacing: 1px; margin-bottom: 8px; color: #FFFFFF; }
    .screen-mock p { font-size: 14px; color: #CCCCCC; white-space: pre-wrap; word-break: break-word; }

    .form-panel { border: 1px solid #FFFFFF; background: #050505; padding: 20px; margin-bottom: 16px; }
    .form-group { margin-bottom: 14px; }
    .form-group label { display: block; font-size: 10px; color: #888888; text-transform: uppercase; letter-spacing: 1.5px; font-weight: 700; margin-bottom: 6px; }
    input[type=text], textarea, select { width: 100%; background: #000000; color: #FFFFFF; border: 1px solid #444444; padding: 12px 14px; font-family: inherit; font-size: 14px; font-weight: 700; outline: none; }
    input[type=text]:focus, textarea:focus, select:focus { border-color: #FFFFFF; }
    textarea { resize: vertical; min-height: 100px; }
    .ctrl-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px; margin-bottom: 14px; }
    
    .btn-row { display: flex; gap: 10px; flex-wrap: wrap; margin-top: 10px; }
    .btn { flex: 1; min-width: 140px; padding: 12px 18px; font-size: 11px; font-weight: 800; letter-spacing: 1px; text-transform: uppercase; color: #000000; background: #FFFFFF; border: 1px solid #FFFFFF; cursor: pointer; text-align: center; }
    .btn:hover { background: #000000; color: #FFFFFF; }
    .btn.outline { background: #000000; color: #FFFFFF; border-color: #444444; }
    .btn.outline:hover { border-color: #FFFFFF; }

    .preset-chips { display: flex; gap: 6px; flex-wrap: wrap; margin-top: 8px; }
    .chip { font-size: 9px; background: #111111; border: 1px solid #333333; padding: 6px 10px; cursor: pointer; text-transform: uppercase; font-weight: 700; }
    .chip:hover { border-color: #FFFFFF; }

    .table-container { border: 1px solid #333333; background: #080808; overflow-x: auto; margin-bottom: 16px; }
    table { width: 100%; border-collapse: collapse; font-size: 12px; text-align: left; }
    th, td { padding: 10px 14px; border-bottom: 1px solid #222222; border-right: 1px solid #222222; }
    th:last-child, td:last-child { border-right: none; }
    tr:last-child td { border-bottom: none; }
    th { background: #111111; color: #888888; font-size: 10px; text-transform: uppercase; letter-spacing: 1px; font-weight: 700; }
    td.num { font-weight: 700; color: #FFFFFF; font-size: 13px; }

    footer { border-top: 1px solid #333333; padding: 14px 0; font-size: 10px; color: #666666; display: flex; justify-content: space-between; letter-spacing: 1px; text-transform: uppercase; flex-wrap: wrap; gap: 8px; }
    footer a { color: #FFFFFF; text-decoration: none; border-bottom: 1px solid #FFFFFF; }
  </style>
</head>
<body>
  <div class="container">
    
    <header>
      <div class="title-group">
        <h1>HYDRA // PEOC COMMAND KIOSK</h1>
        <p>3.5" IPS PANEL &bull; 8-BIT DIRECT PARALLEL BUS &bull; TELEMETRY MIRROR</p>
      </div>
      <div class="badges">
        <span class="badge solid"><span class="pulse"></span> LIVE COMMS</span>
        <a href="/api/refresh" class="badge link">&#128260; REFRESH</a>
        <a href="/update" class="badge link">&#9889; WEB OTA</a>
      </div>
    </header>

    <div class="preview-card">
      <div class="preview-label">
        <span>CURRENT SYSTEM STATUS</span>
        <span id="sys-status-badge">ONLINE</span>
      </div>
      <div class="screen-mock">
        <h2 id="modal-title">HYDRA DISASTER COMMAND ACTIVE</h2>
        <p id="modal-desc">Streaming live telemetry from all Level 0 nodes...</p>
      </div>
    </div>

    <!-- LIVE STATIONS TABLE -->
    <div class="section-title">
      <span>01 // REAL-TIME STATION TELEMETRY</span>
      <span id="last-sync-tag">SYNCING...</span>
    </div>

    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>STATION</th>
            <th>STATUS</th>
            <th>PRIMARY METRIC</th>
            <th>SECONDARY METRIC</th>
            <th>HAZARD LEVEL</th>
          </tr>
        </thead>
        <tbody id="telemetry-table-body">
          <tr>
            <td style="color:#00D4FF; font-weight:bold;">01 // FLOOD GAUGE</td>
            <td id="t-flood-status" class="num">CONNECTING</td>
            <td id="t-flood-depth" class="num">-- cm</td>
            <td id="t-flood-clear" class="num">-- cm</td>
            <td id="t-flood-hazard" class="num">--</td>
          </tr>
          <tr>
            <td style="color:#FFB800; font-weight:bold;">02 // FIRE &amp; AIR</td>
            <td id="t-fire-status" class="num">CONNECTING</td>
            <td id="t-fire-temp" class="num">-- °C</td>
            <td id="t-fire-gas" class="num">-- PPM</td>
            <td id="t-fire-hazard" class="num">--</td>
          </tr>
          <tr>
            <td style="color:#00FF87; font-weight:bold;">03 // LANDSLIDE</td>
            <td id="t-land-status" class="num">CONNECTING</td>
            <td id="t-land-accel" class="num">-- g</td>
            <td id="t-land-tilt" class="num">--</td>
            <td id="t-land-hazard" class="num">--</td>
          </tr>
        </tbody>
      </table>
    </div>

    <footer>
      <span>HYDRA LEVEL 2 COMMAND KIOSK &bull; PEOC REGIONAL NETWORK</span>
      <span><a href="/update">[ OVER-THE-AIR FIRMWARE UPDATE ]</a></span>
    </footer>

  </div>

  <script>
    async function pollStatus() {
      try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();

        // Flood
        document.getElementById('t-flood-status').innerText = data.flood.status;
        document.getElementById('t-flood-depth').innerText = data.flood.water_depth_cm + ' cm';
        document.getElementById('t-flood-clear').innerText = 'Clear: ' + data.flood.dist_cm + ' cm';
        document.getElementById('t-flood-hazard').innerText = data.flood.hazard;

        // Fire
        document.getElementById('t-fire-status').innerText = data.fire.status;
        document.getElementById('t-fire-temp').innerText = data.fire.temp_c + ' °C (' + data.fire.humidity + '%)';
        document.getElementById('t-fire-gas').innerText = data.fire.gas_ppm + ' PPM';
        document.getElementById('t-fire-hazard').innerText = data.fire.hazard;

        // Landslide
        document.getElementById('t-land-status').innerText = data.landslide.status;
        document.getElementById('t-land-accel').innerText = data.landslide.accel_g + ' g';
        document.getElementById('t-land-tilt').innerText = 'P:' + data.landslide.pitch + ' R:' + data.landslide.roll;
        document.getElementById('t-land-hazard').innerText = data.landslide.hazard;

        // Summary
        document.getElementById('sys-status-badge').innerText = data.summary.status;
        document.getElementById('last-sync-tag').innerText = 'LATENCY: ' + data.summary.latency_ms + 'ms';
        
        if (data.summary.emergency) {
          document.getElementById('modal-title').innerHTML = '<span style="color:#FF3B3B;">! EMERGENCY ALERT ACTIVE !</span>';
          document.getElementById('modal-desc').innerText = 'Breach active in region: ' + data.summary.region;
        } else {
          document.getElementById('modal-title').innerText = 'ALL STATIONS NOMINAL';
          document.getElementById('modal-desc').innerText = 'Monitoring basin: ' + data.summary.region;
        }
      } catch (e) {
        console.error('Fetch error:', e);
      }
    }
    setInterval(pollStatus, 2500);
    pollStatus();
  </script>
</body>
</html>
)rawliteral";

/* ====================================================================
 * 9. WEB SERVER HANDLERS & OTA
 * ==================================================================== */

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String json = "{";
  json += "\"sys\":{";
  json += "\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptime_sec\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap());
  json += "},";

  json += "\"summary\":{";
  json += "\"region\":\"" + liveSummary.region + "\",";
  json += "\"status\":\"" + liveSummary.overallStatus + "\",";
  json += "\"emergency\":" + String(liveSummary.isEmergency ? "true" : "false") + ",";
  json += "\"latency_ms\":" + String(liveSummary.lastLatencyMs) + ",";
  json += "\"http_code\":" + String(liveSummary.lastHttpCode) + ",";
  json += "\"success_count\":" + String(liveSummary.fetchSuccessCount) + ",";
  json += "\"fail_count\":" + String(liveSummary.fetchFailCount);
  json += "},";

  json += "\"flood\":{";
  json += "\"status\":\"" + liveFlood.status + "\",";
  json += "\"water_depth_cm\":" + String(liveFlood.waterDepthCm, 1) + ",";
  json += "\"dist_cm\":" + String(liveFlood.distCm, 1) + ",";
  json += "\"zone\":\"" + liveFlood.zone + "\",";
  json += "\"hazard\":\"" + liveFlood.hazard + "\"";
  json += "},";

  json += "\"fire\":{";
  json += "\"status\":\"" + liveFire.status + "\",";
  json += "\"temp_c\":" + String(liveFire.tempC, 1) + ",";
  json += "\"humidity\":" + String(liveFire.humidity, 1) + ",";
  json += "\"gas_ppm\":" + String(liveFire.gasPpm, 1) + ",";
  json += "\"air_quality\":\"" + liveFire.airQuality + "\",";
  json += "\"hazard\":\"" + liveFire.hazard + "\"";
  json += "},";

  json += "\"landslide\":{";
  json += "\"status\":\"" + liveLandslide.status + "\",";
  json += "\"accel_g\":" + String(liveLandslide.accelG, 2) + ",";
  json += "\"pitch\":" + String(liveLandslide.pitch, 1) + ",";
  json += "\"roll\":" + String(liveLandslide.roll, 1) + ",";
  json += "\"satellites\":" + String(liveLandslide.satellites) + ",";
  json += "\"gps_fix\":" + String(liveLandslide.gpsFix ? "true" : "false") + ",";
  json += "\"lat\":" + String(liveLandslide.lat, 4) + ",";
  json += "\"lng\":" + String(liveLandslide.lng, 4) + ",";
  json += "\"alt_m\":" + String(liveLandslide.altM, 1) + ",";
  json += "\"hazard\":\"" + liveLandslide.hazard + "\"";
  json += "}";

  json += "}";
  server.send(200, "application/json", json);
}

void handleDisplayUpdate() {
  if (server.hasArg("title")) currentTitle = server.arg("title");
  if (server.hasArg("msg"))   currentMessage = server.arg("msg");
  if (server.hasArg("mode"))  currentMode = server.arg("mode");
  if (server.hasArg("theme")) applyTheme(server.arg("theme"));
  if (server.hasArg("font"))  currentFontSize = server.arg("font").toInt();
  if (server.hasArg("rot")) {
    uint8_t newRot = server.arg("rot").toInt();
    if (newRot != currentRotation) setRotation(newRot);
  }

  Serial.println("[TFT] Updating display from Web request...");
  renderDisplay();
  server.send(200, "application/json", "{\"status\":\"OK\"}");
}

// Embedded Web OTA HTML
const char OTA_INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"><title>ESP32 // OTA UPDATE</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; border-radius: 0 !important; }
    body { background: #000; color: #FFF; font-family: ui-monospace, Menlo, monospace; padding: 24px; }
    .box { max-width: 480px; margin: 40px auto; border: 1px solid #FFF; padding: 24px; background: #080808; }
    h1 { font-size: 16px; margin-bottom: 12px; }
    input[type=file] { width: 100%; border: 1px solid #444; padding: 10px; background: #000; color: #FFF; margin-bottom: 16px; }
    .btn { display: block; width: 100%; padding: 12px; background: #FFF; color: #000; font-weight: 800; border: none; cursor: pointer; text-align: center; text-decoration: none; }
    .btn:hover { background: #000; color: #FFF; outline: 1px solid #FFF; }
  </style>
</head>
<body>
  <div class="box">
    <h1>FIRMWARE FLASH PORTAL (ESP32)</h1>
    <form id="upload_form" enctype="multipart/form-data" method="POST" action="/update">
      <input type="file" name="update" accept=".bin" required>
      <button type="submit" class="btn">[ FLASH .BIN FIRMWARE ]</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

void setupWebOTA() {
  server.on("/update", HTTP_GET, []() {
    if (strlen(otaUsername) > 0 && strlen(otaPassword) > 0) {
      if (!server.authenticate(otaUsername, otaPassword)) {
        return server.requestAuthentication();
      }
    }
    server.send_P(200, "text/html", OTA_INDEX_HTML);
  });

  server.on("/update", HTTP_POST, []() {
    if (strlen(otaUsername) > 0 && strlen(otaPassword) > 0) {
      if (!server.authenticate(otaUsername, otaPassword)) {
        return server.requestAuthentication();
      }
    }
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "OTA_FAIL" : "OTA_OK");
    delay(1000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) Serial.println("[WebOTA] Flash complete. Rebooting...");
      else Update.printError(Serial);
    }
  });
}

/* ====================================================================
 * 10. SETUP & MAIN LOOP
 * ==================================================================== */

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==============================================");
  Serial.println("  HYDRA LEVEL 2 PEOC KIOSK // 3.5\" ILI9488   ");
  Serial.println("==============================================");

  // 1. Initialize ILI9488 8-bit parallel display
  tftInit();
  applyTheme("CYAN");

  // 2. HYDRA Animated Boot Up Screen
  renderBootScreen(15, "INITIALIZING 8-BIT PARALLEL TFT BUS... [OK]");
  delay(600);

  renderBootScreen(35, "STARTING ARDUINO NANO ESP32 SUBSYSTEMS... [OK]");
  delay(400);

  renderBootScreen(55, "CONNECTING TO WI-FI: 'sakshyam'...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Connecting to '%s'", WIFI_SSID);

  unsigned long startAttemptTime = millis();
  int wifiProgress = 55;
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    wifiProgress = min(80, wifiProgress + 3);
    renderBootScreen(wifiProgress, "CONNECTING TO WI-FI ROUTER...");
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    renderBootScreen(85, "WI-FI LINK ESTABLISHED! ASSIGNED IP.");
    Serial.print("[WIFI] Connected! Display Web IP: ");
    Serial.println(WiFi.localIP());
    delay(400);
  } else {
    renderBootScreen(85, "ROUTER TIMEOUT. LAUNCHING ACCESS POINT...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    delay(400);
  }

  // Setup mDNS
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("[mDNS] Responding at: http://%s.local\n", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  // Setup Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/display", HTTP_GET, handleDisplayUpdate);
  server.on("/api/refresh", HTTP_GET, []() {
    fetchTelemetryFeed();
    renderDisplay();
    server.send(200, "application/json", "{\"status\":\"REFRESHED\"}");
  });
  setupWebOTA();
  server.begin();
  Serial.println("[HTTP] Web server listening on port 80.");

  // Fetch initial telemetry from HYDRA server
  if (WiFi.status() == WL_CONNECTED) {
    renderBootScreen(95, "FETCHING LIVE TELEMETRY FEED FROM SERVER...");
    fetchTelemetryFeed();
    delay(500);
  }

  renderBootScreen(100, "BOOT COMPLETE. LAUNCHING HYDRA DISASTER COMMAND...");
  delay(1000);

  // Transition to data / alert screen
  bool isAlert = liveSummary.isEmergency || 
                 (liveFlood.hazard == "CRITICAL" || liveFlood.hazard == "HIGH") ||
                 (liveLandslide.hazard == "CRITICAL" || liveLandslide.hazard == "HIGH") ||
                 (liveFire.hazard == "CRITICAL" || liveFire.hazard == "HIGH");
  currentScreenState = isAlert ? STATE_ALERT : STATE_NORMAL;
  fillScreen(COLOR_BG);

  renderDisplay();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  // Periodic Telemetry Ingest & TFT Dashboard Refresh
  unsigned long now = millis();
  if (now - lastServerPollTime >= SERVER_POLL_INTERVAL_MS) {
    lastServerPollTime = now;
    if (WiFi.status() == WL_CONNECTED) {
      fetchTelemetryFeed();
    }

    bool isAlert = liveSummary.isEmergency || 
                   (liveFlood.hazard == "CRITICAL" || liveFlood.hazard == "HIGH") ||
                   (liveLandslide.hazard == "CRITICAL" || liveLandslide.hazard == "HIGH") ||
                   (liveFire.hazard == "CRITICAL" || liveFire.hazard == "HIGH");

    DisplayState targetState = isAlert ? STATE_ALERT : STATE_NORMAL;

    if (targetState != currentScreenState) {
      currentScreenState = targetState;
      fillScreen(COLOR_BG); // Clean transition between Normal & Alert
    }

    renderDisplay();
  }
}
