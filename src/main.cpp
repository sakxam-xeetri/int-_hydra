/* ====================================================================
 * ESP32 + SIM900A / SIM900 GSM CELLULAR CALL & SMS GATEWAY
 * Microcontroller : ESP32 (Dev Module / WROOM-32 / ESP32-S3)
 * GSM Module      : SIM900A / SIM900 Mini GSM GPRS Module
 * Communication   : Hardware Serial 2 (GPIO 16 RX2, GPIO 17 TX2 @ 9600 Baud)
 * Wi-Fi           : SSID "sakshyam" | Password "sakshyam"
 * UI Theme        : Stark Monochrome High-Contrast (Black & White, 0px Radius)
 * Features        : Auto-Baud Lock, Web Phone Calls, SMS Sender, Signal Meter, Live AT
 * ==================================================================== */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>

/* ====================================================================
 * 1. WI-FI & OTA CONFIGURATION
 * ==================================================================== */
const char* WIFI_SSID     = "sakshyam";
const char* WIFI_PASSWORD = "sakshyam";

// Fallback Access Point (AP) if router is out of range
const char* AP_SSID       = "ESP32-GSM-GATEWAY";
const char* AP_PASSWORD   = "12345678";

// mDNS Hostname (Access via http://esp32-gsm.local)
const char* MDNS_HOSTNAME = "esp32-gsm";

// OTA Credentials (used at /update)
const char* otaUsername   = "admin";
const char* otaPassword   = "admin";

/* ====================================================================
 * 2. HARDWARE PIN DEFINITIONS
 * ==================================================================== */
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define GSM_RX_PIN 4   // ESP32-S3 GPIO 4 -> Connect to GSM Module TX
  #define GSM_TX_PIN 5   // ESP32-S3 GPIO 5 -> Connect to GSM Module RX
  #define GSM_UART_NUM 1
#else
  #define GSM_RX_PIN 16  // Standard ESP32 GPIO 16 (RX2) -> Connect to GSM Module TX
  #define GSM_TX_PIN 17  // Standard ESP32 GPIO 17 (TX2) -> Connect to GSM Module RX
  #define GSM_UART_NUM 2
#endif

#define GSM_BAUD_RATE 9600

/* ====================================================================
 * 3. GLOBAL OBJECTS & STATE
 * ==================================================================== */
WebServer server(80);
HardwareSerial gsmSerial(GSM_UART_NUM);

// GSM Status State
bool isGsmAlive = false;
int  gsmRssi = 99;           // 0 - 31 (99 = unknown / not detectable)
int  gsmSignalPercent = 0;   // 0 - 100%
int  gsmSignalDbm = -115;    // dBm estimate
int  gsmNetworkReg = 0;      // 0 = Not registered, 1 = Home, 2 = Searching, 5 = Roaming
String gsmOperator = "UNKNOWN";
String gsmStatusText = "INITIALIZING MODEM...";

// Call State
String activeCallState = "IDLE";
String lastCalledNumber = "";

// Recent Action Feedback
String lastActionResult = "GATEWAY READY";
bool lastActionSuccess = true;
unsigned long lastActionTime = 0;

// Rolling AT Console Log (circular buffer)
const int MAX_LOG_LINES = 15;
String logLines[MAX_LOG_LINES];
int logIndex = 0;

// Periodic status check
unsigned long lastStatusCheck = 0;
const unsigned long STATUS_CHECK_INTERVAL_MS = 6000;
bool isOperationInProgress = false;

/* ====================================================================
 * 4. AT COMMAND HELPER & CONSOLE LOGGING
 * ==================================================================== */

void addLog(String text) {
  text.trim();
  if (text.length() == 0) return;
  
  // Replace newlines with spaces for clean single-line logs
  text.replace("\r", " ");
  text.replace("\n", " ");
  while (text.indexOf("  ") >= 0) text.replace("  ", " ");

  logLines[logIndex] = text;
  logIndex = (logIndex + 1) % MAX_LOG_LINES;
  Serial.println("[GSM LOG] " + text);
}

String sendATCommand(String cmd, unsigned long timeoutMs = 2000, String expectedReply = "OK") {
  // Clear RX buffer
  while (gsmSerial.available()) gsmSerial.read();

  addLog("> " + cmd);
  gsmSerial.print(cmd + "\r\n");

  String response = "";
  unsigned long start = millis();
  
  while (millis() - start < timeoutMs) {
    while (gsmSerial.available()) {
      char c = gsmSerial.read();
      response += c;
    }
    if (expectedReply.length() > 0 && response.indexOf(expectedReply) >= 0) {
      break;
    }
  }

  response.trim();
  if (response.length() > 0) {
    addLog("< " + response);
  }
  return response;
}

/* ====================================================================
 * 5. GSM HARDWARE ROUTINES (CALL, SMS, NETWORK STATUS)
 * ==================================================================== */

