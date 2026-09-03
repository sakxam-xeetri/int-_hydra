/* ====================================================================
 * HYDRA — LEVEL 2 PEOC URBAN COMMAND HUB & REAL-TIME TELEMETRY DISPLAY
 * Hardware Platform  : Arduino Nano ESP32 (ESP32-S3)
 * Display Controller : ILI9488 3.5" IPS TFT LCD (8-bit Parallel Mode, 480x320)
 * API Endpoint       : https://zenithkandel.com.np/hydra/backend/api/nodes/level2.php
 * Wi-Fi Credentials  : SSID "sakshyam" | Password "sakshyam"
 * Aesthetic Style    : Deep Black Background, Crisp White & Terminal Green Text,
 *                      Dynamic Alert Pop-up Overlay for Emergency Events
 * ==================================================================== */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>

/* ====================================================================
 * 1. HARDWARE PIN DEFINITIONS (ARDUINO NANO ESP32 / ESP32-S3)
 * ==================================================================== */
#define TFT_RD   1   // Silk Screen: A0 (Native GPIO 1)
#define TFT_WR   2   // Silk Screen: A1 (Native GPIO 2)
#define TFT_RS   3   // Silk Screen: A2 (Native GPIO 3) - Command / Data
#define TFT_CS   4   // Silk Screen: A3 (Native GPIO 4) - Chip Select
#define TFT_RST  11  // Silk Screen: A4 (Native GPIO 11) - Reset

// 8-Bit Parallel Data Bus (D0 through D7)
#define TFT_D0   5   // Silk Screen: D2
#define TFT_D1   6   // Silk Screen: D3
#define TFT_D2   7   // Silk Screen: D4
#define TFT_D3   8   // Silk Screen: D5
#define TFT_D4   9   // Silk Screen: D6
#define TFT_D5   10  // Silk Screen: D7
#define TFT_D6   17  // Silk Screen: D8
#define TFT_D7   18  // Silk Screen: D9

const uint8_t dataPins[8] = { TFT_D0, TFT_D1, TFT_D2, TFT_D3,
                             TFT_D4, TFT_D5, TFT_D6, TFT_D7 };

/* ====================================================================
 * 2. NETWORK & API CONFIGURATION
 * ==================================================================== */
const char* WIFI_SSID     = "sakshyam";
const char* WIFI_PASSWORD = "sakshyam";

// Fallback SoftAP
const char* AP_SSID       = "HYDRA-PEOC-HUB";
const char* AP_PASSWORD   = "12345678";
const char* MDNS_HOSTNAME = "hydra-display";

// HYDRA Production Level 2 Endpoint (per a.md Section 1 & Section 4)
const char* API_LEVEL2_URL = "https://zenithkandel.com.np/hydra/backend/api/nodes/level2.php";
const unsigned long API_POLL_INTERVAL_MS = 2500; // Poll every 2.5 seconds
unsigned long lastApiPollTime = 0;

// Web Server for Live Diagnostics & Web OTA
WebServer server(80);
const char* otaUsername = "admin";
const char* otaPassword = "admin";

/* ====================================================================
 * 3. COLOR DEFINITIONS (RGB565)
 * ==================================================================== */
#define RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

#define COLOR_BLACK          0x0000
#define COLOR_WHITE          0xFFFF

// High-Contrast Green Palette for White Background (Crisp & Highly Readable)
#define COLOR_GREEN_DARK     RGB565(0, 110, 35)    // Deep Emerald Green for Titles & Headers
#define COLOR_GREEN_MAIN     RGB565(0, 150, 45)    // Vibrant Forest Green for Sensor Readouts
#define COLOR_GREEN_BORDER   RGB565(0, 160, 60)    // Crisp Green for Card Outlines & Dividers
#define COLOR_GREEN_MUTED    RGB565(40, 130, 65)   // Muted Green for Secondary Details
#define COLOR_GREEN_TERMINAL RGB565(0, 255, 128)   // Terminal Matrix Green (Boot & Alerts)

// Compatibility aliases
#define COLOR_GREEN          COLOR_GREEN_MAIN
#define COLOR_DARK_GREEN     COLOR_GREEN_DARK
#define COLOR_PANEL_BG       RGB565(8, 14, 10)     // Deep Cybernetic Black-Green (Alert Box)

#define COLOR_RED            RGB565(220, 30, 30)   // Critical Emergency Red
#define COLOR_AMBER          RGB565(240, 150, 0)   // Warning Amber
#define COLOR_CYAN           RGB565(0, 180, 220)   // Secondary Cyan
#define COLOR_DARK_GRAY      RGB565(90, 100, 95)   // Soft Gray

int screenWidth  = 480;
int screenHeight = 320;

/* ====================================================================
 * 4. TELEMETRY DATA STRUCTURES (LEVEL 2 CITY HUB)
 * ==================================================================== */
