/* ====================================================================
 * ESP32 + GSM MODULE (SIM800L / SIM900 / SIM7600 / SIM900A) CALL, SMS & ALERT GATEWAY
 * Microcontroller : ESP32 (Dev Module / WROOM-32 / ESP32-S3 / Nano ESP32)
 * GSM Module      : SIM800L / SIM900 / SIM800C / SIM7600 / SIM900A
 * Communication   : Hardware Serial (Standard ESP32: GPIO 16 RX2, GPIO 17 TX2 @ 9600 Baud)
 *                   (ESP32-S3 / Nano: GPIO 4 RX, GPIO 5 TX @ 9600 Baud)
 * Hardware Output : Status LED (GPIO 2 / 13), Alert Strobe LED (GPIO 12 / 7), Siren Relay (GPIO 14 / 6)
 * Cloud Endpoint  : https://zenithkandel.com.np/hydra/backend/api/nodes/level1.php
 * Wi-Fi           : SSID "sakshyam" | Password "sakshyam"
 * Features        : Power-On Startup Boot Call, 1-Time Call per Alert Type,
 *                   Cloud API Uplink Integration, Automated Emergency Calls & SMS,
 *                   Hardware Siren/LED Indication, Local Web Dashboard, Live AT Console
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
 * 1. WI-FI & CLOUD CONFIGURATION
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

// Production Level 1 Cloud Endpoint (per a.md Section 1 & Section 3)
const char* API_LEVEL1_URL = "https://zenithkandel.com.np/hydra/backend/api/nodes/level1.php";
const unsigned long CLOUD_POLL_INTERVAL_MS = 1500; // Poll server every 1.5s
unsigned long lastCloudPollTime = 0;

/* ====================================================================
 * 2. HARDWARE PIN DEFINITIONS & MAPPING
 * ==================================================================== */
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define GSM_RX_PIN     4   // ESP32-S3 GPIO 4 -> Connect to GSM Module TX
  #define GSM_TX_PIN     5   // ESP32-S3 GPIO 5 -> Connect to GSM Module RX
  #define GSM_UART_NUM   1
  #define STATUS_LED_PIN 13  // Onboard Status LED (Heartbeat / Network)
  #define ALERT_LED_PIN  7   // Red Strobe Alert LED
  #define SIREN_PIN      6   // Audible Siren / Piezo Buzzer Relay
#else
  #define GSM_RX_PIN     16  // Standard ESP32 GPIO 16 (RX2) -> GSM Module TX
  #define GSM_TX_PIN     17  // Standard ESP32 GPIO 17 (TX2) -> GSM Module RX
  #define GSM_UART_NUM   2
  #define STATUS_LED_PIN 2   // Standard ESP32 Onboard LED (GPIO 2)
  #define ALERT_LED_PIN  12  // High-Intensity Strobe Alert LED (GPIO 12)
  #define SIREN_PIN      14  // Siren / Buzzer Control Pin (GPIO 14)
#endif

// Siren / Buzzer Drive Polarity:
// Set to 'true' if Buzzer (+) is connected to 3.3V/VCC and (-) to GPIO 14 (Active-LOW: LOW = ON, HIGH = OFF)
// Set to 'false' if Buzzer (+) is connected to GPIO 14 and (-) to GND (Active-HIGH: HIGH = ON, LOW = OFF)
#define SIREN_ACTIVE_LOW true

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

// Power-On & 1-Time Alert Dispatch Tracker
bool bootCallDispatched = false;
String defaultBootCallNumber = "+9779800000000"; // Power-on notification phone number
String lastDispatchedAlertType = "";             // Strictly 1 call per alert type tracker

// Cloud Server Sync State
bool cloudPollConnected = false;
bool cloudEmergencyActive = false;
String cloudSirenState = "OFF";
String cloudBreachSummary = "NOMINAL";

// Alert & LED Indication State
bool isAlertActive = false;
String alertLevel = "NONE";       // NONE, INFO, WARNING, CRITICAL, EMERGENCY
String alertSourceNode = "NONE";  // e.g. FLOOD-01, FIRE-01, LANDSLIDE-01
String alertMessage = "NOMINAL";
unsigned long alertStartTime = 0;

// LED Modes
enum StatusLedState { STATUS_HEARTBEAT, STATUS_BUSY, STATUS_ERROR, STATUS_OFF, STATUS_ON };
enum AlertLedState  { ALERT_OFF, ALERT_ON, ALERT_BLINK, ALERT_STROBE };

StatusLedState statusLedMode = STATUS_HEARTBEAT;
AlertLedState  alertLedMode  = ALERT_OFF;
bool sirenActive = false;

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

// Function Prototypes
bool gsmMakeCall(String phoneNumber);
bool gsmSendSMS(String phoneNumber, String message);
bool gsmHangupCall();
bool triggerAlert(String node, String level, String msg, String targetNum, bool doCall, bool doSms);
void clearAlert();

/* ====================================================================
 * 4. LED & HARDWARE OUTPUT ROUTINES (NON-BLOCKING STATE MACHINE)
 * ==================================================================== */
unsigned long lastStatusLedToggle = 0;
unsigned long lastAlertLedToggle  = 0;
bool statusLedCurState = LOW;
bool alertLedCurState  = LOW;