void queryGsmStatus() {
  if (isOperationInProgress) return;

  // 1. Ping Modem
  String atResp = sendATCommand("AT", 1000);
  if (atResp.indexOf("OK") >= 0) {
    isGsmAlive = true;
  } else {
    isGsmAlive = false;
    gsmStatusText = "NO RESPONSE FROM GSM MODULE (CHECK POWER & WIRING)";
    return;
  }

  // 2. Query Signal Quality: AT+CSQ
  String csqResp = sendATCommand("AT+CSQ", 1500);
  int csqIdx = csqResp.indexOf("+CSQ:");
  if (csqIdx >= 0) {
    int commaIdx = csqResp.indexOf(",", csqIdx);
    if (commaIdx > csqIdx) {
      String rssiStr = csqResp.substring(csqIdx + 5, commaIdx);
      rssiStr.trim();
      gsmRssi = rssiStr.toInt();
      if (gsmRssi >= 0 && gsmRssi <= 31) {
        gsmSignalDbm = -113 + (gsmRssi * 2);
        gsmSignalPercent = map(gsmRssi, 0, 31, 0, 100);
      } else {
        gsmSignalDbm = -115;
        gsmSignalPercent = 0;
      }
    }
  }

  // 3. Query Network Registration: AT+CREG?
  String cregResp = sendATCommand("AT+CREG?", 1500);
  int cregIdx = cregResp.indexOf("+CREG:");
  if (cregIdx >= 0) {
    int commaIdx = cregResp.indexOf(",", cregIdx);
    if (commaIdx > cregIdx && commaIdx + 1 < cregResp.length()) {
      gsmNetworkReg = cregResp.substring(commaIdx + 1, commaIdx + 2).toInt();
    }
  }

  // 4. Query Carrier Operator: AT+COPS?
  String copsResp = sendATCommand("AT+COPS?", 2000);
  int quote1 = copsResp.indexOf("\"");
  if (quote1 >= 0) {
    int quote2 = copsResp.indexOf("\"", quote1 + 1);
    if (quote2 > quote1) {
      gsmOperator = copsResp.substring(quote1 + 1, quote2);
    }
  }

  // Update Status Text
  if (gsmNetworkReg == 1) {
    gsmStatusText = "CONNECTED (HOME NETWORK)";
  } else if (gsmNetworkReg == 5) {
    gsmStatusText = "CONNECTED (ROAMING)";
  } else if (gsmNetworkReg == 2) {
    gsmStatusText = "SEARCHING FOR CELLULAR TOWER...";
  } else if (gsmNetworkReg == 3) {
    gsmStatusText = "REGISTRATION DENIED BY CARRIER";
  } else {
    gsmStatusText = "NOT REGISTERED / NO SIM CARD";
  }
}

bool gsmMakeCall(String phoneNumber) {
  isOperationInProgress = true;
  phoneNumber.trim();
  phoneNumber.replace(" ", "");
  phoneNumber.replace("-", "");

  if (phoneNumber.length() < 3) {
    lastActionResult = "ERROR: INVALID PHONE NUMBER";
    lastActionSuccess = false;
    isOperationInProgress = false;
    return false;
  }

  addLog("[CALL] Dialing: " + phoneNumber);
  activeCallState = "DIALING " + phoneNumber + "...";

  // ATD<number>; - Semicolon is mandatory for voice calls!
  String resp = sendATCommand("ATD" + phoneNumber + ";", 5000);

  if (resp.indexOf("OK") >= 0 || resp.indexOf("VOICE") >= 0) {
    activeCallState = "CALL IN PROGRESS &bull; " + phoneNumber;
    lastActionResult = "CALL INITIATED TO " + phoneNumber;
    lastCalledNumber = phoneNumber;
    lastActionSuccess = true;
  } else if (resp.indexOf("NO CARRIER") >= 0) {
    activeCallState = "IDLE (CALL REJECTED / NO CARRIER)";
    lastActionResult = "CALL FAILED: NO CARRIER";
    lastActionSuccess = false;
  } else if (resp.indexOf("BUSY") >= 0) {
    activeCallState = "IDLE (LINE BUSY)";
    lastActionResult = "CALL FAILED: LINE BUSY";
    lastActionSuccess = false;
  } else {
    activeCallState = "DIALED (CHECK HANDSET)";
    lastActionResult = "COMMAND SENT: " + resp;
    lastActionSuccess = true;
  }

  lastActionTime = millis();
  isOperationInProgress = false;
  return lastActionSuccess;
}

bool gsmHangupCall() {
  isOperationInProgress = true;
  addLog("[CALL] Hanging up call...");
  String resp = sendATCommand("ATH", 3000);

  activeCallState = "IDLE (CALL ENDED)";
  lastActionResult = "CALL TERMINATED (ATH OK)";
  lastActionSuccess = true;
  lastActionTime = millis();
  isOperationInProgress = false;
  return true;
}