struct FloodStation {
  String nodeUid      = "NODE-FLOOD-01";
  String name         = "MODI KHOLA SURGE";
  String status       = "STANDBY";
  float waterDepthCm  = 0.0;
  float clearanceCm   = 0.0;
  String radarZone    = "NO ECHO / STANDBY";
  String lastSync     = "--:--:--";
} floodData;

struct FireStation {
  String nodeUid      = "NODE-FIRE-01";
  String name         = "PINE RIDGE FOREST";
  String status       = "STANDBY";
  float tempC         = 0.0;
  float humidityPct   = 0.0;
  float gasPpm        = 0.0;
  String airQuality   = "Calibrating";
  String lastSync     = "--:--:--";
} fireData;

struct LandslideStation {
  String nodeUid      = "NODE-LANDSLIDE-01";
  String name         = "ANNAPURNA ESCARPMENT";
  String status       = "STANDBY";
  bool gpsConnected   = false;
  bool gpsFix         = false;
  int satellites      = 0;
  float lat           = 0.0;
  float lng           = 0.0;
  float altM          = 0.0;
  float speedKmh      = 0.0;
  float pitchDeg      = 0.0;
  float rollDeg       = 0.0;
  float tiltDeg       = 0.0;
  float totalAccelG   = 1.0;
  String lastSync     = "--:--:--";
} landslideData;

struct SystemStatus {
  String systemState       = "BOOTING";
  bool isEmergency         = false;
  String emergencySummary  = "NONE";
  String sirenState        = "OFF";
  String villageNodeLink   = "OFFLINE";
  unsigned long syncCount  = 0;
  unsigned long failCount  = 0;
  int lastHttpCode         = 0;
  unsigned long lastLatencyMs = 0;
} sysStatus;

// Screen Modes
enum DisplayScreen {
  SCREEN_BOOT,
  SCREEN_DASHBOARD,
  SCREEN_ALERT
};
DisplayScreen currentScreen = SCREEN_BOOT;
unsigned long lastScreenSwitchTime = 0;
bool alertDisplayState = false; // Alternates between alert overlay and data screen during active emergency

/* ====================================================================
 * 5. FONT 5x7 BITMAP TABLE
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
    {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F}, {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02}, {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00}, {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7F, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x10, 0x08, 0x08, 0x10, 0x08}, {0x00, 0x00, 0x00, 0x00, 0x00}
};

/* ====================================================================
 * 6. LOW-LEVEL HIGH-SPEED ILI9488 8-BIT PARALLEL DRIVER
 * ==================================================================== */
inline void writeData8(uint8_t d) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(dataPins[i], (d >> i) & 1);
  }
  digitalWrite(TFT_WR, LOW);
  digitalWrite(TFT_WR, HIGH);
}

void writeCommand(uint8_t c) {
  digitalWrite(TFT_RS, LOW);
  digitalWrite(TFT_CS, LOW);
  writeData8(c);
  digitalWrite(TFT_CS, HIGH);
}

void writeDataByte(uint8_t d) {
  digitalWrite(TFT_RS, HIGH);
  digitalWrite(TFT_CS, LOW);
  writeData8(d);
  digitalWrite(TFT_CS, HIGH);
}

void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  writeCommand(0x2A);
  writeDataByte(x0 >> 8);
  writeDataByte(x0 & 0xFF);
  writeDataByte(x1 >> 8);
  writeDataByte(x1 & 0xFF);
  writeCommand(0x2B);
  writeDataByte(y0 >> 8);
  writeDataByte(y0 & 0xFF);
  writeDataByte(y1 >> 8);
  writeDataByte(y1 & 0xFF);
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

void setRotationLandscape() {
  writeCommand(0x36);
  writeDataByte(0x28); // Landscape 480x320
  screenWidth  = 480;
  screenHeight = 320;
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

  // Hardware Reset
  digitalWrite(TFT_RST, HIGH);
  delay(50);
  digitalWrite(TFT_RST, LOW);
  delay(120);
  digitalWrite(TFT_RST, HIGH);
  delay(120);

  writeCommand(0x01); delay(120); // Software Reset
  writeCommand(0x11); delay(120); // Sleep Out
  writeCommand(0x3A); writeDataByte(0x55); // 16-bit Color
  setRotationLandscape();
  writeCommand(0x29); delay(50);  // Display ON

  fillScreen(COLOR_BLACK);
  Serial.println("[TFT] ILI9488 8-bit parallel bus initialized successfully.");
}