void updateHardwareOutputs() {
  unsigned long now = millis();

  // 1. Status LED Handling
  if (statusLedMode == STATUS_OFF) {
    statusLedCurState = LOW;
  } else if (statusLedMode == STATUS_ON) {
    statusLedCurState = HIGH;
  } else if (statusLedMode == STATUS_BUSY) {
    if (now - lastStatusLedToggle >= 100) {
      lastStatusLedToggle = now;
      statusLedCurState = !statusLedCurState;
    }
  } else if (statusLedMode == STATUS_ERROR) {
    if (now - lastStatusLedToggle >= 250) {
      lastStatusLedToggle = now;
      statusLedCurState = !statusLedCurState;
    }
  } else {
    // Default Heartbeat: 100ms pulse every 1500ms
    unsigned long phase = now % 1500;
    statusLedCurState = (phase < 100) ? HIGH : LOW;
  }
  digitalWrite(STATUS_LED_PIN, statusLedCurState);

  // 2. Alert Strobe LED Handling
  if (alertLedMode == ALERT_OFF) {
    alertLedCurState = LOW;
  } else if (alertLedMode == ALERT_ON) {
    alertLedCurState = HIGH;
  } else if (alertLedMode == ALERT_BLINK) {
    if (now - lastAlertLedToggle >= 250) {
      lastAlertLedToggle = now;
      alertLedCurState = !alertLedCurState;
    }
  } else if (alertLedMode == ALERT_STROBE) {
    if (now - lastAlertLedToggle >= 60) {
      lastAlertLedToggle = now;
      alertLedCurState = !alertLedCurState;
    }
  }
  digitalWrite(ALERT_LED_PIN, alertLedCurState);

  // 3. Siren Control (Active-LOW: LOW = ON, HIGH = OFF)
  digitalWrite(SIREN_PIN, sirenActive ? (SIREN_ACTIVE_LOW ? LOW : HIGH) : (SIREN_ACTIVE_LOW ? HIGH : LOW));
}

/* ====================================================================
 * 5. AT COMMAND HELPER & CONSOLE LOGGING
 * ==================================================================== */

void addLog(String text) {
  text.trim();
  if (text.length() == 0) return;
  
  text.replace("\r", " ");
  text.replace("\n", " ");
  while (text.indexOf("  ") >= 0) text.replace("  ", " ");

  logLines[logIndex] = text;
  logIndex = (logIndex + 1) % MAX_LOG_LINES;
  Serial.println("[GSM LOG] " + text);
}