bool gsmSendSMS(String phoneNumber, String message) {
  isOperationInProgress = true;
  phoneNumber.trim();
  phoneNumber.replace(" ", "");
  phoneNumber.replace("-", "");
  message.trim();

  if (phoneNumber.length() < 3 || message.length() == 0) {
    lastActionResult = "ERROR: PHONE NUMBER OR MESSAGE CANNOT BE EMPTY";
    lastActionSuccess = false;
    isOperationInProgress = false;
    return false;
  }

  addLog("[SMS] Setting Text Mode (AT+CMGF=1)...");
  sendATCommand("AT+CMGF=1", 1500, "OK");
  sendATCommand("AT+CSCS=\"GSM\"", 1000, "OK");

  // Clear incoming buffer
  while (gsmSerial.available()) gsmSerial.read();

  // Send AT+CMGS="<number>"
  String cmgsCmd = "AT+CMGS=\"" + phoneNumber + "\"";
  addLog("> " + cmgsCmd);
  gsmSerial.print(cmgsCmd + "\r\n");

  // Wait up to 5000ms for prompt character '>'
  unsigned long startPrompt = millis();
  bool gotPrompt = false;
  while (millis() - startPrompt < 5000) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
  }

  if (!gotPrompt) {
    addLog("< ERROR: NO PROMPT '>' FROM MODEM");
    lastActionResult = "SMS FAILED: MODEM DID NOT RETURN PROMPT '>'";
    lastActionSuccess = false;
    // Send ESC (0x1B) to cancel
    gsmSerial.write(0x1B);
    isOperationInProgress = false;
    return false;
  }

  addLog("> [SENDING MESSAGE BODY + CTRL+Z]...");
  // Send message body followed by Ctrl+Z (ASCII 26 / 0x1A)
  gsmSerial.print(message);
  gsmSerial.write(0x1A);

  // Wait up to 15 seconds for network delivery confirmation
  String smsResp = "";
  unsigned long startDelivery = millis();
  while (millis() - startDelivery < 15000) {
    while (gsmSerial.available()) {
      char c = gsmSerial.read();
      smsResp += c;
    }
    if (smsResp.indexOf("OK") >= 0 || smsResp.indexOf("ERROR") >= 0) {
      break;
    }
  }

  smsResp.trim();
  addLog("< " + smsResp);

  if (smsResp.indexOf("+CMGS:") >= 0 || smsResp.indexOf("OK") >= 0) {
    lastActionResult = "SMS SENT SUCCESSFULLY TO " + phoneNumber;
    lastActionSuccess = true;
  } else {
    lastActionResult = "SMS FAILED: " + smsResp;
    lastActionSuccess = false;
  }

  lastActionTime = millis();
  isOperationInProgress = false;
  return lastActionSuccess;
}

