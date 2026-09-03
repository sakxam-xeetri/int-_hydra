/* ====================================================================
 * ARDUINO NANO ESP32 + 3.5" ILI9488 IPS TFT LCD WEB DISPLAY
 * Microcontroller : Arduino Nano ESP32 (ESP32-S3)
 * Display         : 3.5" TFT LCD with ILI9488 IPS Driver (480x320)
 * Graphics Library: LovyanGFX (Ultra-Fast Native SPI / Parallel)
 * Wi-Fi           : SSID "sakshyam" | Password "sakshyam"
 * UI Theme        : Stark Monochrome High-Contrast (Black & White, 0px Radius)
 * Features        : Web-to-Display Text Streamer, Custom Layouts, HUD & Alerts
 * ==================================================================== */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

/* ====================================================================
 * 1. WI-FI & OTA CONFIGURATION
 * ==================================================================== */
const char* WIFI_SSID     = "sakshyam";
const char* WIFI_PASSWORD = "sakshyam";

// Fallback Access Point (AP) if router is out of range
const char* AP_SSID       = "NANO-ESP32-DISPLAY";
const char* AP_PASSWORD   = "12345678";

// mDNS Hostname (Access via http://nano-display.local)
const char* MDNS_HOSTNAME = "nano-display";

// OTA Credentials (used at /update)
const char* otaUsername   = "admin";
const char* otaPassword   = "admin";

/* ====================================================================
 * 2. LOVYANGFX ILI9488 DISPLAY HARDWARE CONFIGURATION
 * ==================================================================== */

// Arduino Nano ESP32 Silk Screen Pins mapped to native ESP32-S3 GPIOs
#ifndef TFT_SCK
  #define TFT_SCK  48 // Silk screen: D13
  #define TFT_MOSI 38 // Silk screen: D11
  #define TFT_MISO 47 // Silk screen: D12
  #define TFT_CS   21 // Silk screen: D10
  #define TFT_DC   18 // Silk screen: D9
  #define TFT_RST  17 // Silk screen: D8
  #define TFT_BL   10 // Silk screen: D7
#endif


class LGFX_NanoESP32 : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:
  LGFX_NanoESP32(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;     // FSPI on ESP32-S3
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;    // 40 MHz SPI write speed
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      // Nano ESP32 SPI Pins
      cfg.pin_sclk = TFT_SCK;  // SPI Clock (D13)
      cfg.pin_mosi = TFT_MOSI; // SPI MOSI / Data In (D11)
      cfg.pin_miso = TFT_MISO; // SPI MISO (D12)
      cfg.pin_dc   = TFT_DC;   // Data / Command (D9)
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = TFT_CS;  // Chip Select (D10)
      cfg.pin_rst          = TFT_RST; // Hardware Reset (D8)
      cfg.pin_busy         = -1;
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 1;     // 1 = Landscape Orientation (480 x 320)
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = false;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = TFT_BL;          // Backlight control pin (D7)
      cfg.invert = false;
      cfg.freq   = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};

LGFX_NanoESP32 tft;
WebServer server(80);

/* ====================================================================
 * 3. DISPLAY CONTENT STATE & THEMES
 * ==================================================================== */
String currentTitle       = "ARDUINO NANO ESP32";
String currentMessage     = "System is online. Submit any text or alert from the web dashboard to render it live on this 3.5\" IPS display!";
String currentMode        = "CARD";      // CARD, HUD, ALERT, TEXT
String currentTheme       = "MONO";      // MONO, CYAN, EMERALD, AMBER
int    currentBrightness  = 255;         // 0 - 255
int    currentFontSize    = 2;           // 1 = Small, 2 = Medium, 3 = Large
unsigned long lastMsgTime = 0;

// Color Theme definitions (16-bit 565)
uint16_t COLOR_BG      = TFT_BLACK;
uint16_t COLOR_FG      = TFT_WHITE;
uint16_t COLOR_ACCENT  = TFT_WHITE;
uint16_t COLOR_PANEL   = 0x0841; // Dark Gray (RGB 8,8,8)
uint16_t COLOR_BORDER  = TFT_WHITE;