String sendATCommand(String cmd, unsigned long timeoutMs = 2000, String expectedReply = "OK") {
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
 * 6. GSM HARDWARE ROUTINES (CALL, SMS, POWER-ON & ALERT DISPATCH)
 * ==================================================================== */

void queryGsmStatus() {
  if (isOperationInProgress) return;

  String atResp = sendATCommand("AT", 1000);
  if (atResp.indexOf("OK") >= 0) {
    isGsmAlive = true;
  } else {
    isGsmAlive = false;
    gsmStatusText = "NO RESPONSE FROM GSM MODULE (CHECK POWER & WIRING)";
    statusLedMode = STATUS_ERROR;
    return;
  }

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

  String cregResp = sendATCommand("AT+CREG?", 1500);
  int cregIdx = cregResp.indexOf("+CREG:");
  if (cregIdx >= 0) {
    int commaIdx = cregResp.indexOf(",", cregIdx);
    if (commaIdx > cregIdx && commaIdx + 1 < cregResp.length()) {
      gsmNetworkReg = cregResp.substring(commaIdx + 1, commaIdx + 2).toInt();
    }
  }

  String copsResp = sendATCommand("AT+COPS?", 2000);
  int quote1 = copsResp.indexOf("\"");
  if (quote1 >= 0) {
    int quote2 = copsResp.indexOf("\"", quote1 + 1);
    if (quote2 > quote1) {
      gsmOperator = copsResp.substring(quote1 + 1, quote2);
    }
  }

  if (gsmNetworkReg == 1 || gsmNetworkReg == 5) {
    gsmStatusText = (gsmNetworkReg == 1) ? "CONNECTED (HOME NETWORK)" : "CONNECTED (ROAMING)";
    if (!isAlertActive) statusLedMode = STATUS_HEARTBEAT;

    // Power-On Startup Call: Triggers 1-time call when power is turned ON and network connects
    if (!bootCallDispatched && defaultBootCallNumber.length() > 2) {
      bootCallDispatched = true;
      addLog("[POWER-ON BOOT DISPATCH] Power ON detected! Dispatching 1-time startup boot call to " + defaultBootCallNumber);
      gsmMakeCall(defaultBootCallNumber);
    }
  } else if (gsmNetworkReg == 2) {
    gsmStatusText = "SEARCHING FOR CELLULAR TOWER...";
    statusLedMode = STATUS_BUSY;
  } else if (gsmNetworkReg == 3) {
    gsmStatusText = "REGISTRATION DENIED BY CARRIER";
    statusLedMode = STATUS_ERROR;
  } else {
    gsmStatusText = "NOT REGISTERED / NO SIM CARD";
    statusLedMode = STATUS_ERROR;
  }
}

bool gsmMakeCall(String phoneNumber) {
  isOperationInProgress = true;
  statusLedMode = STATUS_BUSY;
  phoneNumber.trim();
  phoneNumber.replace(" ", "");
  phoneNumber.replace("-", "");

  if (phoneNumber.length() < 3) {
    lastActionResult = "ERROR: INVALID PHONE NUMBER";
    lastActionSuccess = false;
    isOperationInProgress = false;
    statusLedMode = STATUS_HEARTBEAT;
    return false;
  }

  addLog("[CALL] Dialing: " + phoneNumber);
  activeCallState = "DIALING " + phoneNumber + "...";

  String resp = sendATCommand("ATD" + phoneNumber + ";", 5000);

  if (resp.indexOf("OK") >= 0 || resp.indexOf("VOICE") >= 0) {
    activeCallState = "CALL IN PROGRESS - " + phoneNumber;
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
  statusLedMode = STATUS_HEARTBEAT;
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
  statusLedMode = STATUS_HEARTBEAT;
  return true;
}

bool gsmSendSMS(String phoneNumber, String message) {
  isOperationInProgress = true;
  statusLedMode = STATUS_BUSY;
  phoneNumber.trim();
  phoneNumber.replace(" ", "");
  phoneNumber.replace("-", "");
  message.trim();

  if (phoneNumber.length() < 3 || message.length() == 0) {
    lastActionResult = "ERROR: PHONE NUMBER OR MESSAGE CANNOT BE EMPTY";
    lastActionSuccess = false;
    isOperationInProgress = false;
    statusLedMode = STATUS_HEARTBEAT;
    return false;
  }

  addLog("[SMS] Setting Text Mode (AT+CMGF=1)...");
  sendATCommand("AT+CMGF=1", 1500, "OK");
  sendATCommand("AT+CSCS=\"GSM\"", 1000, "OK");

  while (gsmSerial.available()) gsmSerial.read();

  String cmgsCmd = "AT+CMGS=\"" + phoneNumber + "\"";
  addLog("> " + cmgsCmd);
  gsmSerial.print(cmgsCmd + "\r\n");

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
    gsmSerial.write(0x1B);
    isOperationInProgress = false;
    statusLedMode = STATUS_HEARTBEAT;
    return false;
  }

  addLog("> [SENDING MESSAGE BODY + CTRL+Z]...");
  gsmSerial.print(message);
  gsmSerial.write(0x1A);

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
  statusLedMode = STATUS_HEARTBEAT;
  return lastActionSuccess;
}

bool triggerAlert(String node, String level, String msg, String targetNum, bool doCall, bool doSms) {
  isAlertActive = true;
  alertSourceNode = node;
  alertLevel = level;
  alertMessage = msg;
  alertStartTime = millis();

  String alertKey = node + ":" + level;

  addLog("[ALERT ENGINE] Emergency Alert Triggered! Node: " + node + " | Level: " + level);

  if (level == "CRITICAL" || level == "EMERGENCY") {
    alertLedMode = ALERT_STROBE;
    sirenActive = true;
  } else if (level == "WARNING") {
    alertLedMode = ALERT_BLINK;
    sirenActive = false;
  } else {
    alertLedMode = ALERT_ON;
    sirenActive = false;
  }

  bool smsResult = true;
  bool callResult = true;

  // Strictly 1-Time Call & SMS per Alert Type!
  if (alertKey != lastDispatchedAlertType) {
    if (doSms && targetNum.length() > 2) {
      String smsContent = "[HYDRA ALERT]\nNODE: " + node + "\nLEVEL: " + level + "\nMSG: " + msg;
      smsResult = gsmSendSMS(targetNum, smsContent);
    }

    if (doCall && targetNum.length() > 2) {
      callResult = gsmMakeCall(targetNum);
    }
    lastDispatchedAlertType = alertKey;
    addLog("[ALERT ENGINE] Dispatched 1-time call/SMS for alert type: " + alertKey);
  } else {
    addLog("[ALERT ENGINE] Alert '" + alertKey + "' already dispatched. Skipping duplicate call.");
  }

  lastActionResult = "ALERT ACTIVE: " + level + " (" + node + ")";
  lastActionSuccess = smsResult && callResult;
  lastActionTime = millis();
  return true;
}

void clearAlert() {
  isAlertActive = false;
  alertLevel = "NONE";
  alertSourceNode = "NONE";
  alertMessage = "NOMINAL";
  alertLedMode = ALERT_OFF;
  sirenActive = false;
  lastDispatchedAlertType = ""; // Reset dispatch tracker on clear

  if (activeCallState.indexOf("CALL IN PROGRESS") >= 0 || activeCallState.indexOf("DIALING") >= 0) {
    gsmHangupCall();
  }

  lastActionResult = "ALERT CLEARED / DISARMED";
  lastActionSuccess = true;
  lastActionTime = millis();
  addLog("[ALERT ENGINE] System disarmed. 1-time alert dispatch tracker reset.");
}

/* ====================================================================
 * 7. CLOUD SERVER POLLING ROUTINE (per a.md Section 3)
 * ==================================================================== */
void pollLevel1CloudServer() {
  if (WiFi.status() != WL_CONNECTED || isOperationInProgress) return;

  WiFiClientSecure client;
  client.setInsecure(); // Bypass SSL certificate verification for production endpoint

  HTTPClient http;
  if (!http.begin(client, API_LEVEL1_URL)) {
    cloudPollConnected = false;
    return;
  }

  http.setTimeout(2500);
  int httpCode = http.GET();

  if (httpCode == 200) {
    cloudPollConnected = true;
    String payload = http.getString();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && doc["status"] == "SUCCESS") {
      JsonObject data = doc["data"];

      bool emergency = data["emergency"] | false;
      const char* sirenStr = data["siren"] | "OFF";
      cloudEmergencyActive = emergency;

      // 1. Siren & Strobe Hardware Control
      if (emergency || strcmp(sirenStr, "ON") == 0) {
        isAlertActive = true;
        alertLevel = "EMERGENCY";
        alertSourceNode = data["node"] | "LEVEL_1_VILLAGE_MASTER";
        alertMessage = data["breach_summary"] | "DISASTER ALERT TRIGGERED";
        alertLedMode = ALERT_STROBE;
        sirenActive = true;

        String serverAlertType = data["breach_summary"] | "EMERGENCY_DISASTER";
        String targetPhone = defaultBootCallNumber;
        if (!data["gsm_receiver_number"].isNull()) {
          targetPhone = data["gsm_receiver_number"].as<String>();
        }
        String smsMsg = alertMessage;
        if (!data["gsm_message"].isNull()) {
          smsMsg = data["gsm_message"].as<String>();
        }

        // Strictly 1-Time Call & SMS per Server Alert Type
        if (serverAlertType != lastDispatchedAlertType && targetPhone.length() > 2) {
          addLog("[CLOUD DISPATCH] New Server Alert '" + serverAlertType + "' received. Dispatching 1-time call to " + targetPhone);
          gsmSendSMS(targetPhone, smsMsg);
          gsmMakeCall(targetPhone);
          lastDispatchedAlertType = serverAlertType;
        }
      } else {
        if (isAlertActive && alertSourceNode == "LEVEL_1_VILLAGE_MASTER") {
          isAlertActive = false;
          alertLevel = "NONE";
          alertSourceNode = "NONE";
          alertMessage = "NOMINAL";
          alertLedMode = ALERT_OFF;
          sirenActive = false;
          lastDispatchedAlertType = ""; // Reset dispatch tracker when server clears emergency
        }
      }

      if (!data["breach_summary"].isNull()) {
        cloudBreachSummary = data["breach_summary"].as<String>();
      } else {
        cloudBreachSummary = "NOMINAL";
      }
    }
  } else {
    cloudPollConnected = false;
  }
  http.end();
}