/* ====================================================================
 * 7. GRAPHICS & TYPOGRAPHY PRIMITIVES
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

void drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
  if (c < 32 || c > 127) c = ' ';
  uint8_t idx = c - 32;
  // Pre-fill character bounding cell to avoid ghost pixels during updates
  fillRect(x, y, 6 * size, 8 * size, bg);
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

void drawText(int16_t x, int16_t y, const char *text, uint16_t color, uint16_t bg, uint8_t size) {
  while (*text) {
    drawChar(x, y, *text++, color, bg, size);
    x += 6 * size;
  }
}

void drawTextCentered(int16_t y, const char *text, uint16_t color, uint8_t size) {
  int16_t x = (screenWidth - strlen(text) * 6 * size) / 2;
  if (x < 0) x = 0;
  drawText(x, y, text, color, size);
}

void drawTextCentered(int16_t y, const char *text, uint16_t color, uint16_t bg, uint8_t size) {
  int16_t x = (screenWidth - strlen(text) * 6 * size) / 2;
  if (x < 0) x = 0;
  drawText(x, y, text, color, bg, size);
}

/* ====================================================================
 * 8. UI SCREEN RENDERERS
 * ==================================================================== */

// --- BOOT UP SCREEN (Black BG, Large Crisp White HYDRA Branding) ---
int bootLogY = 148;
void bootLogMessage(const char* label, const char* status, uint16_t statusColor = COLOR_GREEN_TERMINAL) {
  drawText(30, bootLogY, label, COLOR_WHITE, COLOR_BLACK, 1);
  drawText(390, bootLogY, status, statusColor, COLOR_BLACK, 1);
  bootLogY += 18;
  delay(120);
}

void renderBootScreenHeader() {
  fillScreen(COLOR_BLACK);

  // Large White HYDRA Title (Size 6: ~174px wide, 42px tall)
  drawTextCentered(38, "HYDRA", COLOR_WHITE, COLOR_BLACK, 6);

  // Clean, modern white subtitle
  drawTextCentered(96, "EARLY WARNING & DISASTER MITIGATION NETWORK", COLOR_WHITE, COLOR_BLACK, 1);
  drawTextCentered(112, "LEVEL 2 PEOC COMMAND NODE  //  ARDUINO NANO ESP32", COLOR_WHITE, COLOR_BLACK, 1);

  // Clean minimalist white divider
  drawHLine(30, 132, screenWidth - 60, COLOR_WHITE);
  bootLogY = 148;

  drawTextCentered(296, "SYSTEM INITIALIZING...", COLOR_WHITE, COLOR_BLACK, 1);
}

// --- MAIN TELEMETRY DATA SCREEN (Black BG, Crisp White Sensor Text) ---
bool dashboardStaticDrawn = false;