void applyTheme(String themeName) {
  currentTheme = themeName;
  if (themeName == "CYAN") {
    COLOR_BG     = 0x0002; // Very dark navy
    COLOR_FG     = TFT_WHITE;
    COLOR_ACCENT = TFT_CYAN;
    COLOR_PANEL  = 0x0863;
    COLOR_BORDER = TFT_CYAN;
  } else if (themeName == "EMERALD") {
    COLOR_BG     = 0x0100; // Deep dark green
    COLOR_FG     = TFT_WHITE;
    COLOR_ACCENT = TFT_GREEN;
    COLOR_PANEL  = 0x0920;
    COLOR_BORDER = TFT_GREEN;
  } else if (themeName == "AMBER") {
    COLOR_BG     = TFT_BLACK;
    COLOR_FG     = TFT_WHITE;
    COLOR_ACCENT = TFT_ORANGE;
    COLOR_PANEL  = 0x18A0;
    COLOR_BORDER = TFT_ORANGE;
  } else { // Default MONO
    COLOR_BG     = TFT_BLACK;
    COLOR_FG     = TFT_WHITE;
    COLOR_ACCENT = TFT_WHITE;
    COLOR_PANEL  = 0x1082; // Charcoal
    COLOR_BORDER = TFT_WHITE;
  }
}

/* ====================================================================
 * 4. NATIVE 480x320 TFT DISPLAY RENDER ROUTINES
 * ==================================================================== */