/* ====================================================================
 * 8. EMBEDDED SHARP MONOCHROME WEB DASHBOARD HTML
 * ==================================================================== */
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 // CELLULAR GSM & ALERT GATEWAY</title>
  <style>
    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      border-radius: 0 !important;
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
      max-width: 980px;
      margin: 0 auto;
    }

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
    .badge.alert-active { background: #FFFFFF; color: #000000; animation: flash 0.5s infinite alternate; }

    @keyframes flash { 0% { opacity: 1; } 100% { opacity: 0.3; } }

    .pulse {
      display: inline-block;
      width: 8px;
      height: 8px;
      background: #000000;
      animation: blink 1s steps(1) infinite;
    }

    @keyframes blink { 50% { opacity: 0; } }

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

    .modem-hero {
      border: 2px solid #FFFFFF;
      background: #080808;
      padding: 18px;
      margin-bottom: 16px;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
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
      font-size: 17px;
      font-weight: 900;
      letter-spacing: 0.5px;
      color: #FFFFFF;
    }

    .stat-box-sub {
      font-size: 11px;
      color: #AAAAAA;
      margin-top: 3px;
    }

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

    .workspace-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(310px, 1fr));
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

    input[type=text], input[type=tel], select, textarea {
      width: 100%;
      background: #000000;
      color: #FFFFFF;
      border: 1px solid #444444;
      padding: 12px 14px;
      font-family: inherit;
      font-size: 13px;
      font-weight: 700;
      outline: none;
      transition: border-color 0.1s ease;
    }

    input[type=text]:focus, input[type=tel]:focus, select:focus, textarea:focus {
      border-color: #FFFFFF;
    }

    textarea {
      resize: vertical;
      min-height: 70px;
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
      gap: 8px;
      flex-wrap: wrap;
      margin-top: 6px;
    }

    .btn {
      flex: 1;
      min-width: 100px;
      padding: 11px 14px;
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

    .btn.outline {
      background: #000000;
      color: #FFFFFF;
      border-color: #444444;
    }
    .btn.outline:hover {
      border-color: #FFFFFF;
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

    .checkbox-label {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      font-size: 11px;
      color: #CCCCCC;
      cursor: pointer;
      margin-right: 12px;
    }

    .console-box {
      border: 1px solid #FFFFFF;
      background: #000000;
      padding: 14px;
      height: 220px;
      overflow-y: auto;
      font-size: 11px;
      line-height: 1.5;
    }

    .console-line {
      margin-bottom: 2px;
      word-break: break-all;
    }

    .console-line.tx { color: #FFFFFF; font-weight: 700; }
    .console-line.rx { color: #888888; }

    .pin-table {
      width: 100%;
      border-collapse: collapse;
      font-size: 11px;
      margin-top: 8px;
    }
    .pin-table th, .pin-table td {
      border: 1px solid #333333;
      padding: 6px 10px;
      text-align: left;
    }
    .pin-table th {
      background: #111111;
      color: #888888;
      text-transform: uppercase;
      font-size: 10px;
      letter-spacing: 1px;
    }

    code {
      background: #111;
      padding: 2px 5px;
      border: 1px solid #333;
      font-size: 11px;
    }

    footer {
      border-top: 1px solid #222222;
      padding-top: 16px;
      margin-top: 24px;
      font-size: 10px;
      color: #666666;
      display: flex;
      justify-content: space-between;
      letter-spacing: 1px;
    }
  </style>
</head>
<body>

  <div class="container">

    <!-- HEADER BAR -->
    <header>
      <div class="title-group">
        <h1>HYDRA // GSM CELLULAR &amp; ALERT GATEWAY</h1>
        <p>DISASTER TELEMETRY TELEPHONY &amp; DISPATCH NODE</p>
      </div>
      <div class="badges">
        <span id="alert-status-badge" class="badge outline">[ STATUS: NORMAL ]</span>
        <span class="badge solid"><span class="pulse"></span> ONLINE</span>
        <a href="/update" class="badge outline">[ FIRMWARE OTA ]</a>
      </div>
    </header>

    <!-- RECENT ACTION FEEDBACK -->
    <div id="feedback-banner" class="feedback-banner success">
      <div>
        <strong id="feedback-prefix">SYSTEM:</strong> <span id="feedback-text">GATEWAY INITIALIZED &amp; READY</span>
      </div>
      <div id="feedback-time" style="font-size: 10px; color: #888888;">JUST NOW</div>
    </div>

    <!-- MODEM & HARDWARE STATUS HERO -->
    <div class="modem-hero">
      <div>
        <div class="stat-box-label">CELLULAR MODEM</div>
        <div id="val-gsm-alive" class="stat-box-value">CHECKING...</div>
        <div id="val-operator" class="stat-box-sub">CARRIER: --</div>
      </div>
      <div>
        <div class="stat-box-label">SIGNAL STRENGTH</div>
        <div id="val-signal-pct" class="stat-box-value">0%</div>
        <div id="val-signal-dbm" class="stat-box-sub">-115 dBm (CSQ 99)</div>
        <div class="signal-track"><div id="signal-fill" class="signal-fill"></div></div>
      </div>
      <div>
        <div class="stat-box-label">TELEPHONY STATE</div>
        <div id="val-call-state" class="stat-box-value">IDLE</div>
        <div id="val-last-num" class="stat-box-sub">LAST DIALED: --</div>
      </div>
      <div>
        <div class="stat-box-label">HYDRA CLOUD UPLINK</div>
        <div id="val-cloud-status" class="stat-box-value">CONNECTED</div>
        <div id="val-cloud-breach" class="stat-box-sub">SUMMARY: NOMINAL</div>
      </div>
    </div>

    <!-- MAIN CONTROL PANELS -->
    <div class="workspace-grid">

      <!-- EMERGENCY ALERT DISPATCH PANEL -->
      <div class="panel" style="border-width: 2px;">
        <div>
          <div class="panel-header">
            <span>🚨 EMERGENCY DISPATCH ENGINE</span>
            <span style="font-size: 10px; color: #888;">/api/alert</span>
          </div>
          <div class="form-group">
            <label>TARGET EMERGENCY PHONE NUMBER (POWER-ON &amp; DISPATCH)</label>
            <input type="tel" id="alert-phone" placeholder="+9779800000000" value="+9779800000000">
          </div>
          <div class="form-group">
            <label>HAZARD SEVERITY LEVEL</label>
            <select id="alert-level">
              <option value="EMERGENCY" selected>🚨 EMERGENCY (STROBE + SIREN + CALL + SMS)</option>
              <option value="CRITICAL">⚠️ CRITICAL (STROBE + CALL + SMS)</option>
              <option value="WARNING">🌊 WARNING (LED BLINK + SMS)</option>
              <option value="INFO">ℹ️ INFO (LED ON)</option>
            </select>
          </div>
          <div class="form-group">
            <label>ORIGINATING NODE ID</label>
            <input type="text" id="alert-node" value="FLOOD-MODI-KHOLA">
          </div>
          <div class="form-group">
            <label>ALERT MESSAGE DETAIL</label>
            <textarea id="alert-msg">CRITICAL FLOOD SURGE DETECTED! Water level clearance < 20cm. Immediate evacuation required!</textarea>
          </div>
          <div class="form-group">
            <label class="checkbox-label"><input type="checkbox" id="alert-do-call" checked> Initiate Voice Call</label>
            <label class="checkbox-label"><input type="checkbox" id="alert-do-sms" checked> Send SMS Alert</label>
          </div>
        </div>
        <div class="btn-row">
          <button class="btn danger" onclick="triggerEmergencyAlert()">[ 🚨 DISPATCH ALERT ]</button>
          <button class="btn outline" onclick="clearEmergencyAlert()">[ DISARM / CLEAR ]</button>
        </div>
      </div>

      <!-- DIRECT CALL & SMS CONTROLLER -->
      <div class="panel">
        <div>
          <div class="panel-header">
            <span>📞 VOICE CALL &amp; SMS SENDER</span>
            <span style="font-size: 10px; color: #888;">/api/call &amp; /api/send-sms</span>
          </div>
          <div class="form-group">
            <label>DESTINATION PHONE NUMBER</label>
            <input type="tel" id="phone-number" placeholder="+9779800000000">
          </div>
          <div class="btn-row" style="margin-bottom: 14px;">
            <button class="btn" onclick="makeCall()">[ DIAL CALL ]</button>
            <button class="btn danger" onclick="hangupCall()">[ HANGUP ]</button>
          </div>
          <div class="form-group">
            <label>DIRECT SMS MESSAGE</label>
            <textarea id="sms-msg" placeholder="Type SMS content..." oninput="updateCharCount()"></textarea>
            <div class="char-count"><span id="char-count">0</span> / 160 CHARS</div>
          </div>
        </div>
        <div class="btn-row">
          <button class="btn" onclick="sendSms()">[ SEND SMS ]</button>
        </div>
      </div>

      <!-- LED & SIREN HARDWARE CONTROLLER -->
      <div class="panel">
        <div>
          <div class="panel-header">
            <span>💡 HARDWARE LED &amp; SIREN OVERRIDE</span>
            <span style="font-size: 10px; color: #888;">/api/led</span>
          </div>
          <div class="form-group">
            <label>ALERT STROBE LED (RED)</label>
            <div class="btn-row">
              <button class="btn outline" onclick="controlLed('alert', 'off')">OFF</button>
              <button class="btn outline" onclick="controlLed('alert', 'on')">ON</button>
              <button class="btn outline" onclick="controlLed('alert', 'blink')">BLINK</button>
              <button class="btn" onclick="controlLed('alert', 'strobe')">STROBE</button>
            </div>
          </div>
          <div class="form-group">
            <label>AUDIBLE SIREN / BUZZER</label>
            <div class="btn-row">
              <button class="btn outline" onclick="controlLed('siren', 'off')">SIREN OFF</button>
              <button class="btn danger" onclick="controlLed('siren', 'on')">SIREN ON</button>
            </div>
          </div>
          <div class="form-group">
            <label>SYSTEM STATUS LED (BLUE)</label>
            <div class="btn-row">
              <button class="btn outline" onclick="controlLed('status', 'heartbeat')">HEARTBEAT</button>
              <button class="btn outline" onclick="controlLed('status', 'busy')">BUSY</button>
              <button class="btn outline" onclick="controlLed('status', 'error')">ERROR</button>
            </div>
          </div>
        </div>
      </div>

    </div>

    <!-- CONSOLE & API REFERENCE SECTION -->
    <div class="section-title">
      <span>📟 REAL-TIME AT CONSOLE &amp; LOGS</span>
      <span style="font-size: 10px; color: #888;">UART BAUD: 9600</span>
    </div>

    <div class="console-box" id="console-box">
      <div class="console-line rx">SYSTEM // INITIALIZING MODEM CONSOLE LOG...</div>
    </div>

    <div style="margin-top: 10px; display: flex; gap: 8px;">
      <input type="text" id="custom-at" placeholder="ENTER CUSTOM AT COMMAND (e.g. AT+CSQ, AT+COPS?, ATI)..." onkeypress="if(event.key==='Enter') sendCustomAt()">
      <button class="btn" style="flex: 0 0 140px;" onclick="sendCustomAt()">[ EXECUTE AT ]</button>
    </div>

    <!-- HARDWARE PIN MAPPING & API DOCUMENTATION -->
    <div class="section-title">
      <span>📌 PIN MAPPING &amp; API ENDPOINT MATRIX</span>
    </div>

    <div class="panel">
      <table class="pin-table">
        <thead>
          <tr>
            <th>Function / Hardware Signal</th>
            <th>Standard ESP32 Pin</th>
            <th>ESP32-S3 Pin</th>
            <th>Target Module / Component</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td><strong>GSM Modem TX (Receive at ESP32)</strong></td>
            <td><code>GPIO 16 (RX2)</code></td>
            <td><code>GPIO 4</code></td>
            <td>SIM800L / SIM900 / SIM900A TXD Pin</td>
          </tr>
          <tr>
            <td><strong>GSM Modem RX (Transmit from ESP32)</strong></td>
            <td><code>GPIO 17 (TX2)</code></td>
            <td><code>GPIO 5</code></td>
            <td>SIM800L / SIM900 / SIM900A RXD Pin</td>
          </tr>
          <tr>
            <td><strong>System Status LED</strong></td>
            <td><code>GPIO 2</code></td>
            <td><code>GPIO 13</code></td>
            <td>Onboard Blue LED (Heartbeat / Traffic)</td>
          </tr>
          <tr>
            <td><strong>Alert Strobe LED</strong></td>
            <td><code>GPIO 12</code></td>
            <td><code>GPIO 7</code></td>
            <td>High-Intensity Red Warning LED</td>
          </tr>
          <tr>
            <td><strong>Audible Siren / Buzzer</strong></td>
            <td><code>GPIO 14</code></td>
            <td><code>GPIO 6</code></td>
            <td>Active-LOW Buzzer (+ to 3.3V, - to GPIO 14)</td>
          </tr>
        </tbody>
      </table>

      <div style="margin-top: 14px; font-size: 11px; color: #AAA; line-height: 1.6;">
        <strong style="color: #FFF;">Available REST API Endpoints:</strong><br>
        • <code>GET /api/status</code> or <code>/api/data</code> - Live status JSON (power boot state, 1-time alert tracker, cloud sync, call state, LED)<br>
        • <code>GET /api/call?num=+9779800000000</code> - Initiate cellular voice call<br>
        • <code>GET /api/hangup</code> - Terminate active call<br>
        • <code>GET /api/send-sms?num=+9779800000000&amp;msg=Hello</code> - Send SMS message<br>
        • <code>GET /api/alert?node=FLOOD-01&amp;level=CRITICAL&amp;msg=Evacuate&amp;num=+9779800000000&amp;call=true&amp;sms=true</code> - Dispatch 1-Time Emergency Alert<br>
        • <code>GET /api/alert?clear=true</code> - Disarm / clear emergency alert and reset 1-time dispatch tracker<br>
        • <code>GET /api/led?alert=strobe|blink|on|off&amp;status=heartbeat|busy|error&amp;siren=on|off</code> - Direct hardware control
      </div>
    </div>

    <!-- FOOTER -->
    <footer>
      <div>HYDRA DISASTER MONITORING NETWORK // GSM GATEWAY</div>
      <div id="val-uptime">UPTIME: 00:00:00</div>
    </footer>

  </div>

  <script>
    async function pollStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();

        // Modem status
        document.getElementById('val-gsm-alive').innerText = data.gsm.alive ? "READY & ALIVE" : "OFFLINE / ERROR";
        document.getElementById('val-operator').innerText = "CARRIER: " + (data.gsm.operator || "UNKNOWN");
        document.getElementById('val-signal-pct').innerText = data.gsm.signal_percent + "%";
        document.getElementById('val-signal-dbm').innerText = data.gsm.signal_dbm + " dBm (CSQ " + data.gsm.rssi + ")";
        document.getElementById('signal-fill').style.width = data.gsm.signal_percent + "%";

        // Call state
        document.getElementById('val-call-state').innerText = data.call.state;
        document.getElementById('val-last-num').innerText = "LAST DIALED: " + (data.call.last_number || "--");

        // Cloud Uplink Status
        document.getElementById('val-cloud-status').innerText = data.cloud.connected ? "ACTIVE UPLINK" : "UPLINK ERROR";
        document.getElementById('val-cloud-breach').innerText = "SUMMARY: " + (data.cloud.breach_summary || "NOMINAL");

        // Alert & LED status
        const alertBadge = document.getElementById('alert-status-badge');
        if (data.alert.active) {
          alertBadge.innerText = "[ 🚨 ALERT: " + data.alert.level + " (" + data.alert.source_node + ") ]";
          alertBadge.className = "badge alert-active";
        } else {
          alertBadge.innerText = "[ STATUS: NORMAL ]";
          alertBadge.className = "badge outline";
        }

        // Action feedback
        document.getElementById('feedback-text').innerText = data.action.text;
        const banner = document.getElementById('feedback-banner');
        banner.className = "feedback-banner " + (data.action.success ? "success" : "error");

        // Uptime
        document.getElementById('val-uptime').innerText = "UPTIME: " + formatUptime(data.sys.uptime_sec);

        // Logs
        if (data.logs) {
          const consoleBox = document.getElementById('console-box');
          consoleBox.innerHTML = data.logs.map(line => {
            const isTx = line.startsWith('>');
            return `<div class="console-line ${isTx ? 'tx' : 'rx'}">${escapeHtml(line)}</div>`;
          }).join('');
          consoleBox.scrollTop = consoleBox.scrollHeight;
        }

      } catch (e) {
        console.error("Poll error:", e);
      }
    }

    async function triggerEmergencyAlert() {
      const num = document.getElementById('alert-phone').value.trim();
      const level = document.getElementById('alert-level').value;
      const node = document.getElementById('alert-node').value.trim();
      const msg = document.getElementById('alert-msg').value.trim();
      const doCall = document.getElementById('alert-do-call').checked;
      const doSms = document.getElementById('alert-do-sms').checked;

      document.getElementById('feedback-text').innerText = "DISPATCHING EMERGENCY ALERT SEQUENCE...";
      try {
        const url = `/api/alert?node=${encodeURIComponent(node)}&level=${encodeURIComponent(level)}&msg=${encodeURIComponent(msg)}&num=${encodeURIComponent(num)}&call=${doCall}&sms=${doSms}`;
        const res = await fetch(url);
        const data = await res.json();
        pollStatus();
      } catch (e) {
        alert("Alert Error: " + e.message);
      }
    }

    async function clearEmergencyAlert() {
      document.getElementById('feedback-text').innerText = "DISARMING ALERT & MUTING SIREN/STROBE...";
      try {
        const res = await fetch('/api/alert?clear=true');
        const data = await res.json();
        pollStatus();
      } catch (e) {
        alert("Clear Error: " + e.message);
      }
    }

    async function controlLed(type, mode) {
      try {
        let url = '/api/led?';
        if (type === 'alert') url += 'alert=' + mode;
        else if (type === 'status') url += 'status=' + mode;
        else if (type === 'siren') url += 'siren=' + mode;

        await fetch(url);
        pollStatus();
      } catch (e) {
        alert("LED Error: " + e.message);
      }
    }

    async function makeCall() {
      const num = document.getElementById('phone-number').value.trim();
      if (!num) {
        alert("Please enter a destination phone number.");
        return;
      }
      document.getElementById('feedback-text').innerText = "DIALING " + num + "...";
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
      const num = document.getElementById('phone-number').value.trim();
      const msg = document.getElementById('sms-msg').value.trim();
      if (!num) {
        alert("Please enter a recipient phone number.");
        return;
      }
      if (!msg) {
        alert("Please enter SMS message text.");
        return;
      }
      document.getElementById('feedback-text').innerText = "DISPATCHING SMS TO " + num + "...";
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

    function escapeHtml(text) {
      return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }

    pollStatus();
    setInterval(pollStatus, 1500);
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
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptime_sec\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"boot_call_dispatched\":" + String(bootCallDispatched ? "true" : "false") + ",";
  json += "\"pins\":{";
  json += "\"gsm_rx\":" + String(GSM_RX_PIN) + ",";
  json += "\"gsm_tx\":" + String(GSM_TX_PIN) + ",";
  json += "\"status_led\":" + String(STATUS_LED_PIN) + ",";
  json += "\"alert_led\":" + String(ALERT_LED_PIN) + ",";
  json += "\"siren\":" + String(SIREN_PIN);
  json += "}";
  json += "},";

  json += "\"cloud\":{";
  json += "\"url\":\"" + String(API_LEVEL1_URL) + "\",";
  json += "\"connected\":" + String(cloudPollConnected ? "true" : "false") + ",";
  json += "\"emergency\":" + String(cloudEmergencyActive ? "true" : "false") + ",";
  json += "\"siren\":\"" + cloudSirenState + "\",";
  json += "\"breach_summary\":\"" + cloudBreachSummary + "\"";
  json += "},";

  json += "\"gsm\":{";
  json += "\"alive\":" + String(isGsmAlive ? "true" : "false") + ",";
  json += "\"rssi\":" + String(gsmRssi) + ",";
  json += "\"signal_percent\":" + String(gsmSignalPercent) + ",";
  json += "\"signal_dbm\":" + String(gsmSignalDbm) + ",";
  json += "\"network_reg\":" + String(gsmNetworkReg) + ",";
  json += "\"operator\":\"" + gsmOperator + "\",";
  json += "\"status_text\":\"" + gsmStatusText + "\"";
  json += "},";

  json += "\"call\":{";
  json += "\"state\":\"" + activeCallState + "\",";
  json += "\"last_number\":\"" + lastCalledNumber + "\",";
  json += "\"boot_target_num\":\"" + defaultBootCallNumber + "\"";
  json += "},";

  json += "\"alert\":{";
  json += "\"active\":" + String(isAlertActive ? "true" : "false") + ",";
  json += "\"level\":\"" + alertLevel + "\",";
  json += "\"source_node\":\"" + alertSourceNode + "\",";
  json += "\"message\":\"" + alertMessage + "\",";
  json += "\"last_dispatched_type\":\"" + lastDispatchedAlertType + "\"";
  json += "},";

  String alertModeStr = "OFF";
  if (alertLedMode == ALERT_ON) alertModeStr = "ON";
  else if (alertLedMode == ALERT_BLINK) alertModeStr = "BLINK";
  else if (alertLedMode == ALERT_STROBE) alertModeStr = "STROBE";

  String statusModeStr = "HEARTBEAT";
  if (statusLedMode == STATUS_BUSY) statusModeStr = "BUSY";
  else if (statusLedMode == STATUS_ERROR) statusModeStr = "ERROR";
  else if (statusLedMode == STATUS_OFF) statusModeStr = "OFF";
  else if (statusLedMode == STATUS_ON) statusModeStr = "ON";

  json += "\"led\":{";
  json += "\"status_mode\":\"" + statusModeStr + "\",";
  json += "\"alert_mode\":\"" + alertModeStr + "\",";
  json += "\"siren_active\":" + String(sirenActive ? "true" : "false");
  json += "},";

  json += "\"action\":{";
  json += "\"text\":\"" + lastActionResult + "\",";
  json += "\"success\":" + String(lastActionSuccess ? "true" : "false");
  json += "},";

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
  if (json.endsWith(",")) json.remove(json.length() - 1);
  json += "]";

  json += "}";

  server.send(200, "application/json", json);
}

void handleCall() {
  String num = "";
  if (server.hasArg("num")) num = server.arg("num");
  else if (server.hasArg("number")) num = server.arg("number");

  if (num.length() > 0) {
    bool ok = gsmMakeCall(num);
    server.send(200, "application/json", "{\"success\":" + String(ok ? "true" : "false") + ",\"called\":\"" + num + "\"}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing number parameter\"}");
  }
}

void handleHangup() {
  bool ok = gsmHangupCall();
  server.send(200, "application/json", "{\"success\":" + String(ok ? "true" : "false") + "}");
}

void handleSendSms() {
  String num = server.hasArg("num") ? server.arg("num") : server.arg("number");
  String msg = server.hasArg("msg") ? server.arg("msg") : server.arg("message");

  if (num.length() > 0 && msg.length() > 0) {
    bool ok = gsmSendSMS(num, msg);
    server.send(200, "application/json", "{\"success\":" + String(ok ? "true" : "false") + "}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing number or message parameter\"}");
  }
}

void handleAlert() {
  if (server.hasArg("clear") && (server.arg("clear") == "true" || server.arg("clear") == "1")) {
    clearAlert();
    server.send(200, "application/json", "{\"success\":true,\"action\":\"alert_cleared\"}");
    return;
  }

  String node  = server.hasArg("node")  ? server.arg("node")  : "GSM-GATEWAY";
  String level = server.hasArg("level") ? server.arg("level") : "EMERGENCY";
  String msg   = server.hasArg("msg")   ? server.arg("msg")   : "HYDRA Emergency Alert Triggered";
  String num   = server.hasArg("num")   ? server.arg("num")   : (server.hasArg("number") ? server.arg("number") : defaultBootCallNumber);
  bool doCall  = server.hasArg("call")  ? (server.arg("call") == "true" || server.arg("call") == "1") : false;
  bool doSms   = server.hasArg("sms")   ? (server.arg("sms") == "true" || server.arg("sms") == "1")   : false;

  bool ok = triggerAlert(node, level, msg, num, doCall, doSms);
  server.send(200, "application/json", "{\"success\":" + String(ok ? "true" : "false") + ",\"alert\":{\"active\":true,\"level\":\"" + level + "\",\"node\":\"" + node + "\"}}");
}

void handleLed() {
  if (server.hasArg("alert")) {
    String mode = server.arg("alert");
    mode.toLowerCase();
    if (mode == "on" || mode == "1") alertLedMode = ALERT_ON;
    else if (mode == "strobe") alertLedMode = ALERT_STROBE;
    else if (mode == "blink") alertLedMode = ALERT_BLINK;
    else alertLedMode = ALERT_OFF;
  }

  if (server.hasArg("status")) {
    String mode = server.arg("status");
    mode.toLowerCase();
    if (mode == "busy") statusLedMode = STATUS_BUSY;
    else if (mode == "error") statusLedMode = STATUS_ERROR;
    else if (mode == "on") statusLedMode = STATUS_ON;
    else if (mode == "off") statusLedMode = STATUS_OFF;
    else statusLedMode = STATUS_HEARTBEAT;
  }

  if (server.hasArg("siren")) {
    String val = server.arg("siren");
    val.toLowerCase();
    sirenActive = (val == "on" || val == "true" || val == "1");
  }

  server.send(200, "application/json", "{\"success\":true,\"siren_active\":" + String(sirenActive ? "true" : "false") + "}");
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
 * 10. SETUP & MAIN LOOP
 * ==================================================================== */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==============================================");
  Serial.println("  ESP32 GSM CELLULAR CALL, SMS & ALERT GATEWAY");
  Serial.println("==============================================");

  // Initialize Hardware GPIO Pin Modes
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(ALERT_LED_PIN, OUTPUT);
  pinMode(SIREN_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  digitalWrite(ALERT_LED_PIN, LOW);
  digitalWrite(SIREN_PIN, SIREN_ACTIVE_LOW ? HIGH : LOW);

  // Initialize Hardware Serial for GSM Module
  gsmSerial.begin(GSM_BAUD_RATE, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  Serial.printf("[GSM] Hardware Serial initialized on RX: %d, TX: %d @ %d baud\n", GSM_RX_PIN, GSM_TX_PIN, GSM_BAUD_RATE);

  addLog("ESP32 GSM Alert Gateway Initialized.");

  // Modem Auto-Baud Synchronization
  Serial.println("[GSM] Synchronizing baud rate with GSM modem...");
  for (int i = 0; i < 6; i++) {
    gsmSerial.print("AT\r\n");
    delay(250);
    while (gsmSerial.available()) gsmSerial.read();
  }
  gsmSerial.print("AT+IPR=9600\r\n");
  delay(150);
  gsmSerial.print("ATE0\r\n");
  delay(150);
  gsmSerial.print("AT+CMGF=1\r\n");
  delay(150);

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Connecting to '%s'", WIFI_SSID);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN)); // Flash fast while connecting
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

  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("[mDNS] Responding at: http://%s.local\n", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  // Register REST API Route Handlers
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/data", HTTP_GET, handleStatus);
  server.on("/api/call", HTTP_GET, handleCall);
  server.on("/api/call", HTTP_POST, handleCall);
  server.on("/api/hangup", HTTP_GET, handleHangup);
  server.on("/api/hangup", HTTP_POST, handleHangup);
  server.on("/api/send-sms", HTTP_GET, handleSendSms);
  server.on("/api/send-sms", HTTP_POST, handleSendSms);
  server.on("/api/alert", HTTP_GET, handleAlert);
  server.on("/api/alert", HTTP_POST, handleAlert);
  server.on("/api/led", HTTP_GET, handleLed);
  server.on("/api/led", HTTP_POST, handleLed);
  server.on("/api/at", HTTP_GET, handleCustomAt);
  server.on("/api/at", HTTP_POST, handleCustomAt);

  setupWebOTA();
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[HTTP] Web server listening on port 80 with Power-On & 1-Time Dispatch Logic.");

  setupArduinoOTA();

  queryGsmStatus();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  updateHardwareOutputs();

  unsigned long now = millis();

  // Poll Production Level 1 Cloud Server Endpoint (per a.md Section 3)
  if (now - lastCloudPollTime >= CLOUD_POLL_INTERVAL_MS) {
    lastCloudPollTime = now;
    pollLevel1CloudServer();
  }

  // Modem Periodic Check
  if (now - lastStatusCheck >= STATUS_CHECK_INTERVAL_MS) {
    lastStatusCheck = now;
    queryGsmStatus();
  }

  // Async UART responses from GSM Modem
  while (gsmSerial.available()) {
    String asyncLine = gsmSerial.readStringUntil('\n');
    asyncLine.trim();
    if (asyncLine.length() > 0) {
      addLog("< [ASYNC] " + asyncLine);
      if (asyncLine.indexOf("RING") >= 0) {
        activeCallState = "INCOMING CALL RINGING...";
        alertLedMode = ALERT_BLINK;
      } else if (asyncLine.indexOf("NO CARRIER") >= 0) {
        activeCallState = "IDLE (CALL DISCONNECTED)";
        if (!isAlertActive) alertLedMode = ALERT_OFF;
      } else if (asyncLine.indexOf("BUSY") >= 0) {
        activeCallState = "IDLE (LINE BUSY)";
        if (!isAlertActive) alertLedMode = ALERT_OFF;
      }
    }
  }
}
