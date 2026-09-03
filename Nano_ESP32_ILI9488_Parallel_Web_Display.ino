/* ====================================================================
 * ARDUINO NANO ESP32 + 3.5" ILI9488 IPS TFT LCD (8-BIT PARALLEL)
 * Reference Hardware Driver : example.ino
 * Microcontroller           : Arduino Nano ESP32 (ESP32-S3)
 * Display Controller        : ILI9488 3.5" TFT LCD (8-bit Parallel Mode)
 * Low-Level Driver          : Direct Zero-Dependency High-Speed GPIO Bus
 * Wi-Fi Credentials         : SSID "sakshyam" | Password "sakshyam"
 * Features                  : Live Web-to-Display Streamer, 4 Layouts, 4 Themes
 * ==================================================================== */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>

/* ====================================================================
 * 1. PIN CONFIGURATION FOR ARDUINO NANO ESP32
 * ====================================================================
 * Connect the 3.5" ILI9488 8-Bit Parallel Shield to the Arduino Nano ESP32
 * silk-screen headers as follows:
 *
 * CONTROL PINS:
 *   - LCD_RD  -> Nano ESP32 Pin A0 (Native GPIO 1)
 *   - LCD_WR  -> Nano ESP32 Pin A1 (Native GPIO 2)
 *   - LCD_RS  -> Nano ESP32 Pin A2 (Native GPIO 3)  [Command / Data]
 *   - LCD_CS  -> Nano ESP32 Pin A3 (Native GPIO 4)  [Chip Select]
 *   - LCD_RST -> Nano ESP32 Pin A4 (Native GPIO 11) [Reset]
 *
 * 8-BIT DATA BUS (D0 to D7):
 *   - LCD_D0  -> Nano ESP32 Pin D2 (Native GPIO 5)
 *   - LCD_D1  -> Nano ESP32 Pin D3 (Native GPIO 6)
 *   - LCD_D2  -> Nano ESP32 Pin D4 (Native GPIO 7)
 *   - LCD_D3  -> Nano ESP32 Pin D5 (Native GPIO 8)
 *   - LCD_D4  -> Nano ESP32 Pin D6 (Native GPIO 9)
 *   - LCD_D5  -> Nano ESP32 Pin D7 (Native GPIO 10)
 *   - LCD_D6  -> Nano ESP32 Pin D8 (Native GPIO 17)
 *   - LCD_D7  -> Nano ESP32 Pin D9 (Native GPIO 18)
 *
 * POWER:
 *   - VCC     -> 5V or 3.3V
 *   - GND     -> GND
 * ==================================================================== */

// Control Pins (mapped to ESP32-S3 GPIOs for Nano ESP32)
#define TFT_RD   1   // Silk Screen: A0
#define TFT_WR   2   // Silk Screen: A1
#define TFT_RS   3   // Silk Screen: A2 (Data/Command)
#define TFT_CS   4   // Silk Screen: A3 (Chip Select)
#define TFT_RST  11  // Silk Screen: A4 (Reset)

// 8-Bit Data Bus Pins (D0 to D7)
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
 * 2. WI-FI & OTA CREDENTIALS
 * ==================================================================== */
const char* WIFI_SSID     = "sakshyam";
const char* WIFI_PASSWORD = "sakshyam";

const char* AP_SSID       = "NANO-ESP32-DISPLAY";
const char* AP_PASSWORD   = "12345678";

const char* MDNS_HOSTNAME = "nano-display";
const char* otaUsername   = "admin";
const char* otaPassword   = "admin";

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
String currentTitle      = "NANO ESP32 // ILI9488";
String currentMessage    = "Online and connected! Submit any message or alert from the web dashboard to render it live on this 3.5\" 8-bit parallel TFT display.";
String currentMode       = "CARD"; // CARD, HUD, ALERT, TEXT
String currentTheme      = "MONO"; // MONO, CYAN, EMERALD, AMBER
int    currentFontSize   = 2;      // 1 = Small, 2 = Medium, 3 = Large

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

  Serial.println("[OK] ILI9488 initialized in 8-bit Parallel Mode on Arduino Nano ESP32!");
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