void renderDisplay() {
  tft.startWrite();
  tft.fillScreen(COLOR_BG);

  // 1. TOP STATUS BAR (Height: 30px)
  tft.fillRect(0, 0, 480, 28, COLOR_PANEL);
  tft.drawFastHLine(0, 28, 480, COLOR_BORDER);

  tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  tft.setTextSize(1);
  tft.drawString("NANO ESP32 // ILI9488 3.5\" IPS", 10, 8);

  String ipInfo = "IP: " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
  tft.drawString(ipInfo.c_str(), 270, 8);

  // 2. BOTTOM STATUS BAR (Height: 24px)
  tft.fillRect(0, 298, 480, 22, COLOR_PANEL);
  tft.drawFastHLine(0, 297, 480, COLOR_BORDER);

  tft.setTextColor(COLOR_FG, COLOR_PANEL);
  String modeTag = "MODE: " + currentMode + "  |  THEME: " + currentTheme;
  tft.drawString(modeTag.c_str(), 10, 304);

  char uptimeBuf[32];
  unsigned long secs = millis() / 1000;
  snprintf(uptimeBuf, sizeof(uptimeBuf), "UPTIME: %02lu:%02lu:%02lu", secs / 3600, (secs % 3600) / 60, secs % 60);
  tft.drawString(uptimeBuf, 340, 304);

  // 3. MAIN CONTENT BODY BASED ON SELECTED MODE
  if (currentMode == "ALERT") {
    // ALERT MODE: Bold inverted flashing border box
    tft.drawRect(12, 40, 456, 246, COLOR_BORDER);
    tft.drawRect(14, 42, 452, 242, COLOR_BORDER);

    tft.fillRect(18, 46, 444, 40, COLOR_BORDER);
    tft.setTextColor(COLOR_BG, COLOR_BORDER);
    tft.setTextSize(2);
    tft.drawString("! ALERT NOTICE !", 130, 56);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(currentFontSize);
    tft.setCursor(26, 105);
    tft.setTextWrap(true, true);
    tft.print(currentMessage);

  } else if (currentMode == "HUD") {
    // SYSTEM HUD MODE: Real-time telemetry cards
    tft.drawRect(12, 38, 220, 118, COLOR_BORDER);
    tft.fillRect(14, 40, 216, 114, COLOR_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    tft.setTextSize(1);
    tft.drawString("[ WI-FI NETWORK ]", 24, 48);
    tft.setTextColor(COLOR_FG, COLOR_PANEL);
    tft.setTextSize(2);
    tft.drawString(WiFi.status() == WL_CONNECTED ? WIFI_SSID : AP_SSID, 24, 70);
    tft.setTextSize(1);
    tft.drawString("RSSI: " + String(WiFi.RSSI()) + " dBm", 24, 105);
    tft.drawString("GATEWAY: " + WiFi.gatewayIP().toString(), 24, 125);

    // Card 2: Memory & S3 Specs
    tft.drawRect(248, 38, 220, 118, COLOR_BORDER);
    tft.fillRect(250, 40, 216, 114, COLOR_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    tft.drawString("[ HARDWARE HEALTH ]", 260, 48);
    tft.setTextColor(COLOR_FG, COLOR_PANEL);
    tft.setTextSize(2);
    tft.drawString(String(ESP.getFreeHeap() / 1024) + " KB FREE", 260, 70);
    tft.setTextSize(1);
    tft.drawString("CPU FREQ: " + String(ESP.getCpuFreqMHz()) + " MHz", 260, 105);
    tft.drawString("FLASH SIZE: " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB", 260, 125);

    // Lower Banner: Custom Message
    tft.drawRect(12, 168, 456, 118, COLOR_BORDER);
    tft.fillRect(14, 170, 452, 114, COLOR_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    tft.drawString("[ ACTIVE MESSAGE ]", 24, 178);
    tft.setTextColor(COLOR_FG, COLOR_PANEL);
    tft.setTextSize(currentFontSize);
    tft.setCursor(24, 202);
    tft.setTextWrap(true, true);
    tft.print(currentMessage);

  } else if (currentMode == "TEXT") {
    // FULLSCREEN TERMINAL TEXT
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextSize(1);
    tft.drawString("> LIVE WEB STREAM / TERMINAL OUTPUT:", 16, 38);
    tft.drawFastHLine(16, 52, 448, COLOR_BORDER);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(currentFontSize);
    tft.setCursor(16, 62);
    tft.setTextWrap(true, true);
    tft.print(currentMessage);

  } else {
    // DEFAULT CARD / BANNER MODE
    tft.drawRect(14, 40, 452, 246, COLOR_BORDER);
    tft.fillRect(16, 42, 448, 242, COLOR_PANEL);

    // Title banner
    tft.fillRect(16, 42, 448, 38, COLOR_BG);
    tft.drawFastHLine(16, 80, 448, COLOR_BORDER);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextSize(2);
    tft.drawString(currentTitle.c_str(), 28, 52);

    // Message Body
    tft.setTextColor(COLOR_FG, COLOR_PANEL);
    tft.setTextSize(currentFontSize);
    tft.setCursor(28, 105);
    tft.setTextWrap(true, true);
    tft.print(currentMessage);
  }

  tft.endWrite();
}

/* ====================================================================
 * 5. EMBEDDED SHARP MONOCHROME WEB DASHBOARD HTML
 * ==================================================================== */
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>NANO ESP32 // 3.5" TFT DISPLAY CONTROLLER</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; border-radius: 0 !important; }
    body { background: #000000; color: #FFFFFF; font-family: ui-monospace, Menlo, Consolas, monospace; padding: 16px; line-height: 1.35; -webkit-font-smoothing: antialiased; }
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
    .section-title { font-size: 12px; letter-spacing: 2px; text-transform: uppercase; font-weight: 900; border-left: 4px solid #FFFFFF; padding-left: 8px; margin: 20px 0 10px 0; display: flex; justify-content: space-between; align-items: center; }
    
    /* PREVIEW CARD */
    .preview-card { border: 2px solid #FFFFFF; background: #080808; padding: 20px; margin-bottom: 16px; }
    .preview-label { font-size: 10px; color: #777; text-transform: uppercase; letter-spacing: 1.5px; font-weight: 700; margin-bottom: 6px; display: flex; justify-content: space-between; }
    .screen-mock { background: #000000; border: 1px solid #444444; padding: 18px; min-height: 120px; }
    .screen-mock h2 { font-size: 20px; font-weight: 900; letter-spacing: 1px; margin-bottom: 8px; color: #FFFFFF; }
    .screen-mock p { font-size: 14px; color: #CCCCCC; white-space: pre-wrap; word-break: break-word; }

    /* WORKSPACE FORM */
    .form-panel { border: 1px solid #FFFFFF; background: #050505; padding: 20px; margin-bottom: 16px; }
    .form-group { margin-bottom: 14px; }
    .form-group label { display: block; font-size: 10px; color: #888888; text-transform: uppercase; letter-spacing: 1.5px; font-weight: 700; margin-bottom: 6px; }
    input[type=text], textarea, select { width: 100%; background: #000000; color: #FFFFFF; border: 1px solid #444444; padding: 12px 14px; font-family: inherit; font-size: 14px; font-weight: 700; outline: none; }
    input[type=text]:focus, textarea:focus, select:focus { border-color: #FFFFFF; }
    textarea { resize: vertical; min-height: 100px; }
    .ctrl-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px; margin-bottom: 14px; }
    
    .btn-row { display: flex; gap: 10px; flex-wrap: wrap; margin-top: 10px; }
    .btn { flex: 1; min-width: 140px; padding: 12px 18px; font-size: 11px; font-weight: 800; letter-spacing: 1px; text-transform: uppercase; color: #000000; background: #FFFFFF; border: 1px solid #FFFFFF; cursor: pointer; text-align: center; }
    .btn:hover { background: #000000; color: #FFFFFF; }
    .btn.outline { background: #000000; color: #FFFFFF; border-color: #444444; }
    .btn.outline:hover { border-color: #FFFFFF; }

    .preset-chips { display: flex; gap: 6px; flex-wrap: wrap; margin-top: 8px; }
    .chip { font-size: 9px; background: #111111; border: 1px solid #333333; padding: 6px 10px; cursor: pointer; text-transform: uppercase; font-weight: 700; }
    .chip:hover { border-color: #FFFFFF; }

    /* TABLE */
    .table-container { border: 1px solid #333333; background: #080808; overflow-x: auto; margin-bottom: 16px; }
    table { width: 100%; border-collapse: collapse; font-size: 12px; text-align: left; }
    th, td { padding: 10px 14px; border-bottom: 1px solid #222222; border-right: 1px solid #222222; }
    th:last-child, td:last-child { border-right: none; }
    tr:last-child td { border-bottom: none; }
    th { background: #111111; color: #888888; font-size: 10px; text-transform: uppercase; letter-spacing: 1px; font-weight: 700; }
    td.num { font-weight: 700; color: #FFFFFF; font-size: 13px; }

    footer { border-top: 1px solid #333333; padding: 14px 0; font-size: 10px; color: #666666; display: flex; justify-content: space-between; letter-spacing: 1px; text-transform: uppercase; flex-wrap: wrap; gap: 8px; }
    footer a { color: #FFFFFF; text-decoration: none; border-bottom: 1px solid #FFFFFF; }
    footer a:hover { background: #FFFFFF; color: #000000; }
  </style>
</head>
<body>
  <div class="container">
    
    <header>
      <div class="title-group">
        <h1>NANO ESP32 // 3.5" TFT DISPLAY</h1>
        <p>ILI9488 IPS SCREEN &bull; 480x320 NATIVE RESOLUTION &bull; LIVE WEB DISPATCHER</p>
      </div>
      <div class="badges">
        <span class="badge solid"><span class="pulse"></span> TFT CONNECTED</span>
        <span id="active-mode-badge" class="badge outline">LAYOUT: CARD</span>
        <a href="/update" class="badge link">&#9889; WEB OTA</a>
      </div>
    </header>

    <!-- LIVE HARDWARE SCREEN PREVIEW -->
    <div class="preview-card">
      <div class="preview-label">
        <span>CURRENT 3.5" TFT SCREEN OUTPUT</span>
        <span id="render-status">STATUS: SYNCHRONIZED</span>
      </div>
      <div class="screen-mock">
        <h2 id="prev-title">TITLE PREVIEW</h2>
        <p id="prev-msg">Message preview will appear here...</p>
      </div>
    </div>

    <!-- SECTION 1: CONTENT DISPATCH WORKSPACE -->
    <div class="section-title">
      <span>01 // SEND CONTENT TO DISPLAY</span>
      <span>WEB BROADCASTER</span>
    </div>

    <div class="form-panel">
      <form id="tft-form" onsubmit="event.preventDefault(); submitContent();">
        <div class="form-group">
          <label for="input-title">CARD TITLE / HEADLINE:</label>
          <input type="text" id="input-title" value="WEATHER & SYSTEM ALERT" required>
        </div>

        <div class="form-group">
          <label for="input-msg">MESSAGE TEXT / PARAGRAPH (SHOWN ON 3.5" IPS PANEL):</label>
          <textarea id="input-msg" placeholder="Type anything to render onto the TFT LCD screen..." required>Room temperature is 22.4°C. Air quality index is optimal. All sensors online.</textarea>
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
              <option value="1">SMALL (COMPACT MULTILINE)</option>
              <option value="2" selected>MEDIUM (BALANCED)</option>
              <option value="3">LARGE (HERO HEADLINE)</option>
            </select>
          </div>
        </div>

        <!-- QUICK PRESETS -->
        <div style="font-size: 10px; color: #888; text-transform: uppercase; margin-bottom: 4px;">QUICK MESSAGE PRESETS:</div>
        <div class="preset-chips">
          <span class="chip" onclick="setPreset('WELCOME SAKSHYAM', 'Nano ESP32 + 3.5 inch ILI9488 IPS screen initialized successfully.', 'CARD', 'CYAN')">WELCOME NOTICE</span>
          <span class="chip" onclick="setPreset('EMERGENCY ALERT', 'Warning: High distance breach detected on security sector A.', 'ALERT', 'AMBER')">SECURITY ALERT</span>
          <span class="chip" onclick="setPreset('STATUS REPORT', 'ESP32 system health 100%. Free heap memory: 280KB. Zero errors.', 'HUD', 'EMERALD')">SYSTEM HEALTH</span>
          <span class="chip" onclick="setPreset('TERMINAL LOG', 'Boot sequence completed at 240MHz. Wi-Fi connected to sakshyam.', 'TEXT', 'MONO')">TERMINAL LOG</span>
        </div>

        <div class="btn-row">
          <button type="submit" class="btn">[ &#9654; RENDER ON TFT DISPLAY ]</button>
          <button type="button" class="btn outline" onclick="clearDisplay()">[ CLEAR DISPLAY ]</button>
        </div>
      </form>
    </div>

    <!-- SECTION 2: SYSTEM DIAGNOSTICS -->
    <div class="section-title">
      <span>02 // HARDWARE &amp; SYSTEM DIAGNOSTICS</span>
      <span>ARDUINO NANO ESP32</span>
    </div>

    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>IP ADDRESS</th>
            <th>WI-FI RSSI</th>
            <th>SYSTEM UPTIME</th>
            <th>FREE HEAP</th>
            <th>SPI WIRING PINOUT</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td id="sys-ip" class="num">---.---.---.---</td>
            <td id="sys-rssi" class="num">-- dBm</td>
            <td id="sys-uptime" class="num">00:00:00</td>
            <td id="sys-heap" class="num">-- KB</td>
            <td class="num">SCK: D13 &bull; MOSI: D11 &bull; CS: D10 &bull; DC: D9 &bull; RST: D8</td>
          </tr>
        </tbody>
      </table>
    </div>

    <footer>
      <span>ARDUINO NANO ESP32 &bull; 3.5" ILI9488 IPS TFT GATEWAY</span>
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
        document.getElementById('sys-heap').innerText = Math.round(data.sys.free_heap / 1024) + " KB";
      } catch (e) {}
    }

    async function submitContent() {
      const title = document.getElementById('input-title').value.trim();
      const msg   = document.getElementById('input-msg').value.trim();
      const mode  = document.getElementById('select-mode').value;
      const theme = document.getElementById('select-theme').value;
      const font  = document.getElementById('select-font').value;

      document.getElementById('render-status').innerText = "TRANSMITTING TO TFT...";

      try {
        const url = '/api/display?title=' + encodeURIComponent(title) +
                    '&msg=' + encodeURIComponent(msg) +
                    '&mode=' + encodeURIComponent(mode) +
                    '&theme=' + encodeURIComponent(theme) +
                    '&font=' + encodeURIComponent(font);
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
 * 6. WEB SERVER HANDLERS & OTA
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

  Serial.println("[TFT] Updating display from Web request...");
  renderDisplay();
  server.send(200, "application/json", "{\"status\":\"OK\"}");
}

// Embedded OTA Web HTML
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
 * 7. SETUP & MAIN LOOP
 * ==================================================================== */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==============================================");
  Serial.println("  ARDUINO NANO ESP32 // 3.5\" ILI9488 TFT");
  Serial.println("==============================================");

  // Initialize ILI9488 Display with LovyanGFX
  tft.init();
  tft.setRotation(1); // Landscape (480 x 320)
  tft.setBrightness(currentBrightness);
  applyTheme("MONO");

  // Show splash screen on TFT
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("ARDUINO NANO ESP32", 110, 100);
  tft.setTextSize(1);
  tft.drawString("3.5\" ILI9488 IPS (480x320) INITIALIZING...", 110, 140);
  tft.drawString("CONNECTING TO WI-FI: 'sakshyam'...", 110, 170);

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
