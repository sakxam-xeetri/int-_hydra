/*
 * ═══════════════════════════════════════════════════════════════════════════
 *              LIFELINE EMERGENCY RECEIVER - ILI9488 PARALLEL
 *              Production v4.0 - 8-bit Parallel Display + LoRa
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * DISPLAY: ILI9488 3.5" TFT (8-bit Parallel Mode) - 320x480
 * RADIO: LoRa SX1278 @ 433 MHz
 *
 * ⚠️ PIN WARNINGS:
 *   - GPIO 2 (LoRa CS): Boot strapping pin - may need BOOT button during upload
 *   - GPIO 15 (LoRa RST): Strapping pin - usually OK
 *
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <Arduino.h>
#include <HTTPClient.h>
#include <LoRa.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>

// ═══════════════════════════════════════════════════════════════════════════
//                         ILI9488 DISPLAY PINS (8-bit Parallel)
// ═══════════════════════════════════════════════════════════════════════════

#define TFT_RST 18
#define TFT_CS 19
#define TFT_RS 21 // DC pin
#define TFT_WR 22
#define TFT_RD 23

#define TFT_D0 33
#define TFT_D1 32
#define TFT_D2 13
#define TFT_D3 12
#define TFT_D4 14
#define TFT_D5 27
#define TFT_D6 26
#define TFT_D7 25

const uint8_t dataPins[8] = {TFT_D0, TFT_D1, TFT_D2, TFT_D3,
                             TFT_D4, TFT_D5, TFT_D6, TFT_D7};

// ═══════════════════════════════════════════════════════════════════════════
//                         LORA SX1278 PINS
// ═══════════════════════════════════════════════════════════════════════════
// ⚠️ GPIO 2 = Boot pin (may need to hold BOOT during upload)
// ⚠️ GPIO 15 = Strapping pin (should be OK)

#define LORA_SCK 16
#define LORA_MISO 5
#define LORA_MOSI 17
#define LORA_CS 15 // Swapped - safer for boot
#define LORA_DIO0 4
#define LORA_RST 2 // Swapped - RST is less critical during boot

// ═══════════════════════════════════════════════════════════════════════════
//                         LORA CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define LORA_FREQUENCY 433E6
#define LORA_SF 12
#define LORA_BW 125E3
#define LORA_SYNC_WORD 0x12

// ═══════════════════════════════════════════════════════════════════════════
//                         OTHER PINS
// ═══════════════════════════════════════════════════════════════════════════

#define BUZZER_PIN 0 // BOOT button pin (shared)
#define WIFI_PORTAL_PIN 0

// ═══════════════════════════════════════════════════════════════════════════
//                         DISPLAY CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 480

// ═══════════════════════════════════════════════════════════════════════════
//                         COLORS (RGB565)
// ═══════════════════════════════════════════════════════════════════════════

#define RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_BG_PRIMARY RGB565(13, 13, 15)
#define COLOR_BG_HEADER RGB565(10, 25, 41)
#define COLOR_BG_CARD RGB565(26, 26, 46)
#define COLOR_RED RGB565(255, 59, 59)
#define COLOR_GREEN RGB565(0, 255, 135)
#define COLOR_AMBER RGB565(255, 184, 0)
#define COLOR_CYAN RGB565(0, 212, 255)
#define COLOR_ORANGE RGB565(255, 123, 0)
#define COLOR_TEXT_PRIMARY COLOR_WHITE
#define COLOR_TEXT_MUTED RGB565(100, 116, 139)
#define COLOR_BORDER RGB565(50, 60, 85)

// ═══════════════════════════════════════════════════════════════════════════
//                         ALERT DEFINITIONS
// ═══════════════════════════════════════════════════════════════════════════

#define ALERT_COUNT 15

const char *alertNames[ALERT_COUNT] = {
    "EMERGENCY",         "MEDICAL EMERGENCY", "MEDICINE SHORTAGE",
    "EVACUATION NEEDED", "STATUS OK",         "INJURY REPORTED",
    "FOOD SHORTAGE",     "WATER SHORTAGE",    "WEATHER ALERT",
    "LOST PERSON",       "ANIMAL ATTACK",     "LANDSLIDE",
    "SNOW STORM",        "EQUIPMENT FAILURE", "OTHER EMERGENCY"};

const char *alertShort[ALERT_COUNT] = {
    "EMERGENCY", "MEDICAL",   "MEDICINE", "EVACUATION", "STATUS OK",
    "INJURY",    "FOOD",      "WATER",    "WEATHER",    "LOST PERSON",
    "ANIMAL",    "LANDSLIDE", "SNOW",     "EQUIPMENT",  "OTHER"};

const uint8_t alertPriority[ALERT_COUNT] = {0, 0, 2, 0, 3, 1, 2, 2,
                                            2, 1, 1, 1, 1, 2, 4};
const char *priorityLabels[] = {"CRITICAL", "HIGH", "MEDIUM", "OK", "INFO"};

// ═══════════════════════════════════════════════════════════════════════════
//                         STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

enum ScreenState { SCREEN_BOOT, SCREEN_IDLE, SCREEN_ALERT };
ScreenState currentScreen = SCREEN_BOOT;

unsigned long bootStartTime = 0;
unsigned long alertReceivedTime = 0;
unsigned long lastPulseTime = 0;
unsigned long lastRadarTime = 0;
unsigned long lastBootAnimTime = 0;
uint8_t pulseState = 0;
uint8_t radarAngle = 0;
uint8_t bootProgress = 0;
uint8_t alertPulseState = 0;
unsigned long lastAlertPulse = 0;

int lastDeviceId = 0;
int lastAlertIndex = 0;
int lastRssi = 0;
int totalAlertsReceived = 0;
bool loraInitialized = false;

Preferences preferences;
String storedSSID = "";
String storedPassword = "";
bool wifiConnected = false;

// ═══════════════════════════════════════════════════════════════════════════
//                         ILI9488 LOW-LEVEL DRIVER
// ═══════════════════════════════════════════════════════════════════════════

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

void tftInit() {
  // Reset
  digitalWrite(TFT_RST, HIGH);
  delay(50);
  digitalWrite(TFT_RST, LOW);
  delay(150);
  digitalWrite(TFT_RST, HIGH);
  delay(150);

  writeCommand(0x01);
  delay(150); // Software reset
  writeCommand(0x11);
  delay(150); // Sleep out
  writeCommand(0x3A);
  writeDataByte(0x55); // 16-bit color
  writeCommand(0x36);
  writeDataByte(0x48); // Memory access
  writeCommand(0x29);
  delay(50); // Display ON

  Serial.println("[OK] ILI9488 initialized (8-bit parallel)");
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
  setAddressWindow(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
  digitalWrite(TFT_RS, HIGH);
  digitalWrite(TFT_CS, LOW);
  uint8_t hi = color >> 8, lo = color & 0xFF;
  for (uint32_t i = 0; i < (uint32_t)SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
    writeData8(hi);
    writeData8(lo);
  }
  digitalWrite(TFT_CS, HIGH);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT || w <= 0 || h <= 0)
    return;
  if (x + w > SCREEN_WIDTH)
    w = SCREEN_WIDTH - x;
  if (y + h > SCREEN_HEIGHT)
    h = SCREEN_HEIGHT - y;
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

void drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
    return;
  setAddressWindow(x, y, x, y);
  digitalWrite(TFT_RS, HIGH);
  digitalWrite(TFT_CS, LOW);
  writeData8(color >> 8);
  writeData8(color);
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

void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  for (int16_t y = -r; y <= r; y++) {
    int16_t wx = (int16_t)sqrt(r * r - y * y);
    fillRect(x0 - wx, y0 + y, wx * 2 + 1, 1, color);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//                         FONT (5x7)
// ═══════════════════════════════════════════════════════════════════════════

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

void drawChar(int16_t x, int16_t y, char c, uint16_t color, uint8_t size) {
  if (c < 32 || c > 127)
    c = ' ';
  uint8_t idx = c - 32;
  for (int col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(&font5x7[idx][col]);
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        if (size == 1)
          drawPixel(x + col, y + row, color);
        else
          fillRect(x + col * size, y + row * size, size, size, color);
      }
    }
  }
}

void drawText(int16_t x, int16_t y, const char *text, uint16_t color,
              uint8_t size) {
  while (*text) {
    drawChar(x, y, *text++, color, size);
    x += 6 * size;
  }
}

void drawTextCentered(int16_t y, const char *text, uint16_t color,
                      uint8_t size) {
  int16_t x = (SCREEN_WIDTH - strlen(text) * 6 * size) / 2;
  drawText(x, y, text, color, size);
}

// ═══════════════════════════════════════════════════════════════════════════
//                         UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

uint16_t getPriorityColor(uint8_t priority) {
  switch (priority) {
  case 0:
    return COLOR_RED;
  case 1:
    return COLOR_ORANGE;
  case 2:
    return COLOR_AMBER;
  case 3:
    return COLOR_GREEN;
  default:
    return COLOR_TEXT_MUTED;
  }
}

uint16_t getAlertColor(int index) {
  if (index < 0 || index >= ALERT_COUNT)
    return COLOR_TEXT_MUTED;
  return getPriorityColor(alertPriority[index]);
}

char getAlertCode(int index) {
  return (index >= 0 && index < ALERT_COUNT) ? 'A' + index : 'X';
}

void playAlertTone(int priority) {
  if (priority <= 1) {
    tone(BUZZER_PIN, 2500, 100);
    delay(120);
    tone(BUZZER_PIN, 2500, 100);
    delay(120);
    tone(BUZZER_PIN, 2500, 100);
  } else {
    tone(BUZZER_PIN, 2000, 200);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//                         SCREEN DRAWING
// ═══════════════════════════════════════════════════════════════════════════

void drawBootScreen() {
  fillScreen(COLOR_BG_PRIMARY);

  // Top accent line
  drawHLine(0, 0, SCREEN_WIDTH, COLOR_CYAN);
  drawHLine(0, 1, SCREEN_WIDTH, RGB565(0, 100, 120));

  // Medical cross
  int cx = SCREEN_WIDTH / 2, cy = 80;
  fillRect(cx - 6, cy - 22, 12, 44, COLOR_RED);
  fillRect(cx - 22, cy - 6, 44, 12, COLOR_RED);
  // Cross glow
  drawRect(cx - 7, cy - 23, 14, 46, RGB565(255, 100, 100));
  drawRect(cx - 23, cy - 7, 46, 14, RGB565(255, 100, 100));

  // Title
  drawTextCentered(130, "LifeLine", COLOR_WHITE, 4);
  drawTextCentered(175, "EMERGENCY RECEIVER", COLOR_CYAN, 1);

  // Separator line
  drawHLine(60, 195, SCREEN_WIDTH - 120, COLOR_BORDER);

  // Loading bar background
  fillRect(40, 280, SCREEN_WIDTH - 80, 12, COLOR_BG_CARD);
  drawRect(40, 280, SCREEN_WIDTH - 80, 12, COLOR_BORDER);

  drawTextCentered(300, "Initializing...", COLOR_TEXT_MUTED, 1);

  // Bottom accent
  drawHLine(0, SCREEN_HEIGHT - 3, SCREEN_WIDTH, RGB565(0, 100, 120));
  drawHLine(0, SCREEN_HEIGHT - 2, SCREEN_WIDTH, COLOR_CYAN);

  bootStartTime = millis();
  lastBootAnimTime = millis();
  bootProgress = 0;
  Serial.println("[SCREEN] Boot screen");
}

// Boot animation update
bool updateBootAnimation() {
  unsigned long now = millis();

  // Update progress bar every 40ms
  if (now - lastBootAnimTime >= 40) {
    lastBootAnimTime = now;

    if (bootProgress < 100) {
      bootProgress += 2;

      // Draw progress bar fill
      int barWidth = (SCREEN_WIDTH - 84) * bootProgress / 100;
      fillRect(42, 282, barWidth, 8, COLOR_CYAN);

      // Update percentage text
      fillRect(135, 300, 50, 10, COLOR_BG_PRIMARY);
      char pct[10];
      sprintf(pct, "%d%%", bootProgress);
      drawTextCentered(300, pct, COLOR_GREEN, 1);

      // Pulse the cross
      if (bootProgress % 20 == 0) {
        int cx = SCREEN_WIDTH / 2, cy = 80;
        uint16_t glowColor =
            (bootProgress % 40 == 0) ? RGB565(255, 120, 120) : COLOR_RED;
        fillRect(cx - 6, cy - 22, 12, 44, glowColor);
        fillRect(cx - 22, cy - 6, 44, 12, glowColor);
      }
    }
  }

  return bootProgress >= 100;
}

void drawIdleScreen() {
  fillScreen(COLOR_BG_PRIMARY);

  // Header
  fillRect(0, 0, SCREEN_WIDTH, 45, COLOR_BG_HEADER);
  drawHLine(0, 44, SCREEN_WIDTH, COLOR_CYAN);
  drawText(15, 15, "LIFELINE RX", COLOR_WHITE, 2);

  // Live badge
  fillRect(SCREEN_WIDTH - 55, 12, 45, 18, RGB565(10, 35, 20));
  fillCircle(SCREEN_WIDTH - 45, 21, 4, COLOR_GREEN);
  drawText(SCREEN_WIDTH - 37, 14, "LIVE", COLOR_GREEN, 1);

  // Main status card
  fillRect(15, 60, SCREEN_WIDTH - 30, 100, COLOR_BG_CARD);
  drawRect(15, 60, SCREEN_WIDTH - 30, 100, COLOR_BORDER);

  // Radar icon
  int rcx = SCREEN_WIDTH / 2, rcy = 100;
  drawCircle(rcx, rcy, 25, COLOR_CYAN); // Using simple lines instead
  fillCircle(rcx, rcy, 8, COLOR_CYAN);

  drawTextCentered(140, "LISTENING", COLOR_WHITE, 2);
  drawTextCentered(180, "Waiting for signals...", COLOR_TEXT_MUTED, 1);

  // Stats
  int sy = 220;
  fillRect(15, sy, 90, 40, COLOR_BG_CARD);
  drawText(20, sy + 5, "FREQ", COLOR_TEXT_MUTED, 1);
  drawText(20, sy + 20, "433MHz", COLOR_CYAN, 1);

  fillRect(115, sy, 90, 40, COLOR_BG_CARD);
  drawText(120, sy + 5, "ALERTS", COLOR_TEXT_MUTED, 1);
  char buf[10];
  sprintf(buf, "%d", totalAlertsReceived);
  drawText(120, sy + 20, buf, COLOR_AMBER, 1);

  fillRect(215, sy, 90, 40, COLOR_BG_CARD);
  drawText(220, sy + 5, "STATUS", COLOR_TEXT_MUTED, 1);
  drawText(220, sy + 20, loraInitialized ? "READY" : "ERROR",
           loraInitialized ? COLOR_GREEN : COLOR_RED, 1);

  lastPulseTime = millis();
  Serial.println("[SCREEN] Idle screen");
}

void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  int16_t x = r, y = 0, err = 0;
  while (x >= y) {
    drawPixel(x0 + x, y0 + y, color);
    drawPixel(x0 + y, y0 + x, color);
    drawPixel(x0 - y, y0 + x, color);
    drawPixel(x0 - x, y0 + y, color);
    drawPixel(x0 - x, y0 - y, color);
    drawPixel(x0 - y, y0 - x, color);
    drawPixel(x0 + y, y0 - x, color);
    drawPixel(x0 + x, y0 - y, color);
    y++;
    err += 1 + 2 * y;
    if (2 * (err - x) + 1 > 0) {
      x--;
      err += 1 - 2 * x;
    }
  }
}

void drawAlertScreen(int deviceId, int alertIndex, int rssi) {
  uint16_t alertColor = getAlertColor(alertIndex);
  uint8_t priority = alertPriority[alertIndex];

  fillScreen(COLOR_BG_PRIMARY);

  // Alert header
  fillRect(0, 0, SCREEN_WIDTH, 50, alertColor);
  drawText(15, 15, "INCOMING ALERT", RGB565(10, 10, 10), 2);

  // Priority badge
  fillRect(15, 60, 80, 25, alertColor);
  drawText(20, 67, priorityLabels[min((int)priority, 4)], RGB565(10, 10, 10),
           1);

  // Main alert card
  fillRect(15, 95, SCREEN_WIDTH - 30, 80, COLOR_BG_CARD);
  drawRect(15, 95, SCREEN_WIDTH - 30, 80, alertColor);
  fillRect(15, 95, 5, 80, alertColor); // Left accent

  drawText(30, 110, alertShort[alertIndex], alertColor, 2);

  // Alert code
  fillRect(SCREEN_WIDTH - 55, 105, 35, 25, alertColor);
  char code[2] = {getAlertCode(alertIndex), 0};
  drawText(SCREEN_WIDTH - 48, 110, code, RGB565(10, 10, 10), 2);

  // Device info
  fillRect(15, 190, 140, 50, COLOR_BG_CARD);
  drawText(20, 200, "FROM DEVICE", COLOR_TEXT_MUTED, 1);
  char devBuf[15];
  sprintf(devBuf, "TX #%03d", deviceId);
  drawText(20, 220, devBuf, COLOR_CYAN, 2);

  // Signal info
  fillRect(165, 190, 140, 50, COLOR_BG_CARD);
  drawText(170, 200, "SIGNAL", COLOR_TEXT_MUTED, 1);
  char rssiBuf[15];
  sprintf(rssiBuf, "%d dBm", rssi);
  drawText(170, 220, rssiBuf, COLOR_GREEN, 2);

  alertReceivedTime = millis();
  lastDeviceId = deviceId;
  lastAlertIndex = alertIndex;
  lastRssi = rssi;
  totalAlertsReceived++;

  playAlertTone(priority);
  Serial.printf("[ALERT] Device=%d, Alert=%s, RSSI=%d\n", deviceId,
                alertNames[alertIndex], rssi);
}

void updateIdleAnimation() {
  if (millis() - lastPulseTime >= 600) {
    lastPulseTime = millis();
    pulseState = (pulseState + 1) % 3;

    // Update live indicator
    fillCircle(SCREEN_WIDTH - 45, 21, 4,
               (pulseState == 0) ? COLOR_WHITE : COLOR_GREEN);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//                         LORA FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

bool parseLoRaPacket(int &deviceId, int &alertIndex, int &rssi) {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0)
    return false;

  String packet = "";
  while (LoRa.available()) {
    packet += (char)LoRa.read();
  }
  rssi = LoRa.packetRssi();

  // Parse TX[ID],[CODE] format
  if (!packet.startsWith("TX"))
    return false;

  int comma = packet.indexOf(',');
  if (comma < 3)
    return false;

  deviceId = packet.substring(2, comma).toInt();
  String codePart = packet.substring(comma + 1);
  codePart.trim();

  if (codePart.length() == 1 && codePart[0] >= 'A' && codePart[0] <= 'O') {
    alertIndex = codePart[0] - 'A';
  } else {
    alertIndex = codePart.toInt();
  }

  return (alertIndex >= 0 && alertIndex < ALERT_COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════
//                         SETUP & LOOP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n");
  Serial.println(
      "╔═══════════════════════════════════════════════════════════╗");
  Serial.println(
      "║     LIFELINE RECEIVER v4.0 - ILI9488 PARALLEL EDITION     ║");
  Serial.println(
      "╚═══════════════════════════════════════════════════════════╝");

  // Initialize display pins
  pinMode(TFT_RST, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_RS, OUTPUT);
  pinMode(TFT_WR, OUTPUT);
  pinMode(TFT_RD, OUTPUT);
  for (int i = 0; i < 8; i++)
    pinMode(dataPins[i], OUTPUT);

  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_RD, HIGH);
  digitalWrite(TFT_WR, HIGH);

  // Initialize LoRa pins
  pinMode(LORA_CS, OUTPUT);
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_CS, HIGH);

  // Reset LoRa
  digitalWrite(LORA_RST, LOW);
  delay(10);
  digitalWrite(LORA_RST, HIGH);
  delay(10);

  // Initialize SPI for LoRa (custom pins)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);
  Serial.println("[OK] SPI initialized");

  // Initialize LoRa
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[ERROR] LoRa init failed!");
    loraInitialized = false;
  } else {
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW);
    LoRa.enableCrc();
    loraInitialized = true;
    Serial.println("[OK] LoRa @ 433MHz, SF12");
  }

  // Initialize display
  tftInit();
  fillScreen(COLOR_BLACK);

  // Show boot screen
  currentScreen = SCREEN_BOOT;
  drawBootScreen();

  Serial.println("[READY] Waiting for alerts...\n");
}

void loop() {
  switch (currentScreen) {
  case SCREEN_BOOT:
    if (millis() - bootStartTime >= 2000) {
      currentScreen = SCREEN_IDLE;
      drawIdleScreen();
    }
    break;

  case SCREEN_IDLE:
    updateIdleAnimation();
    {
      int deviceId, alertIndex, rssi;
      if (parseLoRaPacket(deviceId, alertIndex, rssi)) {
        currentScreen = SCREEN_ALERT;
        drawAlertScreen(deviceId, alertIndex, rssi);
      }
    }
    break;

  case SCREEN_ALERT: {
    int deviceId, alertIndex, rssi;
    if (parseLoRaPacket(deviceId, alertIndex, rssi)) {
      drawAlertScreen(deviceId, alertIndex, rssi);
    }
  }
    if (millis() - alertReceivedTime >= 30000) {
      currentScreen = SCREEN_IDLE;
      drawIdleScreen();
    }
    break;
  }

  delay(10);
}