void drawTextCentered(int16_t y, const char *text, uint16_t color, uint8_t size) {
  int16_t x = (screenWidth - strlen(text) * 6 * size) / 2;
  if (x < 0) x = 0;
  drawText(x, y, text, color, size);
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
 * 7. NATIVE ON-SCREEN DASHBOARD RENDER ROUTINES
 * ==================================================================== */

void renderDisplay() {
  fillScreen(COLOR_BG);

  // 1. TOP HEADER BAR
  fillRect(0, 0, screenWidth, 26, COLOR_PANEL);
  drawHLine(0, 26, screenWidth, COLOR_BORDER);
  drawText(10, 8, "NANO ESP32 // ILI9488 [8-BIT PARALLEL]", COLOR_ACCENT, 1);

  String ipInfo = "IP: " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
  drawText(screenWidth - (ipInfo.length() * 6) - 10, 8, ipInfo.c_str(), COLOR_WHITE, 1);

  // 2. BOTTOM FOOTER BAR
  fillRect(0, screenHeight - 22, screenWidth, 22, COLOR_PANEL);
  drawHLine(0, screenHeight - 23, screenWidth, COLOR_BORDER);

  String statusText = "LAYOUT: " + currentMode + " | THEME: " + currentTheme;
  drawText(10, screenHeight - 15, statusText.c_str(), COLOR_WHITE, 1);

  char uptimeBuf[32];
  unsigned long s = millis() / 1000;
  snprintf(uptimeBuf, sizeof(uptimeBuf), "UP: %02lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
  drawText(screenWidth - 95, screenHeight - 15, uptimeBuf, COLOR_ACCENT, 1);

  // 3. MAIN CONTENT BASED ON LAYOUT MODE
  if (currentMode == "ALERT") {
    drawRect(10, 36, screenWidth - 20, screenHeight - 68, COLOR_BORDER);
    drawRect(12, 38, screenWidth - 24, screenHeight - 72, COLOR_BORDER);
    fillRect(16, 42, screenWidth - 32, 36, COLOR_BORDER);
    drawTextCentered(52, "! EMERGENCY ALERT NOTICE !", COLOR_BG, 2);
    drawWrappedText(24, 96, screenWidth - 48, currentMessage, COLOR_FG, currentFontSize);

  } else if (currentMode == "HUD") {
    int cardW = (screenWidth - 30) / 2;

    drawRect(10, 36, cardW, 110, COLOR_BORDER);
    fillRect(12, 38, cardW - 4, 106, COLOR_PANEL);
    drawText(20, 46, "[ WI-FI NETWORK ]", COLOR_ACCENT, 1);
    drawText(20, 68, WiFi.status() == WL_CONNECTED ? WIFI_SSID : AP_SSID, COLOR_WHITE, 2);
    String rssiStr = "RSSI: " + String(WiFi.RSSI()) + " dBm";
    drawText(20, 102, rssiStr.c_str(), COLOR_WHITE, 1);
    String gwStr = "GATEWAY: " + WiFi.gatewayIP().toString();
    drawText(20, 120, gwStr.c_str(), COLOR_WHITE, 1);

    drawRect(screenWidth - 10 - cardW, 36, cardW, 110, COLOR_BORDER);
    fillRect(screenWidth - 8 - cardW, 38, cardW - 4, 106, COLOR_PANEL);
    drawText(screenWidth - cardW, 46, "[ NANO ESP32 HEALTH ]", COLOR_ACCENT, 1);
    String heapStr = String(ESP.getFreeHeap() / 1024) + " KB FREE";
    drawText(screenWidth - cardW, 68, heapStr.c_str(), COLOR_WHITE, 2);
    String cpuStr = "CPU FREQ: " + String(ESP.getCpuFreqMHz()) + " MHz";
    drawText(screenWidth - cardW, 102, cpuStr.c_str(), COLOR_WHITE, 1);
    drawText(screenWidth - cardW, 120, "BUS: 8-BIT PARALLEL", COLOR_WHITE, 1);

    drawRect(10, 156, screenWidth - 20, screenHeight - 188, COLOR_BORDER);
    fillRect(12, 158, screenWidth - 24, screenHeight - 192, COLOR_PANEL);
    drawText(22, 166, "[ ACTIVE BROADCAST ]", COLOR_ACCENT, 1);
    drawWrappedText(22, 186, screenWidth - 44, currentMessage, COLOR_WHITE, currentFontSize);

  } else if (currentMode == "TEXT") {
    drawText(14, 36, "> SYSTEM TERMINAL STREAM:", COLOR_ACCENT, 1);
    drawHLine(14, 48, screenWidth - 28, COLOR_BORDER);
    drawWrappedText(14, 58, screenWidth - 28, currentMessage, COLOR_FG, currentFontSize);

  } else {
    drawRect(10, 36, screenWidth - 20, screenHeight - 68, COLOR_BORDER);
    fillRect(12, 38, screenWidth - 24, screenHeight - 72, COLOR_PANEL);
    fillRect(12, 38, screenWidth - 24, 38, COLOR_BG);
    drawHLine(12, 76, screenWidth - 24, COLOR_BORDER);
    drawText(24, 48, currentTitle.c_str(), COLOR_ACCENT, 2);
    drawWrappedText(24, 96, screenWidth - 48, currentMessage, COLOR_WHITE, currentFontSize);
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
  <title>NANO ESP32 // 3.5" PARALLEL TFT DASHBOARD</title>
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
        <h1>NANO ESP32 // 8-BIT PARALLEL TFT</h1>
        <p>3.5" IPS PANEL &bull; ILI9488 8-BIT DIRECT BUS &bull; WEB BROADCASTER</p>
      </div>
      <div class="badges">
        <span class="badge solid"><span class="pulse"></span> NANO ESP32 OK</span>
        <span id="active-mode-badge" class="badge outline">LAYOUT: CARD</span>
        <a href="/update" class="badge link">&#9889; WEB OTA</a>
      </div>
    </header>

    <div class="preview-card">
      <div class="preview-label">
        <span>3.5" TFT SCREEN HARDWARE OUTPUT</span>
        <span id="render-status">STATUS: SYNCHRONIZED</span>
      </div>
      <div class="screen-mock">
        <h2 id="prev-title">TITLE PREVIEW</h2>
        <p id="prev-msg">Message preview...</p>
      </div>
    </div>

    <div class="section-title">
      <span>01 // BROADCAST MESSAGE TO DISPLAY</span>
      <span>WEB SENDER</span>
    </div>

    <div class="form-panel">
      <form id="tft-form" onsubmit="event.preventDefault(); submitContent();">
        <div class="form-group">
          <label for="input-title">HEADLINE / CARD TITLE:</label>
          <input type="text" id="input-title" value="NANO ESP32 DASHBOARD" required>
        </div>

        <div class="form-group">
          <label for="input-msg">MESSAGE TEXT / PARAGRAPH (RENDERED DIRECTLY TO TFT):</label>
          <textarea id="input-msg" placeholder="Type anything to render onto the ILI9488 display..." required>Arduino Nano ESP32 is controlling the 3.5 inch ILI9488 display in 8-bit parallel mode!</textarea>
        </div>

        <div class="ctrl-row">
          <div>
            <label for="select-mode">SCREEN LAYOUT MODE:</label>
            <select id="select-mode">
              <option value="CARD">CARD &amp; BANNER (DEFAULT)</option>
              <option value="HUD">SYSTEM TELEMETRY HUD</option>
              <option value="ALERT">INVERTED ALERT BOX</option>
              <option value="TEXT">TERMINAL TEXT STREAM</option>
            </select>
          </div>

          <div>
            <label for="select-theme">COLOR THEME:</label>
            <select id="select-theme">
              <option value="MONO">STARK MONOCHROME (PURE B&amp;W)</option>
              <option value="CYAN">ELECTRIC CYAN &amp; NAVY</option>
              <option value="EMERALD">CYBER EMERALD GREEN</option>
              <option value="AMBER">MATRIX AMBER GOLD</option>
            </select>
          </div>

          <div>
            <label for="select-font">FONT SIZE:</label>
            <select id="select-font">
              <option value="1">SMALL (COMPACT)</option>
              <option value="2" selected>MEDIUM (BALANCED)</option>
              <option value="3">LARGE (HEADLINE)</option>
            </select>
          </div>

          <div>
            <label for="select-rot">ROTATION:</label>
            <select id="select-rot">
              <option value="1" selected>LANDSCAPE (480x320)</option>
              <option value="0">PORTRAIT (320x480)</option>
            </select>
          </div>
        </div>

        <div style="font-size: 10px; color: #888; text-transform: uppercase; margin-bottom: 4px;">QUICK PRESETS:</div>
        <div class="preset-chips">
          <span class="chip" onclick="setPreset('WELCOME SAKSHYAM', 'Arduino Nano ESP32 initialized ILI9488 3.5 parallel screen.', 'CARD', 'CYAN')">WELCOME NOTICE</span>
          <span class="chip" onclick="setPreset('CRITICAL ALERT', 'Warning: High distance breach detected in sector A.', 'ALERT', 'AMBER')">SECURITY ALERT</span>
          <span class="chip" onclick="setPreset('SYSTEM STATUS', 'Free heap: 280KB. Wi-Fi connected to sakshyam. Zero errors.', 'HUD', 'EMERALD')">SYSTEM HEALTH</span>
          <span class="chip" onclick="setPreset('TERMINAL LOG', 'Boot sequence done. 8-Bit bus active on A0..A4 and D2..D9.', 'TEXT', 'MONO')">TERMINAL LOG</span>
        </div>

        <div class="btn-row">
          <button type="submit" class="btn">[ &#9654; RENDER ON TFT DISPLAY ]</button>
          <button type="button" class="btn outline" onclick="clearDisplay()">[ CLEAR DISPLAY ]</button>
        </div>
      </form>
    </div>

    <div class="section-title">
      <span>02 // HARDWARE PINOUT &amp; DIAGNOSTICS</span>
      <span>NANO ESP32 SILK SCREEN PINS</span>
    </div>

    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>IP ADDRESS</th>
            <th>WI-FI RSSI</th>
            <th>UPTIME</th>
            <th>CONTROL PINS (ANALOG)</th>
            <th>DATA BUS (DIGITAL)</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td id="sys-ip" class="num">---.---.---.---</td>
            <td id="sys-rssi" class="num">-- dBm</td>
            <td id="sys-uptime" class="num">00:00:00</td>
            <td class="num">RD: A0 &bull; WR: A1 &bull; RS: A2 &bull; CS: A3 &bull; RST: A4</td>
            <td class="num">D0:D2 &bull; D1:D3 &bull; D2:D4 &bull; D3:D5 &bull; D4:D6 &bull; D5:D7 &bull; D6:D8 &bull; D7:D9</td>
          </tr>
        </tbody>
      </table>
    </div>

    <footer>
      <span>ARDUINO NANO ESP32 // ILI9488 3.5" 8-BIT PARALLEL TFT</span>
      <span><a href="/update">[ OVER-THE-AIR FIRMWARE UPDATE ]</a></span>
    </footer>

  </div>

  <script>
    async function pollStatus() {
      try {
        const res = await fetch('/api/status');
        if (!res.ok) return;
        const data = await res.json();

        document.getElementById('prev-title').innerText = data.display.title;
        document.getElementById('prev-msg').innerText = data.display.message;
        document.getElementById('active-mode-badge').innerText = "LAYOUT: " + data.display.mode;

        document.getElementById('sys-ip').innerText = data.sys.ip;
        document.getElementById('sys-rssi').innerText = data.sys.rssi + " dBm";
        document.getElementById('sys-uptime').innerText = formatUptime(data.sys.uptime_sec);
      } catch (e) {}
    }

    async function submitContent() {
      const title = document.getElementById('input-title').value.trim();
      const msg   = document.getElementById('input-msg').value.trim();
      const mode  = document.getElementById('select-mode').value;
      const theme = document.getElementById('select-theme').value;
      const font  = document.getElementById('select-font').value;
      const rot   = document.getElementById('select-rot').value;

      document.getElementById('render-status').innerText = "TRANSMITTING TO TFT...";

      try {
        const url = '/api/display?title=' + encodeURIComponent(title) +
                    '&msg=' + encodeURIComponent(msg) +
                    '&mode=' + encodeURIComponent(mode) +
                    '&theme=' + encodeURIComponent(theme) +
                    '&font=' + encodeURIComponent(font) +
                    '&rot='  + encodeURIComponent(rot);
        const res = await fetch(url);
        if (res.ok) {
          document.getElementById('render-status').innerText = "STATUS: DISPLAY UPDATED OK";
          setTimeout(pollStatus, 200);
        }
      } catch (e) {
        alert("Render error: " + e.message);
      }
    }

    async function clearDisplay() {
      document.getElementById('input-title').value = "SCREEN CLEARED";
      document.getElementById('input-msg').value = "Waiting for incoming web messages...";
      submitContent();
    }

    function setPreset(title, msg, mode, theme) {
      document.getElementById('input-title').value = title;
      document.getElementById('input-msg').value = msg;
      document.getElementById('select-mode').value = mode;
      document.getElementById('select-theme').value = theme;
      submitContent();
    }

    function formatUptime(totalSecs) {
      const h = Math.floor(totalSecs / 3600).toString().padStart(2, '0');
      const m = Math.floor((totalSecs % 3600) / 60).toString().padStart(2, '0');
      const s = Math.floor(totalSecs % 60).toString().padStart(2, '0');
      return h + ":" + m + ":" + s;
    }

    pollStatus();
    setInterval(pollStatus, 3000);
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

  json += "\"display\":{";
  json += "\"title\":\"" + currentTitle + "\",";
  String escMsg = currentMessage;
  escMsg.replace("\"", "\\\"");
  escMsg.replace("\n", "\\n");
  escMsg.replace("\r", "");
  json += "\"message\":\"" + escMsg + "\",";
  json += "\"mode\":\"" + currentMode + "\",";
  json += "\"theme\":\"" + currentTheme + "\",";
  json += "\"font_size\":" + String(currentFontSize);
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
  <meta charset="UTF-8"><title>NANO ESP32 // OTA UPDATE</title>
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
    <h1>FIRMWARE FLASH PORTAL (NANO ESP32)</h1>
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
  delay(1000);

  Serial.println("\n==============================================");
  Serial.println("  ARDUINO NANO ESP32 // 3.5\" ILI9488 PARALLEL");
  Serial.println("==============================================");

  // Initialize ILI9488 using exact 8-bit parallel driver from example.ino
  tftInit();
  applyTheme("MONO");

  // Show splash screen on TFT
  fillScreen(COLOR_BLACK);
  drawTextCentered(100, "NANO ESP32 // ILI9488", COLOR_WHITE, 2);
  drawTextCentered(135, "3.5\" IPS 8-BIT PARALLEL DISPLAY", COLOR_WHITE, 1);
  drawTextCentered(165, "CONNECTING TO WI-FI: 'sakshyam'...", COLOR_WHITE, 1);

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Connecting to '%s'", WIFI_SSID);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WIFI] Connected! Display Web IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] Router timeout. Starting Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WIFI] SoftAP Active: '%s'\n", AP_SSID);
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
  setupWebOTA();
  server.begin();
  Serial.println("[HTTP] Web server listening on port 80.");

  // Render initial dashboard on TFT display
  renderDisplay();
}

void loop() {
  server.handleClient();
}