/* ====================================================================
 * 6. EMBEDDED SHARP MONOCHROME WEB DASHBOARD HTML
 * ==================================================================== */
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 // CELLULAR GSM GATEWAY</title>
  <style>
    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      border-radius: 0 !important; /* STRICT SHARP EDGES */
    }

    body {
      background-color: #000000;
      color: #FFFFFF;
      font-family: ui-monospace, "Cascadia Code", "SF Mono", Menlo, Consolas, "Courier New", monospace;
      padding: 16px;
      line-height: 1.35;
      -webkit-font-smoothing: antialiased;
    }

    .container {
      max-width: 960px;
      margin: 0 auto;
    }

    /* HEADER */
    header {
      border: 1px solid #FFFFFF;
      padding: 16px;
      margin-bottom: 16px;
      background: #050505;
      display: flex;
      flex-wrap: wrap;
      justify-content: space-between;
      align-items: center;
      gap: 12px;
    }

    .title-group h1 {
      font-size: 17px;
      letter-spacing: 2px;
      font-weight: 900;
      text-transform: uppercase;
    }

    .title-group p {
      font-size: 11px;
      color: #888888;
      letter-spacing: 1px;
      margin-top: 3px;
    }

    .badges {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      align-items: center;
    }

    .badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 4px 8px;
      font-size: 11px;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 1px;
      border: 1px solid #FFFFFF;
      text-decoration: none;
    }

    .badge.solid { background: #FFFFFF; color: #000000; }
    .badge.outline { background: #000000; color: #FFFFFF; }
    .badge.warn { border-style: dashed; background: #1A1A1A; color: #FFFFFF; }
    .badge.link { background: #000000; color: #FFFFFF; cursor: pointer; }
    .badge.link:hover { background: #FFFFFF; color: #000000; }

    .pulse {
      display: inline-block;
      width: 8px;
      height: 8px;
      background: #000000;
      animation: blink 1s steps(1) infinite;
    }

    @keyframes blink { 50% { opacity: 0; } }

    /* SECTION TITLES */
    .section-title {
      font-size: 12px;
      letter-spacing: 2px;
      text-transform: uppercase;
      font-weight: 900;
      border-left: 4px solid #FFFFFF;
      padding-left: 8px;
      margin: 20px 0 10px 0;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    /* GSM MODEM STATUS HERO BANNER */
    .modem-hero {
      border: 2px solid #FFFFFF;
      background: #080808;
      padding: 18px;
      margin-bottom: 16px;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 16px;
    }

    .stat-box-label {
      font-size: 10px;
      color: #777777;
      text-transform: uppercase;
      letter-spacing: 1.5px;
      font-weight: 700;
      margin-bottom: 4px;
    }

    .stat-box-value {
      font-size: 18px;
      font-weight: 900;
      letter-spacing: 0.5px;
      color: #FFFFFF;
    }

    .stat-box-sub {
      font-size: 11px;
      color: #AAAAAA;
      margin-top: 3px;
    }

    /* SIGNAL BAR */
    .signal-track {
      height: 8px;
      background: #222;
      border: 1px solid #444;
      margin-top: 6px;
    }
    .signal-fill {
      height: 100%;
      background: #FFF;
      width: 0%;
      transition: width 0.2s ease;
    }

    /* FEEDBACK ACTION BANNER */
    .feedback-banner {
      border: 1px solid #333333;
      padding: 12px 16px;
      margin-bottom: 16px;
      background: #0A0A0A;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 8px;
      font-size: 12px;
      letter-spacing: 1px;
    }

    .feedback-banner.success { border-color: #FFFFFF; background: #121212; }
    .feedback-banner.error { border-color: #888888; border-style: dashed; background: #1A1A1A; }

    /* TWO-COLUMN WORKSPACE: CALL PANEL + SMS PANEL */
    .workspace-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
      gap: 16px;
      margin-bottom: 16px;
    }

    .panel {
      border: 1px solid #FFFFFF;
      background: #050505;
      padding: 20px;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
    }

    .panel-header {
      font-size: 13px;
      font-weight: 900;
      letter-spacing: 1.5px;
      text-transform: uppercase;
      border-bottom: 1px solid #222222;
      padding-bottom: 10px;
      margin-bottom: 16px;
      display: flex;
      justify-content: space-between;
    }

    .form-group {
      margin-bottom: 14px;
    }

    .form-group label {
      display: block;
      font-size: 10px;
      color: #888888;
      text-transform: uppercase;
      letter-spacing: 1.5px;
      font-weight: 700;
      margin-bottom: 6px;
    }

    input[type=text], input[type=tel], textarea {
      width: 100%;
      background: #000000;
      color: #FFFFFF;
      border: 1px solid #444444;
      padding: 12px 14px;
      font-family: inherit;
      font-size: 14px;
      font-weight: 700;
      outline: none;
      transition: border-color 0.1s ease;
    }

    input[type=text]:focus, input[type=tel]:focus, textarea:focus {
      border-color: #FFFFFF;
    }

    textarea {
      resize: vertical;
      min-height: 90px;
    }

    .char-count {
      text-align: right;
      font-size: 10px;
      color: #666666;
      margin-top: 4px;
      letter-spacing: 1px;
    }

    .btn-row {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      margin-top: 6px;
    }

    .btn {
      flex: 1;
      min-width: 120px;
      padding: 12px 16px;
      font-size: 11px;
      font-weight: 800;
      letter-spacing: 1px;
      text-transform: uppercase;
      color: #000000;
      background: #FFFFFF;
      border: 1px solid #FFFFFF;
      cursor: pointer;
      text-align: center;
    }

    .btn:hover {
      background: #000000;
      color: #FFFFFF;
    }

    .btn.danger {
      background: #000000;
      color: #FFFFFF;
      border-color: #FFFFFF;
    }

    .btn.danger:hover {
      background: #FFFFFF;
      color: #000000;
    }

    /* QUICK CHIP PRESETS */
    .preset-chips {
      display: flex;
      gap: 6px;
      flex-wrap: wrap;
      margin-top: 8px;
    }

    .chip {
      font-size: 9px;
      background: #111111;
      border: 1px solid #333333;
      padding: 4px 8px;
      cursor: pointer;
      text-transform: uppercase;
      font-weight: 700;
    }

    .chip:hover {
      border-color: #FFFFFF;
    }

    /* AT COMMAND CONSOLE */
    .console-box {
      border: 1px solid #333333;
      background: #050505;
      padding: 16px;
      margin-bottom: 16px;
    }

    .console-screen {
      background: #000000;
      border: 1px solid #222222;
      padding: 12px;
      height: 160px;
      overflow-y: auto;
      font-size: 11px;
      color: #AAAAAA;
      font-family: inherit;
      white-space: pre-wrap;
      word-break: break-all;
    }

    .console-input-row {
      display: flex;
      gap: 8px;
      margin-top: 10px;
    }

    .console-input-row input {
      flex: 1;
      padding: 8px 12px;
      font-size: 12px;
    }

    .console-input-row button {
      padding: 8px 14px;
      font-size: 10px;
    }

    /* TABLE */
    .table-container {
      border: 1px solid #333333;
      background: #080808;
      overflow-x: auto;
      margin-bottom: 16px;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 12px;
      text-align: left;
    }

    th, td {
      padding: 10px 14px;
      border-bottom: 1px solid #222222;
      border-right: 1px solid #222222;
    }

    th:last-child, td:last-child { border-right: none; }
    tr:last-child td { border-bottom: none; }

    th {
      background: #111111;
      color: #888888;
      font-size: 10px;
      text-transform: uppercase;
      letter-spacing: 1px;
      font-weight: 700;
    }

    td.num {
      font-weight: 700;
      color: #FFFFFF;
      font-size: 13px;
    }

    footer {
      border-top: 1px solid #333333;
      padding: 14px 0;
      font-size: 10px;
      color: #666666;
      display: flex;
      justify-content: space-between;
      letter-spacing: 1px;
      text-transform: uppercase;
      flex-wrap: wrap;
      gap: 8px;
    }

    footer a { color: #FFFFFF; text-decoration: none; border-bottom: 1px solid #FFFFFF; }
    footer a:hover { background: #FFFFFF; color: #000000; }
  </style>
</head>
<body>
  <div class="container">
    
    <!-- HEADER -->
    <header>
      <div class="title-group">
        <h1>ESP32 // CELLULAR GSM GATEWAY</h1>
        <p>WEB-TRIGGERED VOICE CALLS &bull; SMS SENDER &bull; HARDWARE SERIAL 2</p>
      </div>
      <div class="badges">
        <span id="conn-badge" class="badge solid"><span class="pulse"></span> ONLINE</span>
        <span id="call-badge" class="badge outline">LINE: IDLE</span>
        <a href="/update" class="badge link">&#9889; WEB OTA</a>
      </div>
    </header>

    <!-- ACTION FEEDBACK BANNER -->
    <div id="feedback-box" class="feedback-banner">
      <span id="feedback-text">SYSTEM STATUS: GATEWAY STANDBY &bull; READY FOR COMMANDS</span>
      <span id="feedback-time" style="color: #666; font-size: 10px;"></span>
    </div>

    <!-- SECTION 1: MODEM & NETWORK STATUS -->
    <div class="section-title">
      <span>01 // CELLULAR NETWORK TELEMETRY</span>
      <span id="carrier-tag" style="color: #888; font-size: 10px;">CARRIER: QUERYING...</span>
    </div>

    <div class="modem-hero">
      <div>
        <div class="stat-box-label">MODEM HARDWARE</div>
        <div id="modem-state" class="stat-box-value">CHECKING...</div>
        <div id="modem-sub" class="stat-box-sub">UART2 @ 9600 BAUD</div>
      </div>

      <div>
        <div class="stat-box-label">CELLULAR OPERATOR</div>
        <div id="modem-operator" class="stat-box-value">--</div>
        <div id="modem-reg-status" class="stat-box-sub">REGISTRATION: PENDING</div>
      </div>

      <div>
        <div class="stat-box-label">SIGNAL STRENGTH (CSQ)</div>
        <div id="modem-signal" class="stat-box-value">-- / 31</div>
        <div class="signal-track"><div id="signal-fill" class="signal-fill"></div></div>
        <div id="modem-dbm" class="stat-box-sub">-- dBm &bull; --%</div>
      </div>

      <div>
        <div class="stat-box-label">CALL STATUS</div>
        <div id="active-call-disp" class="stat-box-value">IDLE</div>
        <div id="last-number-disp" class="stat-box-sub">NO ACTIVE DIAL</div>
      </div>
    </div>

    <!-- SECTION 2: CALL & SMS WORKSPACE -->
    <div class="section-title">
      <span>02 // DISPATCH WORKSPACE</span>
      <span>CALL &amp; SMS CONTROLLER</span>
    </div>

    <div class="workspace-grid">
      
      <!-- CALL PANEL -->
      <div class="panel">
        <div>
          <div class="panel-header">
            <span>[ VOICE CALL CONTROLLER ]</span>
            <span>ATD &bull; ATH</span>
          </div>

          <div class="form-group">
            <label for="call-phone">RECIPIENT PHONE NUMBER (WITH COUNTRY CODE):</label>
            <input type="tel" id="call-phone" placeholder="e.g. +9779812345678 or 9812345678" required>
          </div>

          <p style="font-size: 11px; color: #888; margin-bottom: 16px;">
            ESP32 issues standard <code>ATD&lt;number&gt;;</code> voice command to initiate the call through the GSM module.
          </p>
        </div>

        <div class="btn-row">
          <button class="btn" onclick="makeCall()">[ &#128222; MAKE CALL ]</button>
          <button class="btn danger" onclick="hangupCall()">[ &#9746; HANG UP ]</button>
        </div>
      </div>

      <!-- SMS PANEL -->
      <div class="panel">
        <div>
          <div class="panel-header">
            <span>[ SMS DISPATCH CONTROLLER ]</span>
            <span>AT+CMGS</span>
          </div>

          <div class="form-group">
            <label for="sms-phone">RECIPIENT PHONE NUMBER:</label>
            <input type="tel" id="sms-phone" placeholder="e.g. +9779812345678 or 9812345678" required>
          </div>

          <div class="form-group">
            <label for="sms-msg">SMS MESSAGE BODY:</label>
            <textarea id="sms-msg" placeholder="Type your SMS message here..." maxlength="160" oninput="updateCharCount()"></textarea>
            <div class="char-count"><span id="char-count">0</span> / 160 CHARS</div>
          </div>

          <!-- QUICK TEST PRESETS -->
          <div style="font-size: 10px; color: #777; text-transform: uppercase; margin-top: 4px;">QUICK MESSAGE PRESETS:</div>
          <div class="preset-chips">
            <span class="chip" onclick="setPreset('ALERT: System triggered an alarm event at node!')">ALARM EVENT</span>
            <span class="chip" onclick="setPreset('STATUS: ESP32 GSM Gateway is active and healthy.')">PING STATUS</span>
            <span class="chip" onclick="setPreset('SOS: Urgent assistance requested!')">SOS ALERT</span>
          </div>
        </div>

        <div class="btn-row" style="margin-top: 14px;">
          <button class="btn" onclick="sendSms()">[ &#9993; DISPATCH SMS ]</button>
        </div>
      </div>

    </div>

    <!-- SECTION 3: LIVE AT COMMAND TERMINAL -->
    <div class="section-title">
      <span>03 // LIVE AT COMMAND CONSOLE</span>
      <span>RAW MODEM LOGS</span>
    </div>

    <div class="console-box">
      <div id="console-screen" class="console-screen">WAITING FOR MODEM COMMS...</div>
      <div class="console-input-row">
        <input type="text" id="custom-at" placeholder="Send custom AT command (e.g. AT+CCLK?, AT+CBC, AT+CSQ)">
        <button class="btn" onclick="sendCustomAt()">[ EXECUTE AT ]</button>
      </div>
    </div>

    <!-- SECTION 4: SYSTEM DIAGNOSTICS -->
    <div class="section-title">
      <span>04 // SYSTEM DIAGNOSTICS</span>
      <span>ESP32 HARDWARE</span>
    </div>

    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>IP ADDRESS</th>
            <th>WI-FI RSSI</th>
            <th>SYSTEM UPTIME</th>
            <th>FREE MEMORY</th>
            <th>UART PINOUT</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td id="sys-ip" class="num">---.---.---.---</td>
            <td id="sys-rssi" class="num">-- dBm</td>
            <td id="sys-uptime" class="num">00:00:00</td>
            <td id="sys-heap" class="num">-- KB</td>
            <td class="num">RX: GPIO 16 &bull; TX: GPIO 17 &bull; 9600 BAUD</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- FOOTER -->
    <footer>
      <span>ESP32 CELLULAR GSM GATEWAY &bull; WEB DISPATCHER</span>
      <span><a href="/update">[ OVER-THE-AIR FIRMWARE UPDATE ]</a></span>
    </footer>

  </div>

  <script>
    let failedFetches = 0;

    async function pollStatus() {
      try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        failedFetches = 0;

        document.getElementById('conn-badge').className = "badge solid";
        document.getElementById('conn-badge').innerHTML = '<span class="pulse"></span> ONLINE';

        // 1. Modem Status
        if (data.gsm.alive) {
          document.getElementById('modem-state').innerText = "ONLINE (ALIVE)";
          document.getElementById('modem-sub').innerText = "UART RESPONDING // " + data.gsm.operator;
        } else {
          document.getElementById('modem-state').innerText = "OFFLINE";
          document.getElementById('modem-sub').innerText = "NO AT RESPONSE FROM MODEM";
        }

        // Operator & Network
        document.getElementById('modem-operator').innerText = data.gsm.operator;
        document.getElementById('carrier-tag').innerText = "CARRIER: " + data.gsm.operator;
        document.getElementById('modem-reg-status').innerText = data.gsm.status_text;

        // Signal Quality
        if (data.gsm.rssi <= 31) {
          document.getElementById('modem-signal').innerText = data.gsm.rssi + " / 31";
          document.getElementById('signal-fill').style.width = data.gsm.signal_percent + "%";
          document.getElementById('modem-dbm').innerText = data.gsm.signal_dbm + " dBm • " + data.gsm.signal_percent + "%";
        } else {
          document.getElementById('modem-signal').innerText = "NO SIGNAL";
          document.getElementById('signal-fill').style.width = "0%";
          document.getElementById('modem-dbm').innerText = "UNKNOWN / NO TOWER";
        }

        // Active Call Status
        document.getElementById('active-call-disp').innerHTML = data.call.state;
        document.getElementById('call-badge').innerHTML = "LINE: " + (data.call.state.includes("CALL") ? "BUSY" : "IDLE");
        if (data.call.last_number) {
          document.getElementById('last-number-disp').innerText = "LAST: " + data.call.last_number;
        }

        // Action Feedback Banner
        const fBox = document.getElementById('feedback-box');
        fBox.className = data.action.success ? "feedback-banner success" : "feedback-banner error";
        document.getElementById('feedback-text').innerText = data.action.text;

        // Console Logs
        if (data.logs && data.logs.length > 0) {
          document.getElementById('console-screen').innerText = data.logs.join("\n");
          // Auto scroll to bottom
          const cBox = document.getElementById('console-screen');
          cBox.scrollTop = cBox.scrollHeight;
        }

        // System Telemetry
        document.getElementById('sys-ip').innerText = data.sys.ip;
        document.getElementById('sys-rssi').innerText = data.sys.rssi + " dBm";
        document.getElementById('sys-uptime').innerText = formatUptime(data.sys.uptime_sec);
        document.getElementById('sys-heap').innerText = Math.round(data.sys.free_heap / 1024) + " KB";

      } catch (e) {
        failedFetches++;
        if (failedFetches > 3) {
          document.getElementById('conn-badge').className = "badge warn";
          document.getElementById('conn-badge').innerText = "DISCONNECTED";
        }
      }
    }

    async function makeCall() {
      const num = document.getElementById('call-phone').value.trim();
      if (!num) {
        alert("Please enter a destination phone number.");
        return;
      }
      document.getElementById('feedback-text').innerText = "DIALING " + num + "... PLEASE WAIT";
      try {
        const res = await fetch('/api/call?num=' + encodeURIComponent(num));
        const data = await res.json();
        pollStatus();
      } catch (e) {
        alert("Error calling: " + e.message);
      }
    }

    async function hangupCall() {
      document.getElementById('feedback-text').innerText = "HANGING UP CALL (ATH)...";
      try {
        const res = await fetch('/api/hangup');
        const data = await res.json();
        pollStatus();
      } catch (e) {
        alert("Error hanging up: " + e.message);
      }
    }

    async function sendSms() {
      const num = document.getElementById('sms-phone').value.trim();
      const msg = document.getElementById('sms-msg').value.trim();
      if (!num) {
        alert("Please enter a recipient phone number.");
        return;
      }
      if (!msg) {
        alert("Please enter a message to send.");
        return;
      }
      document.getElementById('feedback-text').innerText = "DISPATCHING SMS TO " + num + "... AWAITING NETWORK CONFIRMATION (UP TO 15s)";
      try {
        const res = await fetch('/api/send-sms?num=' + encodeURIComponent(num) + '&msg=' + encodeURIComponent(msg));
        const data = await res.json();
        pollStatus();
      } catch (e) {
        alert("Error sending SMS: " + e.message);
      }
    }

    async function sendCustomAt() {
      const cmd = document.getElementById('custom-at').value.trim();
      if (!cmd) return;
      try {
        await fetch('/api/at?cmd=' + encodeURIComponent(cmd));
        document.getElementById('custom-at').value = "";
        pollStatus();
      } catch (e) {
        alert("Error executing AT: " + e.message);
      }
    }

    function setPreset(text) {
      document.getElementById('sms-msg').value = text;
      updateCharCount();
    }

    function updateCharCount() {
      const len = document.getElementById('sms-msg').value.length;
      document.getElementById('char-count').innerText = len;
    }

    function formatUptime(totalSecs) {
      const h = Math.floor(totalSecs / 3600).toString().padStart(2, '0');
      const m = Math.floor((totalSecs % 3600) / 60).toString().padStart(2, '0');
      const s = Math.floor(totalSecs % 60).toString().padStart(2, '0');
      return h + ":" + m + ":" + s;
    }

    pollStatus();
    setInterval(pollStatus, 1500); // Poll status every 1.5 seconds
  </script>
</body>
</html>
)rawliteral";

/* ====================================================================
 * 7. WEB SERVER HANDLERS
 * ==================================================================== */

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String json = "{";

  // System
  json += "\"sys\":{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptime_sec\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap());
  json += "},";

  // GSM
  json += "\"gsm\":{";
  json += "\"alive\":" + String(isGsmAlive ? "true" : "false") + ",";
  json += "\"rssi\":" + String(gsmRssi) + ",";
  json += "\"signal_percent\":" + String(gsmSignalPercent) + ",";
  json += "\"signal_dbm\":" + String(gsmSignalDbm) + ",";
  json += "\"network_reg\":" + String(gsmNetworkReg) + ",";
  json += "\"operator\":\"" + gsmOperator + "\",";
  json += "\"status_text\":\"" + gsmStatusText + "\"";
  json += "},";

  // Call state
  json += "\"call\":{";
  json += "\"state\":\"" + activeCallState + "\",";
  json += "\"last_number\":\"" + lastCalledNumber + "\"";
  json += "},";

  // Action status
  json += "\"action\":{";
  json += "\"text\":\"" + lastActionResult + "\",";
  json += "\"success\":" + String(lastActionSuccess ? "true" : "false");
  json += "},";

  // Console Logs
  json += "\"logs\":[";
  for (int i = 0; i < MAX_LOG_LINES; i++) {
    int idx = (logIndex + i) % MAX_LOG_LINES;
    if (logLines[idx].length() > 0) {
      String escaped = logLines[idx];
      escaped.replace("\"", "\\\"");
      json += "\"" + escaped + "\"";
      if (i < MAX_LOG_LINES - 1) json += ",";
    }
  }
  // Trim trailing comma if any
  if (json.endsWith(",")) json.remove(json.length() - 1);
  json += "]";

  json += "}";

  server.send(200, "application/json", json);
}

void handleCall() {
  if (server.hasArg("num")) {
    String num = server.arg("num");
    bool ok = gsmMakeCall(num);
    server.send(200, "application/json", "{\"success\":" + String(ok ? "true" : "false") + "}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing number\"}");
  }
}

void handleHangup() {
  bool ok = gsmHangupCall();
  server.send(200, "application/json", "{\"success\":" + String(ok ? "true" : "false") + "}");
}

void handleSendSms() {
  if (server.hasArg("num") && server.hasArg("msg")) {
    String num = server.arg("num");
    String msg = server.arg("msg");
    bool ok = gsmSendSMS(num, msg);
    server.send(200, "application/json", "{\"success\":" + String(ok ? "true" : "false") + "}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing number or message\"}");
  }
}

void handleCustomAt() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    String resp = sendATCommand(cmd, 2500);
    server.send(200, "text/plain", resp);
  } else {
    server.send(400, "text/plain", "Missing cmd parameter");
  }
}

/* ====================================================================
 * 8. EMBEDDED SHARP MONOCHROME WEB OTA HTML
 * ==================================================================== */
const char OTA_INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 // FIRMWARE OTA</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; border-radius: 0 !important; }
    body { background: #000000; color: #FFFFFF; font-family: ui-monospace, Menlo, Consolas, monospace; padding: 20px; line-height: 1.4; -webkit-font-smoothing: antialiased; }
    .box { max-width: 520px; margin: 40px auto; border: 1px solid #FFFFFF; padding: 24px; background: #080808; }
    h1 { font-size: 16px; letter-spacing: 2px; text-transform: uppercase; margin-bottom: 8px; font-weight: 900; }
    p { font-size: 11px; color: #888888; margin-bottom: 20px; letter-spacing: 1px; }
    input[type=file] { display: block; width: 100%; border: 1px solid #333333; padding: 12px; background: #000000; color: #FFFFFF; font-size: 11px; margin-bottom: 16px; cursor: pointer; }
    input[type=file]:hover { border-color: #FFFFFF; }
    .btn { display: inline-block; width: 100%; padding: 12px; font-size: 12px; font-weight: 800; letter-spacing: 1px; text-transform: uppercase; color: #000000; background: #FFFFFF; border: 1px solid #FFFFFF; cursor: pointer; text-align: center; text-decoration: none; margin-bottom: 10px; }
    .btn:hover { background: #000000; color: #FFFFFF; }
    .btn.outline { background: #000000; color: #888888; border-color: #333333; }
    .btn.outline:hover { color: #FFFFFF; border-color: #FFFFFF; }
    .bar-wrap { border: 1px solid #333333; height: 16px; margin: 16px 0; display: none; background: #111111; }
    .bar-fill { height: 100%; width: 0%; background: #FFFFFF; transition: width 0.1s linear; }
    #status { font-size: 11px; letter-spacing: 1px; margin-top: 10px; text-transform: uppercase; font-weight: 700; color: #AAAAAA; }
    .badge { display: inline-block; padding: 2px 6px; border: 1px solid #FFF; font-size: 9px; margin-bottom: 12px; }
  </style>
</head>
<body>
  <div class="box">
    <span class="badge">[ ESP32 FIRMWARE RECOVERY ]</span>
    <h1>FIRMWARE FLASH PORTAL</h1>
    <p>SELECT COMPILED .BIN FIRMWARE BINARY TO REFLASH OVER WI-FI</p>
    <form id="upload_form" enctype="multipart/form-data" method="POST" action="/update">
      <input type="file" name="update" id="file" accept=".bin" required onchange="fileSelected()">
      <div class="bar-wrap" id="bar_wrap"><div class="bar-fill" id="bar_fill"></div></div>
      <button type="submit" id="btn_submit" class="btn">[ FLASH .BIN FIRMWARE ]</button>
      <a href="/" class="btn outline">[ CANCEL &amp; RETURN TO DASHBOARD ]</a>
      <div id="status">STATUS: STANDBY // WAITING FOR BINARY FILE</div>
    </form>
  </div>
  <script>
    function fileSelected() {
      const f = document.getElementById('file').files[0];
      if (f) document.getElementById('status').innerText = 'SELECTED: ' + f.name + ' (' + Math.round(f.size / 1024) + ' KB)';
    }
    document.getElementById('upload_form').onsubmit = function(e) {
      e.preventDefault();
      const f = document.getElementById('file').files[0];
      if (!f) return;
      const data = new FormData();
      data.append('update', f);
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update', true);
      document.getElementById('bar_wrap').style.display = 'block';
      document.getElementById('btn_submit').style.display = 'none';
      document.getElementById('status').innerText = 'FLASHING FIRMWARE TO SPI FLASH... DO NOT POWER OFF';
      xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
          const pct = Math.round((e.loaded / e.total) * 100);
          document.getElementById('bar_fill').style.width = pct + '%';
          document.getElementById('status').innerText = 'UPLOADING: ' + pct + '%';
        }
      };
      xhr.onload = function() {
        if (xhr.status === 200) {
          document.getElementById('bar_fill').style.width = '100%';
          document.getElementById('status').innerText = 'FLASH COMPLETE! REBOOTING ESP32 NODE...';
          setTimeout(() => { window.location.href = '/'; }, 6000);
        } else {
          document.getElementById('status').innerText = 'FLASH ERROR: ' + xhr.responseText;
          document.getElementById('btn_submit').style.display = 'block';
        }
      };
      xhr.onerror = function() {
        document.getElementById('status').innerText = 'NETWORK / COMMS ERROR DURING FLASH';
        document.getElementById('btn_submit').style.display = 'block';
      };
      xhr.send(data);
    };
  </script>
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
      Serial.printf("[WebOTA] Update file received: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("[WebOTA] Success! %u bytes written. Rebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
}

void setupArduinoOTA() {
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  if (strlen(otaPassword) > 0) {
    ArduinoOTA.setPassword(otaPassword);
  }
  ArduinoOTA.begin();
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found on ESP32 GSM Gateway");
}

/* ====================================================================
 * 9. SETUP & MAIN LOOP
 * ==================================================================== */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==============================================");
  Serial.println("  ESP32 GSM CELLULAR CALL & SMS GATEWAY");
  Serial.println("==============================================");

  // Initialize Hardware Serial for SIM900A GSM Module
  gsmSerial.begin(GSM_BAUD_RATE, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  Serial.printf("[SIM900A] Hardware Serial initialized on RX: %d, TX: %d @ %d baud\n", GSM_RX_PIN, GSM_TX_PIN, GSM_BAUD_RATE);

  addLog("ESP32 SIM900A Gateway Initialized.");

  // SIM900A Auto-Baud Synchronization (Sends AT bursts to lock baud rate)
  Serial.println("[SIM900A] Synchronizing baud rate with SIM900A...");
  for (int i = 0; i < 6; i++) {
    gsmSerial.print("AT\r\n");
    delay(250);
    while (gsmSerial.available()) gsmSerial.read();
  }
  // Lock SIM900A to 9600 baud, disable echo, set SMS text mode
  gsmSerial.print("AT+IPR=9600\r\n");
  delay(150);
  gsmSerial.print("ATE0\r\n");
  delay(150);
  gsmSerial.print("AT+CMGF=1\r\n");
  delay(150);

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
    Serial.print("[WIFI] Connected! Gateway IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] Router connection timed out. Starting SoftAP Fallback...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WIFI] SoftAP active! SSID: '%s' | Password: '%s'\n", AP_SSID, AP_PASSWORD);
    Serial.print("[WIFI] Access gateway at: http://");
    Serial.println(WiFi.softAPIP());
  }

  // Setup mDNS
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("[mDNS] Responding at: http://%s.local\n", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  // Setup Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/call", HTTP_GET, handleCall);
  server.on("/api/hangup", HTTP_GET, handleHangup);
  server.on("/api/send-sms", HTTP_GET, handleSendSms);
  server.on("/api/at", HTTP_GET, handleCustomAt);
  setupWebOTA();
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[HTTP] Web server listening on port 80.");

  setupArduinoOTA();

  // Initial modem check
  queryGsmStatus();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  // Read any unsolicited asynchronous codes from GSM (e.g. RING, NO CARRIER)
  while (gsmSerial.available()) {
    String asyncLine = gsmSerial.readStringUntil('\n');
    asyncLine.trim();
    if (asyncLine.length() > 0) {
      addLog("< [ASYNC] " + asyncLine);
      if (asyncLine.indexOf("RING") >= 0) {
        activeCallState = "INCOMING CALL RINGING...";
      } else if (asyncLine.indexOf("NO CARRIER") >= 0) {
        activeCallState = "IDLE (CALL DISCONNECTED)";
      } else if (asyncLine.indexOf("BUSY") >= 0) {
        activeCallState = "IDLE (LINE BUSY)";
      }
    }
  }

  // Periodically query signal and network registration
  unsigned long now = millis();
  if (now - lastStatusCheck >= STATUS_CHECK_INTERVAL_MS) {
    lastStatusCheck = now;
    queryGsmStatus();
  }
}