void renderDataScreenStatic() {
  fillScreen(COLOR_BLACK);

  // 1. Top Header Bar (Y: 0 to 32)
  drawHLine(0, 32, screenWidth, COLOR_GREEN_BORDER);
  drawHLine(0, 33, screenWidth, COLOR_GREEN_BORDER);
  drawText(12, 8, "HYDRA SENSOR NODE", COLOR_WHITE, COLOR_BLACK, 2);

  // 2. Card 1: Environment Sensors / Fire (Top Left: X=8, Y=40, W=228, H=122)
  drawRect(8, 40, 228, 122, COLOR_GREEN_BORDER);
  drawRect(9, 41, 226, 120, COLOR_GREEN_BORDER);
  drawText(16, 46, "[ 01 ENVIRONMENT SENSORS ]", COLOR_WHITE, COLOR_BLACK, 1);
  drawHLine(8, 58, 228, COLOR_GREEN_BORDER);

  drawText(16, 66, "TEMP :", COLOR_WHITE, COLOR_BLACK, 2);
  drawText(16, 90, "HUM  :", COLOR_WHITE, COLOR_BLACK, 2);
  drawText(16, 114, "GAS/SMOKE: ", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(16, 128, "AIR QUAL : ", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(16, 142, "STATION  : PINE RIDGE (FIRE-01)", COLOR_WHITE, COLOR_BLACK, 1);

  // 3. Card 2: Hydrology & Flood (Top Right: X=244, Y=40, W=228, H=122)
  drawRect(244, 40, 228, 122, COLOR_GREEN_BORDER);
  drawRect(245, 41, 226, 120, COLOR_GREEN_BORDER);
  drawText(252, 46, "[ 02 WATER LEVEL & FLOOD ]", COLOR_WHITE, COLOR_BLACK, 1);
  drawHLine(244, 58, 228, COLOR_GREEN_BORDER);

  drawText(252, 66, "DEPTH:", COLOR_WHITE, COLOR_BLACK, 2);
  drawText(252, 90, "CLEAR:", COLOR_WHITE, COLOR_BLACK, 2);
  drawText(252, 114, "RADAR ZONE: ", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(252, 128, "SURGE STAT: ", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(252, 142, "STATION   : MODI KHOLA (FLOOD-01)", COLOR_WHITE, COLOR_BLACK, 1);

  // 4. Card 3: Geolocation & Motion (Bottom Full Width: X=8, Y=168, W=464, H=118)
  drawRect(8, 168, 464, 118, COLOR_GREEN_BORDER);
  drawRect(9, 169, 462, 116, COLOR_GREEN_BORDER);
  drawText(16, 174, "[ 03 GEOLOCATION & MOTION // LANDSLIDE ]", COLOR_WHITE, COLOR_BLACK, 1);
  drawHLine(8, 186, 464, COLOR_GREEN_BORDER);
  drawVLine(240, 186, 100, COLOR_GREEN_BORDER);

  // Left Sub-Column (GPS)
  drawText(16, 194, "LAT :", COLOR_WHITE, COLOR_BLACK, 2);
  drawText(16, 218, "LONG:", COLOR_WHITE, COLOR_BLACK, 2);
  drawText(16, 242, "ALTITUDE : ", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(16, 256, "SATS/SPD : ", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(16, 270, "GPS LINK : ", COLOR_WHITE, COLOR_BLACK, 1);

  // Right Sub-Column (IMU)
  drawText(248, 194, "TILT:", COLOR_WHITE, COLOR_BLACK, 2);
  drawText(248, 218, "PITCH/ROLL: ", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(248, 234, "TOTAL ACC : ", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(248, 250, "MPU-6050  : 6-DOF SENSOR ACTIVE", COLOR_WHITE, COLOR_BLACK, 1);
  drawText(248, 268, "SEISMIC   : ", COLOR_WHITE, COLOR_BLACK, 1);

  // 5. Footer Bar (Y: 294 to 320)
  drawHLine(0, 294, screenWidth, COLOR_GREEN_BORDER);
  drawHLine(0, 295, screenWidth, COLOR_GREEN_BORDER);

  dashboardStaticDrawn = true;
}

void renderDataScreenValues() {
  // Header Values
  String ipStr = "IP: " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("AP-MODE"));
  char ipBuf[32];
  snprintf(ipBuf, sizeof(ipBuf), "%-22s", ipStr.c_str());
  drawText(280, 6, ipBuf, COLOR_WHITE, COLOR_BLACK, 1);

  String sysBadge = sysStatus.isEmergency ? "STATUS: EMERGENCY!" : "STATUS: NOMINAL";
  char badgeBuf[32];
  snprintf(badgeBuf, sizeof(badgeBuf), "%-22s", sysBadge.c_str());
  drawText(280, 18, badgeBuf, sysStatus.isEmergency ? COLOR_RED : COLOR_WHITE, COLOR_BLACK, 1);

  // Card 1: Environment Values (Fire Station)
  char tempStr[16];
  snprintf(tempStr, sizeof(tempStr), "%-7.1f C", fireData.tempC);
  drawText(92, 66, tempStr, COLOR_WHITE, COLOR_BLACK, 2);

  char humStr[16];
  snprintf(humStr, sizeof(humStr), "%-7.1f %%", fireData.humidityPct);
  drawText(92, 90, humStr, COLOR_WHITE, COLOR_BLACK, 2);

  char gasStr[24];
  snprintf(gasStr, sizeof(gasStr), "%-14.1f PPM", fireData.gasPpm);
  drawText(92, 114, gasStr, (fireData.gasPpm > 400.0) ? COLOR_RED : COLOR_WHITE, COLOR_BLACK, 1);

  char airBuf[24];
  snprintf(airBuf, sizeof(airBuf), "%-14s", fireData.airQuality.substring(0, 14).c_str());
  drawText(92, 128, airBuf, COLOR_WHITE, COLOR_BLACK, 1);

  // Card 2: Flood Values
  char depthStr[16];
  snprintf(depthStr, sizeof(depthStr), "%-7.1f cm", floodData.waterDepthCm);
  drawText(328, 66, depthStr, (floodData.waterDepthCm > 250.0) ? COLOR_RED : COLOR_WHITE, COLOR_BLACK, 2);

  char clearStr[16];
  snprintf(clearStr, sizeof(clearStr), "%-7.1f cm", floodData.clearanceCm);
  drawText(328, 90, clearStr, COLOR_WHITE, COLOR_BLACK, 2);

  char zoneBuf[20];
  snprintf(zoneBuf, sizeof(zoneBuf), "%-12s", floodData.radarZone.substring(0, 12).c_str());
  drawText(330, 114, zoneBuf, COLOR_WHITE, COLOR_BLACK, 1);

  char fstatBuf[20];
  snprintf(fstatBuf, sizeof(fstatBuf), "%-12s", floodData.status.c_str());
  drawText(330, 128, fstatBuf, (floodData.status == "CRITICAL_BREACH") ? COLOR_RED : COLOR_WHITE, COLOR_BLACK, 1);

  // Card 3: Geolocation & Motion Values
  // Left Sub-Column (GPS)
  char latStr[20];
  snprintf(latStr, sizeof(latStr), "%-11.5f N", landslideData.lat);
  drawText(80, 194, latStr, COLOR_WHITE, COLOR_BLACK, 2);

  char lngStr[20];
  snprintf(lngStr, sizeof(lngStr), "%-11.5f E", landslideData.lng);
  drawText(80, 218, lngStr, COLOR_WHITE, COLOR_BLACK, 2);

  char altStr[24];
  snprintf(altStr, sizeof(altStr), "%-14.1f m", landslideData.altM);
  drawText(90, 242, altStr, COLOR_WHITE, COLOR_BLACK, 1);

  char satStr[28];
  snprintf(satStr, sizeof(satStr), "%d SATS | %.1f km/h  ", landslideData.satellites, landslideData.speedKmh);
  drawText(90, 256, satStr, COLOR_WHITE, COLOR_BLACK, 1);

  const char* fixStr = landslideData.gpsFix ? "FIX 3D ACQUIRED  " : "SEARCHING LOCK...";
  drawText(90, 270, fixStr, COLOR_WHITE, COLOR_BLACK, 1);

  // Right Sub-Column (IMU)
  char tiltStr[16];
  snprintf(tiltStr, sizeof(tiltStr), "%-7.1f deg", landslideData.tiltDeg);
  drawText(312, 194, tiltStr, (landslideData.tiltDeg > 15.0) ? COLOR_RED : COLOR_WHITE, COLOR_BLACK, 2);

  char prStr[32];
  snprintf(prStr, sizeof(prStr), "%+5.1f / %+5.1f deg ", landslideData.pitchDeg, landslideData.rollDeg);
  drawText(326, 218, prStr, COLOR_WHITE, COLOR_BLACK, 1);

  char accStr[28];
  snprintf(accStr, sizeof(accStr), "%.2f G (%s)   ", landslideData.totalAccelG, (landslideData.totalAccelG > 1.8 ? "SHOCK" : "NOMINAL"));
  drawText(326, 234, accStr, COLOR_WHITE, COLOR_BLACK, 1);

  const char* seisStr = (landslideData.tiltDeg > 15.0) ? "ALERT: DISPLACEMENT" : "NORMAL / STABLE    ";
  drawText(326, 268, seisStr, (landslideData.tiltDeg > 15.0) ? COLOR_RED : COLOR_WHITE, COLOR_BLACK, 1);

  // Footer Bar Values
  char sirenBuf[32];
  snprintf(sirenBuf, sizeof(sirenBuf), "SIREN: [%s]  ", sysStatus.sirenState.c_str());
  drawText(12, 302, sirenBuf, (sysStatus.sirenState == "ON" ? COLOR_RED : COLOR_WHITE), COLOR_BLACK, 1);

  char linkBuf[36];
  snprintf(linkBuf, sizeof(linkBuf), "VILLAGE LINK: %-8s", sysStatus.villageNodeLink.c_str());
  drawText(130, 302, linkBuf, COLOR_WHITE, COLOR_BLACK, 1);

  char pollBuf[48];
  unsigned long uptimeSec = millis() / 1000;
  snprintf(pollBuf, sizeof(pollBuf), "SYNC #%lu (UP: %02lu:%02lu:%02lu) ", sysStatus.syncCount, uptimeSec / 3600, (uptimeSec % 3600) / 60, uptimeSec % 60);
  drawText(285, 302, pollBuf, COLOR_WHITE, COLOR_BLACK, 1);
}

void renderDataScreen(bool forceRedraw = false) {
  if (forceRedraw || !dashboardStaticDrawn) {
    renderDataScreenStatic();
  }
  renderDataScreenValues();
}

// --- CRITICAL ALERT POP-UP SCREEN ---
void renderAlertScreen() {
  fillScreen(COLOR_BLACK);

  // Flashing Emergency Border
  drawRect(6, 6, screenWidth - 12, screenHeight - 12, COLOR_RED);
  drawRect(8, 8, screenWidth - 16, screenHeight - 16, COLOR_AMBER);
  drawRect(10, 10, screenWidth - 20, screenHeight - 20, COLOR_RED);

  // Banner
  fillRect(14, 14, screenWidth - 28, 42, COLOR_RED);
  drawTextCentered(24, "!!! CRITICAL DISASTER ALERT !!!", COLOR_BLACK, 2);
  drawTextCentered(42, "IMMEDIATE EVACUATION & RESPONSE PROTOCOL", COLOR_BLACK, 1);

  // Emergency Details Box
  drawRect(20, 68, screenWidth - 40, 170, COLOR_DARK_GREEN);
  fillRect(22, 70, screenWidth - 44, 166, COLOR_PANEL_BG);

  String emTypeStr = "HAZARD DETECTED: " + sysStatus.emergencySummary;
  drawText(34, 84, emTypeStr.c_str(), COLOR_RED, 2);

  drawHLine(34, 110, screenWidth - 68, COLOR_RED);

  if (sysStatus.emergencySummary.indexOf("FLOOD") != -1) {
    char floodMsg[64];
    snprintf(floodMsg, sizeof(floodMsg), "> MODI KHOLA WATER DEPTH: %.1f cm (DANGER CRITICAL)", floodData.waterDepthCm);
    drawText(34, 122, floodMsg, COLOR_WHITE, 1);
  }
  if (sysStatus.emergencySummary.indexOf("FIRE") != -1) {
    char fireMsg[64];
    snprintf(fireMsg, sizeof(fireMsg), "> PINE RIDGE SMOKE/GAS: %.1f PPM | TEMP: %.1f C", fireData.gasPpm, fireData.tempC);
    drawText(34, 138, fireMsg, COLOR_WHITE, 1);
  }
  if (sysStatus.emergencySummary.indexOf("LANDSLIDE") != -1) {
    char lsMsg[64];
    snprintf(lsMsg, sizeof(lsMsg), "> ANNAPURNA ESCARPMENT TILT: %.1f deg DEVIATION", landslideData.tiltDeg);
    drawText(34, 154, lsMsg, COLOR_WHITE, 1);
  }

  char sirenInfo[64];
  snprintf(sirenInfo, sizeof(sirenInfo), "> VILLAGE SIREN: %s  |  GSM DISPATCH: ACTIVE", sysStatus.sirenState.c_str());
  drawText(34, 172, sirenInfo, COLOR_AMBER, 1);

  drawText(34, 192, "> SAR UNITS NOTIFIED: Armed Police Force / Red Cross", COLOR_GREEN, 1);
  drawText(34, 208, "> DISPATCH INSTRUCTIONS: Move citizens to designated shelters", COLOR_WHITE, 1);

  // Bottom Notice
  drawTextCentered(252, "DISPLAYING LIVE TELEMETRY INTERVALS (AUTO-SWITCHING)", COLOR_WHITE, 1);
  drawTextCentered(270, "PRESS ANY KEY OR WAIT FOR AUTOMATIC CYCLE", COLOR_GREEN, 1);

  drawHLine(20, 290, screenWidth - 40, COLOR_DARK_GREEN);
  drawText(20, 298, "HYDRA PEOC COMMAND HUB", COLOR_GREEN, 1);
  drawText(screenWidth - 140, 298, "INCIDENT ACTIVE", COLOR_RED, 1);
}

/* ====================================================================
 * 9. HTTP GET TELEMETRY CONSUMER (LEVEL 2 API)
 * ==================================================================== */
void fetchLevel2Telemetry() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // Bypass CA certificate bundle validation

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3500);

  unsigned long startMs = millis();
  if (!http.begin(client, API_LEVEL2_URL)) {
    Serial.println("[HTTP] Unable to connect to Level 2 API endpoint.");
    sysStatus.failCount++;
    return;
  }

  http.addHeader("Accept", "application/json");
  int httpCode = http.GET();
  sysStatus.lastHttpCode = httpCode;
  sysStatus.lastLatencyMs = millis() - startMs;

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    // Use ArduinoJson filter to safely parse only the fields we need
    JsonDocument filter;
    filter["data"]["system_status"] = true;
    filter["data"]["emergency"] = true;
    filter["data"]["emergency_types"] = true;
    filter["data"]["siren"] = true;
    filter["data"]["village_node_status"]["link_status"] = true;
    filter["data"]["nodes_telemetry"]["level_0_flood"] = true;
    filter["data"]["nodes_telemetry"]["level_0_fire"] = true;
    filter["data"]["nodes_telemetry"]["level_0_landslide"]["gps"] = true;
    filter["data"]["nodes_telemetry"]["level_0_landslide"]["mpu_imu"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

    if (!err && doc["status"] == "SUCCESS") {
      JsonObject data = doc["data"];

      sysStatus.systemState = data["system_status"] | "NOMINAL";
      sysStatus.isEmergency = data["emergency"] | false;
      sysStatus.sirenState  = data["siren"] | "OFF";
      sysStatus.villageNodeLink = data["village_node_status"]["link_status"] | "ONLINE";

      // Parse Emergency Types
      JsonArray emTypes = data["emergency_types"];
      if (emTypes.size() > 0) {
        String typesStr = "";
        for (JsonVariant v : emTypes) {
          if (typesStr.length() > 0) typesStr += " + ";
          typesStr += v.as<String>();
        }
        sysStatus.emergencySummary = typesStr;
      } else {
        sysStatus.emergencySummary = "NONE";
      }

      // 1. Flood Station
      JsonObject flood = data["nodes_telemetry"]["level_0_flood"];
      if (!flood.isNull()) {
        floodData.status       = flood["status"] | "ONLINE";
        floodData.waterDepthCm = flood["water_depth_cm"] | 0.0f;
        floodData.clearanceCm  = flood["hardware_distance"] | 0.0f;
        floodData.radarZone    = flood["radar_zone"] | "STANDBY";
        floodData.lastSync     = flood["last_reading"] | "--:--:--";
      }

      // 2. Fire Station
      JsonObject fire = data["nodes_telemetry"]["level_0_fire"];
      if (!fire.isNull()) {
        fireData.status       = fire["status"] | "ONLINE";
        fireData.tempC        = fire["temperature_c"] | 0.0f;
        fireData.humidityPct  = fire["humidity_pct"] | 0.0f;
        fireData.gasPpm       = fire["gas_ppm"] | 0.0f;
        fireData.airQuality   = fire["air_quality_status"] | "Normal";
        fireData.lastSync     = fire["last_reading"] | "--:--:--";
      }

      // 3. Landslide Station
      JsonObject ls = data["nodes_telemetry"]["level_0_landslide"];
      if (!ls.isNull()) {
        JsonObject gps = ls["gps"];
        if (!gps.isNull()) {
          landslideData.gpsConnected = gps["connected"] | false;
          landslideData.gpsFix       = gps["fix"] | false;
          landslideData.satellites   = gps["satellites"] | 0;
          landslideData.lat          = gps["latitude"] | 0.0f;
          landslideData.lng          = gps["longitude"] | 0.0f;
          landslideData.altM         = gps["altitude_m"] | 0.0f;
          landslideData.speedKmh     = gps["speed_kmh"] | 0.0f;
        }
        JsonObject imu = ls["mpu_imu"];
        if (!imu.isNull()) {
          landslideData.pitchDeg    = imu["pitch_deg"] | 0.0f;
          landslideData.rollDeg     = imu["roll_deg"] | 0.0f;
          landslideData.tiltDeg     = imu["tilt_deg"] | 0.0f;
          landslideData.totalAccelG = imu["total_accel_g"] | 1.0f;
        }
        landslideData.lastSync = ls["last_reading"] | "--:--:--";
      }

      sysStatus.syncCount++;
      Serial.printf("[API] Sync #%lu OK (%lu ms) | Sys: %s | Emergency: %s | Flood: %.1fcm | Temp: %.1fC\n",
        sysStatus.syncCount, sysStatus.lastLatencyMs, sysStatus.systemState.c_str(),
        sysStatus.isEmergency ? "YES" : "NO", floodData.waterDepthCm, fireData.tempC);
    } else {
      Serial.printf("[API] JSON Deserialization error: %s\n", err.c_str());
      sysStatus.failCount++;
    }
  } else {
    Serial.printf("[API] HTTP GET failed: code %d\n", httpCode);
    sysStatus.failCount++;
  }
  http.end();
}

/* ====================================================================
 * 10. WEB DASHBOARD & WEB OTA SUPPORT
 * ==================================================================== */
void handleWebStatus() {
  JsonDocument doc;
  doc["system"] = sysStatus.systemState;
  doc["emergency"] = sysStatus.isEmergency;
  doc["emergency_summary"] = sysStatus.emergencySummary;
  doc["siren"] = sysStatus.sirenState;
  doc["flood_depth_cm"] = floodData.waterDepthCm;
  doc["fire_temp_c"] = fireData.tempC;
  doc["fire_gas_ppm"] = fireData.gasPpm;
  doc["landslide_tilt_deg"] = landslideData.tiltDeg;
  doc["uptime_sec"] = millis() / 1000;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleWebRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>HYDRA PEOC COMMAND HUB</title><style>";
  html += "body{background:#050505;color:#00FF80;font-family:monospace;padding:20px;}";
  html += ".card{border:1px solid #00FF80;padding:15px;margin-bottom:15px;background:#0A120D;}";
  html += "h1{color:#FFF;font-size:20px;margin-bottom:10px;}h2{color:#00FF80;font-size:16px;}";
  html += ".alert{color:#FF3333;font-weight:bold;}";
  html += "</style></head><body>";
  html += "<h1>HYDRA LEVEL 2 PEOC COMMAND HUB</h1>";
  html += "<div class='card'><h2>SYSTEM STATE: " + sysStatus.systemState + "</h2>";
  if (sysStatus.isEmergency) {
    html += "<p class='alert'>*** CRITICAL EMERGENCY ACTIVE: " + sysStatus.emergencySummary + " ***</p>";
  } else {
    html += "<p>STATUS: ALL SYSTEMS NOMINAL</p>";
  }
  html += "<p>SIREN: " + sysStatus.sirenState + " | LINK: " + sysStatus.villageNodeLink + "</p></div>";
  html += "<div class='card'><h2>01 FLOOD // MODI KHOLA</h2><p>DEPTH: " + String(floodData.waterDepthCm) + " cm | ZONE: " + floodData.radarZone + "</p></div>";
  html += "<div class='card'><h2>02 FIRE // PINE RIDGE</h2><p>TEMP: " + String(fireData.tempC) + " C | HUMIDITY: " + String(fireData.humidityPct) + "% | GAS: " + String(fireData.gasPpm) + " PPM</p></div>";
  html += "<div class='card'><h2>03 LANDSLIDE // ANNAPURNA</h2><p>TILT: " + String(landslideData.tiltDeg) + " deg | ACCEL: " + String(landslideData.totalAccelG) + " G | GPS: " + String(landslideData.lat, 5) + ", " + String(landslideData.lng, 5) + "</p></div>";
  html += "<p><a href='/update' style='color:#FFF;'>[OTA Firmware Update]</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setupWebOTA() {
  server.on("/update", HTTP_GET, []() {
    if (!server.authenticate(otaUsername, otaPassword)) return server.requestAuthentication();
    String otaHtml = "<html><body style='background:#000;color:#0F8;font-family:monospace;padding:20px;'>";
    otaHtml += "<h2>HYDRA FIRMWARE OTA UPGRADE</h2>";
    otaHtml += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    otaHtml += "<input type='file' name='update'><br><br>";
    otaHtml += "<input type='submit' value='Upload Binary' style='padding:10px 20px;background:#0F8;color:#000;font-weight:bold;cursor:pointer;'>";
    otaHtml += "</form></body></html>";
    server.send(200, "text/html", otaHtml);
  });

  server.on("/update", HTTP_POST, []() {
    if (!server.authenticate(otaUsername, otaPassword)) return server.requestAuthentication();
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "OTA UPDATE FAILED" : "OTA SUCCESS! REBOOTING...");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[OTA] Updating: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) Serial.printf("[OTA] Update Success: %u bytes\n", upload.totalSize);
      else Update.printError(Serial);
    }
  });
}

/* ====================================================================
 * 11. ARDUINO SETUP & MAIN LOOP
 * ==================================================================== */
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n==================================================");
  Serial.println("   HYDRA PEOC COMMAND HUB (ARDUINO NANO ESP32)   ");
  Serial.println("==================================================");

  // 1. Initialize ILI9488 3.5" Parallel Display
  tftInit();

  // 2. Render Boot Up Sequence Screen (Black BG, White & Green Text)
  currentScreen = SCREEN_BOOT;
  renderBootScreenHeader();

  bootLogMessage("> Initializing ESP32-S3 Core & Peripherals", "[ OK ]");
  bootLogMessage("> Configuring ILI9488 8-Bit Parallel Bus", "[ OK ]");
  bootLogMessage("> Calibrating Graphics Pipeline (480x320)", "[ OK ]");

  // 3. Connect to Wi-Fi
  bootLogMessage("> Connecting to Wi-Fi SSID: 'sakshyam'...", "[ .... ]");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int wifiRetries = 0;
  while (WiFi.status() != WL_CONNECTED && wifiRetries < 20) {
    delay(400);
    Serial.print(".");
    wifiRetries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    String connMsg = "> Wi-Fi Connected! IP: " + WiFi.localIP().toString();
    bootLogMessage(connMsg.c_str(), "[ OK ]");
  } else {
    bootLogMessage("> Router Unreachable. Activating SoftAP Mode...", "[ WARN ]", COLOR_AMBER);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    String apMsg = "> SoftAP SSID: " + String(AP_SSID) + " (192.168.4.1)";
    bootLogMessage(apMsg.c_str(), "[ ACTIVE ]");
  }

  // 4. Setup mDNS and Web Services
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    bootLogMessage("> mDNS Responder active at: http://hydra-display.local", "[ OK ]");
  }

  server.on("/", HTTP_GET, handleWebRoot);
  server.on("/api/status", HTTP_GET, handleWebStatus);
  setupWebOTA();
  server.begin();
  bootLogMessage("> Embedded PEOC Web & OTA Service (Port 80)", "[ ONLINE ]");

  // 5. Query Initial Telemetry Feed from Zenith Kandel API
  bootLogMessage("> Querying Zenith Kandel HYDRA Level 2 API...", "[ SYNC ]");
  fetchLevel2Telemetry();

  if (sysStatus.syncCount > 0) {
    bootLogMessage("> Initial Telemetry Feed Synchronized!", "[ OK ]");
  } else {
    bootLogMessage("> API Initial Sync: Waiting for live poll...", "[ WARN ]", COLOR_AMBER);
  }

  bootLogMessage("> Boot Sequence Complete. Starting PEOC Grid...", "[ READY ]");
  delay(1500); // Allow operator to view boot screen diagnostics

  // Transition to main dashboard
  currentScreen = SCREEN_DASHBOARD;
  renderDataScreen(true);
  lastScreenSwitchTime = millis();
}

void loop() {
  server.handleClient();

  // Periodic Telemetry Poll
  unsigned long now = millis();
  if (now - lastApiPollTime >= API_POLL_INTERVAL_MS) {
    lastApiPollTime = now;
    fetchLevel2Telemetry();

    // If an emergency is currently active, cycle between Alert Pop-up and Data Screen every 5 seconds
    if (sysStatus.isEmergency) {
      if (now - lastScreenSwitchTime >= 5000) {
        lastScreenSwitchTime = now;
        alertDisplayState = !alertDisplayState;
        if (alertDisplayState) {
          renderAlertScreen();
        } else {
          renderDataScreen(true);
        }
      } else {
        // Redraw current active view with fresh data
        if (alertDisplayState) {
          renderAlertScreen();
        } else {
          renderDataScreen(false);
        }
      }
    } else {
      // Normal nominal monitoring: keep rendering Data Screen with smooth differential update
      alertDisplayState = false;
      renderDataScreen(false);
    }
  }
}