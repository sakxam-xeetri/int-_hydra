#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <DHT.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

/* ====================================================================
 * CONFIGURATION & PIN DEFINITIONS
 * ==================================================================== */

// WiFi Credentials
const char* ssid     = "sakshyam";
const char* password = "sakshyam";

// HYDRA Production Ingest Endpoint for Fire Node (Node 02) per a.md Section 1 & 2.B
const char* SERVER_API_URL  = "https://zenithkandel.com.np/hydra/backend/api/telemetry/fire.php";
// Local Development / Raspberry Pi Fallback (uncomment to use local server)
// const char* SERVER_API_URL = "http://192.168.1.100/codes/hydra/backend/api/telemetry/fire.php";

const char* NODE_UID        = "NODE-FIRE-01";
const unsigned long TELEMETRY_SEND_INTERVAL_MS = 2000; // Cadence: every 2.0s per a.md

// Onboard LED Indicator Settings
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2 // GPIO 2 (D4 on NodeMCU / Wemos D1 Mini)
#endif
#define ONBOARD_LED LED_BUILTIN
// Note: ESP8266 onboard LED is active-LOW on NodeMCU and ESP-12 boards
#define LED_ON  LOW
#define LED_OFF HIGH

// Hostname for mDNS (Access at http://esp8266-air.local)
const char* hostName = "esp8266-air";

// Web OTA credentials (used at http://<ip>/update)
const char* otaUsername = "admin";
const char* otaPassword = "admin";

// DHT11 Sensor Settings
#define DHTPIN  D2       // GPIO4 on ESP8266
#define DHTTYPE DHT11    // DHT 11
DHT dht(DHTPIN, DHTTYPE);

// MQ-135 Gas Sensor Settings
#define MQ135_PIN A0     // ADC0 (Analog Pin on ESP8266)

// MQ-135 Calculation Parameters
// Standard load resistance (RL) is typically 10K to 20K on commercial break-out boards.
// Ro is the sensor resistance in clean fresh air.
// For initial estimation, Ro is calibrated to ~10.0k - 20.0k.
const float R_LOAD = 10.0;     // Load resistance in Kilo-Ohms
const float R0_CLEAN_AIR = 10.0; // Clean air baseline Ro in Kilo-Ohms (adjust after warm-up)

/* ====================================================================
 * GLOBAL VARIABLES & WEB SERVER
 * ==================================================================== */
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// Sensor reading cache
float temperatureC = 0.0;
float temperatureF = 0.0;
float humidity = 0.0;
float heatIndexC = 0.0;
int   gasRawADC = 0;
float gasVoltage = 0.0;
float gasPPM = 0.0;
String airQualityStatus = "Warming up...";

unsigned long lastSensorReadTime = 0;
const unsigned long sensorReadInterval = 2000; // Read sensors every 2 seconds

// Server Telemetry Uplink State
unsigned long lastServerSendMillis = 0;
int lastServerHttpCode = 0;
unsigned long lastServerDurationMs = 0;
unsigned long serverSuccessCount = 0;
unsigned long serverFailCount = 0;
String lastServerResponse = "WAITING FOR FIRST UPLINK";

/* ====================================================================
 * MQ-135 AIR QUALITY CALCULATION HELPERS
 * ==================================================================== */
// Corrected resistance calculation considering temperature and humidity
// (MQ-135 datasheet specifies sensitivity variation with T and RH)
float getMQ135CorrectionFactor(float t, float h) {
  // Approximate MQ-135 atmospheric correction factor curve
  return (0.00035 * t * t) - (0.02718 * t) + 1.395 - (0.0018 * (h - 33.0));
}

void readSensors() {
  // 1. Read DHT11
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h) && !isnan(t)) {
    humidity = h;
    temperatureC = t;
    temperatureF = (t * 1.8) + 32.0;
    heatIndexC = dht.computeHeatIndex(t, h, false);
  }

  // 2. Read MQ-135
  // Over-sample for ADC stability
  long adcSum = 0;
  for (int i = 0; i < 10; i++) {
    adcSum += analogRead(MQ135_PIN);
    delay(5);
  }
  gasRawADC = adcSum / 10;

  // NodeMCU A0 divider maps 0-3.3V to 0-1023 (or 0-1.0V for bare ESP-12)
  // Assuming standard NodeMCU/Wemos D1 with built-in divider:
  gasVoltage = ((float)gasRawADC / 1023.0) * 3.3;

  // Sensor resistance (Rs) calculation:
  // VRL = gasVoltage. If VRL <= 0, prevent division by zero.
  if (gasVoltage > 0.05 && gasVoltage < 3.25) {
    float rs = ((3.3 - gasVoltage) / gasVoltage) * R_LOAD;
    
    // Apply DHT temperature & humidity compensation if valid
    float corrFactor = (!isnan(h) && !isnan(t)) ? getMQ135CorrectionFactor(t, h) : 1.0;
    float rsCompensated = rs / corrFactor;

    // Ratio Rs/Ro
    float ratio = rsCompensated / R0_CLEAN_AIR;

    // MQ-135 curve approximation for general air pollution (CO2/smoke/NH3):
    // PPM = A * (ratio)^B (typical MQ-135 general curve: A ≈ 116.6, B ≈ -2.76)
    if (ratio > 0.1) {
      gasPPM = 116.6020682 * pow(ratio, -2.769034857);
    } else {
      gasPPM = 9999.0;
    }
  } else if (gasVoltage <= 0.05) {
    gasPPM = 0.0;
  }

  // Determine Air Quality Status
  if (millis() < 60000) { // MQ-135 requires initial pre-heat time
    airQualityStatus = "Sensor Preheating (1-3 min)";
  } else if (gasRawADC < 200 || gasPPM < 450) {
    airQualityStatus = "Excellent / Fresh Air";
  } else if (gasRawADC < 400 || gasPPM < 800) {
    airQualityStatus = "Good (Normal Indoor)";
  } else if (gasRawADC < 600 || gasPPM < 1200) {
    airQualityStatus = "Moderate / Ventilate";
  } else if (gasRawADC < 800 || gasPPM < 1800) {
    airQualityStatus = "Poor (Stale / Gaseous)";
  } else {
    airQualityStatus = "Hazardous / High Contaminants!";
  }
}

/* ====================================================================
 * HTML / CSS / JS WEB INTERFACE (Stored in Flash Memory via PROGMEM)
 * ==================================================================== */
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Environmental Monitor</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #0b0f19;
      --card-bg: rgba(23, 32, 54, 0.7);
      --card-border: rgba(255, 255, 255, 0.08);
      --text-main: #f3f4f6;
      --text-sub: #9ca3af;
      --accent-temp: #f97316;
      --accent-hum: #06b6d4;
      --accent-gas: #10b981;
      --accent-ota: #8b5cf6;
      --glow-temp: rgba(249, 115, 22, 0.25);
      --glow-hum: rgba(6, 182, 212, 0.25);
      --glow-gas: rgba(16, 185, 129, 0.25);
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Outfit', -apple-system, BlinkMacSystemFont, sans-serif;
      background: radial-gradient(circle at top, #141e33 0%, var(--bg) 100%);
      color: var(--text-main);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 1.5rem 1rem 3rem;
    }
    header {
      width: 100%;
      max-width: 960px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 2rem;
      padding-bottom: 1rem;
      border-bottom: 1px solid var(--card-border);
    }
    .brand {
      display: flex;
      align-items: center;
      gap: 0.75rem;
    }
    .pulse-dot {
      width: 12px;
      height: 12px;
      background: #10b981;
      border-radius: 50%;
      box-shadow: 0 0 12px #10b981;
      animation: pulse 2s infinite ease-in-out;
    }
    @keyframes pulse {
      0%, 100% { transform: scale(1); opacity: 1; }
      50% { transform: scale(1.3); opacity: 0.7; }
    }
    h1 { font-size: 1.4rem; font-weight: 700; letter-spacing: -0.5px; }
    .subtitle { font-size: 0.82rem; color: var(--text-sub); }

    .header-actions {
      display: flex;
      gap: 0.6rem;
      align-items: center;
    }
    .ota-btn {
      background: linear-gradient(135deg, #7c3aed, #6366f1);
      color: #fff;
      text-decoration: none;
      font-size: 0.85rem;
      font-weight: 600;
      padding: 0.5rem 1rem;
      border-radius: 9999px;
      transition: all 0.2s ease;
      box-shadow: 0 4px 15px rgba(99, 102, 241, 0.3);
    }
    .ota-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(99, 102, 241, 0.45);
    }

    .dashboard-grid {
      width: 100%;
      max-width: 960px;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 1.25rem;
      margin-bottom: 2rem;
    }
    .card {
      background: var(--card-bg);
      backdrop-filter: blur(14px);
      -webkit-backdrop-filter: blur(14px);
      border: 1px solid var(--card-border);
      border-radius: 1.25rem;
      padding: 1.5rem;
      transition: transform 0.25s ease, border-color 0.25s ease, box-shadow 0.25s ease;
      position: relative;
      overflow: hidden;
    }
    .card:hover {
      transform: translateY(-4px);
      border-color: rgba(255, 255, 255, 0.15);
    }
    .card-temp { box-shadow: 0 8px 30px var(--glow-temp); }
    .card-hum  { box-shadow: 0 8px 30px var(--glow-hum); }
    .card-gas  { box-shadow: 0 8px 30px var(--glow-gas); }

    .card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 1rem;
    }
    .card-title {
      font-size: 0.85rem;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      color: var(--text-sub);
    }
    .card-icon {
      font-size: 1.4rem;
    }

    .card-value-wrap {
      display: flex;
      align-items: baseline;
      gap: 0.35rem;
      margin-bottom: 0.75rem;
    }
    .card-value {
      font-size: 2.8rem;
      font-weight: 700;
      line-height: 1;
      letter-spacing: -1px;
    }
    .card-unit {
      font-size: 1.2rem;
      color: var(--text-sub);
      font-weight: 500;
    }

    .meta-row {
      display: flex;
      justify-content: space-between;
      font-size: 0.8rem;
      color: var(--text-sub);
      padding-top: 0.75rem;
      border-top: 1px solid rgba(255, 255, 255, 0.05);
    }
    .badge {
      display: inline-block;
      padding: 0.25rem 0.65rem;
      border-radius: 9999px;
      font-size: 0.78rem;
      font-weight: 600;
      background: rgba(255, 255, 255, 0.08);
    }

    /* Gas Quality Badge Styling */
    .badge-excellent { background: rgba(16, 185, 129, 0.2); color: #34d399; }
    .badge-good      { background: rgba(59, 130, 246, 0.2); color: #60a5fa; }
    .badge-moderate  { background: rgba(245, 158, 11, 0.2); color: #fbbf24; }
    .badge-poor      { background: rgba(239, 68, 68, 0.2); color: #f87171; }
    .badge-hazard    { background: rgba(225, 29, 72, 0.3); color: #fb7185; animation: blink 1.2s infinite; }
    @keyframes blink { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }

    /* System Info Bar */
    .system-bar {
      width: 100%;
      max-width: 960px;
      background: rgba(15, 23, 42, 0.6);
      border: 1px solid var(--card-border);
      border-radius: 1rem;
      padding: 1rem 1.25rem;
      display: flex;
      flex-wrap: wrap;
      gap: 1.5rem;
      justify-content: space-around;
      font-size: 0.85rem;
      color: var(--text-sub);
    }
    .sys-item {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 0.25rem;
    }
    .sys-label { font-size: 0.72rem; text-transform: uppercase; letter-spacing: 0.04em; }
    .sys-val { font-weight: 600; color: var(--text-main); }

    footer {
      margin-top: 2rem;
      font-size: 0.75rem;
      color: #6b7280;
      text-align: center;
    }
  </style>
</head>
<body>

  <header>
    <div class="brand">
      <div class="pulse-dot" id="liveDot"></div>
      <div>
        <h1>ESP8266 Fire &amp; Environmental Node</h1>
        <p class="subtitle">Real-time Telemetry &bull; DHT11 &bull; MQ-135</p>
      </div>
    </div>
    <div class="header-actions">
      <span id="server-badge" class="ota-btn" style="background: rgba(255,255,255,0.1); border: 1px solid var(--card-border); pointer-events: none;">CLOUD: STANDBY</span>
      <a href="/update" class="ota-btn">&#9889; Web OTA</a>
    </div>
  </header>

  <div class="dashboard-grid">
    <!-- Temperature Card -->
    <div class="card card-temp">
      <div class="card-header">
        <span class="card-title">Temperature (DHT11)</span>
        <span class="card-icon">&#127777;&#65039;</span>
      </div>
      <div class="card-value-wrap">
        <span class="card-value" id="valTemp">--</span>
        <span class="card-unit">&deg;C</span>
      </div>
      <div class="meta-row">
        <span>Fahrenheit: <strong id="valTempF">-- &deg;F</strong></span>
        <span>Heat Index: <strong id="valHeatIndex">-- &deg;C</strong></span>
      </div>
    </div>

    <!-- Humidity Card -->
    <div class="card card-hum">
      <div class="card-header">
        <span class="card-title">Humidity (DHT11)</span>
        <span class="card-icon">&#128167;</span>
      </div>
      <div class="card-value-wrap">
        <span class="card-value" id="valHum">--</span>
        <span class="card-unit">% RH</span>
      </div>
      <div class="meta-row">
        <span>Comfort Level:</span>
        <span class="badge" id="badgeHum">Measuring...</span>
      </div>
    </div>

    <!-- Gas & Air Quality Card -->
    <div class="card card-gas">
      <div class="card-header">
        <span class="card-title">Air Quality (MQ-135)</span>
        <span class="card-icon">&#127788;&#65039;</span>
      </div>
      <div class="card-value-wrap">
        <span class="card-value" id="valGasPpm">--</span>
        <span class="card-unit">est. PPM</span>
      </div>
      <div class="meta-row">
        <span>Raw ADC: <strong id="valGasAdc">--</strong> (<span id="valGasVolt">--</span>V)</span>
        <span class="badge" id="badgeGas">Checking</span>
      </div>
    </div>
  </div>

  <!-- System Info Bar -->
  <div class="system-bar">
    <div class="sys-item">
      <span class="sys-label">Air Status</span>
      <span class="sys-val" id="sysAirStatus">Warming up</span>
    </div>
    <div class="sys-item">
      <span class="sys-label">WiFi Signal</span>
      <span class="sys-val" id="sysRssi">-- dBm</span>
    </div>
    <div class="sys-item">
      <span class="sys-label">ESP8266 IP</span>
      <span class="sys-val" id="sysIp">--</span>
    </div>
    <div class="sys-item">
      <span class="sys-label">Cloud Ingest</span>
      <span class="sys-val" id="sysServerStatus">STANDBY</span>
    </div>
    <div class="sys-item">
      <span class="sys-label">Server Latency</span>
      <span class="sys-val" id="sysServerLatency">-- ms</span>
    </div>
    <div class="sys-item">
      <span class="sys-label">Sync Count</span>
      <span class="sys-val" id="sysServerCounts">0 / 0</span>
    </div>
    <div class="sys-item">
      <span class="sys-label">System Uptime</span>
      <span class="sys-val" id="sysUptime">--s</span>
    </div>
    <div class="sys-item">
      <span class="sys-label">Free Heap</span>
      <span class="sys-val" id="sysHeap">-- bytes</span>
    </div>
  </div>

  <footer>
    Hardware: ESP8266 (NodeMCU/Wemos) &bull; DHT11 &bull; MQ-135 &bull; ArduinoOTA &amp; WebOTA Active
  </footer>

  <script>
    async function updateTelemetry() {
      try {
        const res = await fetch('/data');
        if (!res.ok) throw new Error('HTTP error ' + res.status);
        const d = await res.json();

        // Temperature & Humidity
        document.getElementById('valTemp').innerText = d.temperatureC.toFixed(1);
        document.getElementById('valTempF').innerText = d.temperatureF.toFixed(1) + ' °F';
        document.getElementById('valHeatIndex').innerText = d.heatIndexC.toFixed(1) + ' °C';
        document.getElementById('valHum').innerText = d.humidity.toFixed(1);

        // Humidity Comfort Badge
        const humBadge = document.getElementById('badgeHum');
        if (d.humidity < 30) {
          humBadge.innerText = 'Dry';
          humBadge.style.color = '#f59e0b';
        } else if (d.humidity <= 60) {
          humBadge.innerText = 'Optimal';
          humBadge.style.color = '#10b981';
        } else {
          humBadge.innerText = 'High Humidity';
          humBadge.style.color = '#3b82f6';
        }

        // MQ-135 Gas
        document.getElementById('valGasPpm').innerText = Math.round(d.gasPPM);
        document.getElementById('valGasAdc').innerText = d.gasRawADC;
        document.getElementById('valGasVolt').innerText = d.gasVoltage.toFixed(2);

        // Air Status Badge
        const gasBadge = document.getElementById('badgeGas');
        gasBadge.innerText = d.airQualityStatus;
        gasBadge.className = 'badge';
        if (d.airQualityStatus.includes('Preheating')) {
          gasBadge.classList.add('badge-moderate');
        } else if (d.airQualityStatus.includes('Excellent')) {
          gasBadge.classList.add('badge-excellent');
        } else if (d.airQualityStatus.includes('Good')) {
          gasBadge.classList.add('badge-good');
        } else if (d.airQualityStatus.includes('Moderate')) {
          gasBadge.classList.add('badge-moderate');
        } else if (d.airQualityStatus.includes('Poor')) {
          gasBadge.classList.add('badge-poor');
        } else {
          gasBadge.classList.add('badge-hazard');
        }

        // System items
        document.getElementById('sysAirStatus').innerText = d.airQualityStatus;
        document.getElementById('sysRssi').innerText = d.rssi + ' dBm';
        document.getElementById('sysIp').innerText = d.ip;
        document.getElementById('sysUptime').innerText = formatUptime(d.uptimeSeconds);
        document.getElementById('sysHeap').innerText = d.freeHeap + ' B';

        // Server Ingest Telemetry
        if (d.server) {
          const srvBadge = document.getElementById('server-badge');
          document.getElementById('sysServerLatency').innerText = d.server.last_latency_ms + ' ms';
          document.getElementById('sysServerCounts').innerText = d.server.success_count + ' / ' + d.server.fail_count;

          if (d.server.last_code >= 200 && d.server.last_code < 300) {
            srvBadge.innerText = 'CLOUD: 200 OK';
            srvBadge.style.background = 'linear-gradient(135deg, #059669, #10b981)';
            document.getElementById('sysServerStatus').innerText = 'HTTP ' + d.server.last_code + ' OK';
            document.getElementById('sysServerStatus').style.color = '#10b981';
          } else if (d.server.last_code > 0) {
            srvBadge.innerText = 'CLOUD: HTTP ' + d.server.last_code;
            srvBadge.style.background = 'linear-gradient(135deg, #d97706, #f59e0b)';
            document.getElementById('sysServerStatus').innerText = 'HTTP ' + d.server.last_code;
            document.getElementById('sysServerStatus').style.color = '#f59e0b';
          } else {
            srvBadge.innerText = 'CLOUD: STANDBY';
            srvBadge.style.background = 'rgba(255,255,255,0.1)';
            document.getElementById('sysServerStatus').innerText = 'CONNECTING...';
            document.getElementById('sysServerStatus').style.color = '#9ca3af';
          }
        }

        // Keep live pulse active
        document.getElementById('liveDot').style.background = '#10b981';
      } catch (err) {
        console.error('Fetch error:', err);
        document.getElementById('liveDot').style.background = '#ef4444';
      }
    }

    function formatUptime(sec) {
      const h = Math.floor(sec / 3600);
      const m = Math.floor((sec % 3600) / 60);
      const s = sec % 60;
      if (h > 0) return `${h}h ${m}m ${s}s`;
      if (m > 0) return `${m}m ${s}s`;
      return `${s}s`;
    }

    // Initial call and periodic poll every 2 seconds
    updateTelemetry();
    setInterval(updateTelemetry, 2000);
  </script>
</body>
</html>
)rawliteral";

/* ====================================================================
 * HYDRA CLOUD SERVER TELEMETRY INGEST (HTTP/HTTPS POST)
 * ==================================================================== */
void sendTelemetryToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  // Build JSON Payload matching a.md Section 2.B (Node 02 Fire Telemetry)
  String payload = "{";
  payload += "\"node_uid\":\"" + String(NODE_UID) + "\",";
  payload += "\"temperature_c\":" + String(temperatureC, 2) + ",";
  payload += "\"temperature_f\":" + String(temperatureF, 2) + ",";
  payload += "\"humidity\":" + String(humidity, 2) + ",";
  payload += "\"gas_ppm\":" + String(gasPPM, 2) + ",";
  payload += "\"gas_raw_adc\":" + String(gasRawADC) + ",";
  payload += "\"air_quality_status\":\"" + airQualityStatus + "\",";
  payload += "\"rssi\":" + String(WiFi.RSSI());
  payload += "}";

  WiFiClientSecure secureClient;
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(3000);
  unsigned long startT = millis();
  bool isHttps = String(SERVER_API_URL).startsWith("https://");
  int httpCode = 0;

  if (isHttps) {
    secureClient.setInsecure(); // Skip certificate verification for embedded client
    secureClient.setTimeout(3000);
    if (http.begin(secureClient, SERVER_API_URL)) {
      http.addHeader("Content-Type", "application/json");
      httpCode = http.POST(payload);
      if (httpCode > 0) {
        lastServerResponse = http.getString();
      } else {
        lastServerResponse = http.errorToString(httpCode);
      }
      http.end();
    }
  } else {
    client.setTimeout(3000);
    if (http.begin(client, SERVER_API_URL)) {
      http.addHeader("Content-Type", "application/json");
      httpCode = http.POST(payload);
      if (httpCode > 0) {
        lastServerResponse = http.getString();
      } else {
        lastServerResponse = http.errorToString(httpCode);
      }
      http.end();
    }
  }

  lastServerDurationMs = millis() - startT;
  lastServerHttpCode = httpCode;

  if (httpCode >= 200 && httpCode < 300) {
    serverSuccessCount++;
    Serial.printf("[SERVER] Fire Telemetry Ingest SUCCESS (HTTP %d, %lums): %s\n",
                  httpCode, lastServerDurationMs, lastServerResponse.c_str());
  } else if (httpCode > 0) {
    serverFailCount++;
    Serial.printf("[SERVER] Fire Telemetry Ingest WARN (HTTP %d, %lums): %s\n",
                  httpCode, lastServerDurationMs, lastServerResponse.c_str());
  } else {
    serverFailCount++;
    Serial.printf("[SERVER] Fire Telemetry Ingest FAILED: %s (code %d, %lums)\n",
                  http.errorToString(httpCode).c_str(), httpCode, lastServerDurationMs);
  }
}

/* ====================================================================
 * WEB SERVER HANDLERS
 * ==================================================================== */

// Serves the HTML Dashboard
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// Serves the live JSON telemetry data
void handleData() {
  String json = "{";
  json += "\"temperatureC\":" + String(temperatureC, 2) + ",";
  json += "\"temperatureF\":" + String(temperatureF, 2) + ",";
  json += "\"humidity\":" + String(humidity, 2) + ",";
  json += "\"heatIndexC\":" + String(heatIndexC, 2) + ",";
  json += "\"gasRawADC\":" + String(gasRawADC) + ",";
  json += "\"gasVoltage\":" + String(gasVoltage, 3) + ",";
  json += "\"gasPPM\":" + String(gasPPM, 2) + ",";
  json += "\"airQualityStatus\":\"" + airQualityStatus + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"uptimeSeconds\":" + String(millis() / 1000) + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";

  // Server Uplink Status
  json += "\"server\":{";
  json += "\"url\":\"" + String(SERVER_API_URL) + "\",";
  json += "\"last_code\":" + String(lastServerHttpCode) + ",";
  json += "\"last_latency_ms\":" + String(lastServerDurationMs) + ",";
  json += "\"success_count\":" + String(serverSuccessCount) + ",";
  json += "\"fail_count\":" + String(serverFailCount) + ",";
  json += "\"last_send_ago_ms\":" + String(lastServerSendMillis > 0 ? (millis() - lastServerSendMillis) : 999999) + ",";
  String cleanResp = lastServerResponse;
  cleanResp.replace("\"", "'");
  cleanResp.replace("\n", " ");
  cleanResp.replace("\r", " ");
  json += "\"last_response\":\"" + cleanResp + "\"";
  json += "}";

  json += "}";

  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found on ESP8266");
}

/* ====================================================================
 * SETUP FUNCTION
 * ==================================================================== */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n====================================");
  Serial.println("  ESP8266 Sensor Node Starting...   ");
  Serial.println("====================================");

  // Initialize DHT11
  dht.begin();
  pinMode(MQ135_PIN, INPUT);

  // Initialize Onboard LED (blinks during connection, glows solid when connected)
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, LED_OFF);

  // Connect to WiFi
  Serial.printf("Connecting to %s ", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempt = 0;
  bool blinkState = false;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) {
    delay(500);
    blinkState = !blinkState;
    digitalWrite(ONBOARD_LED, blinkState ? LED_ON : LED_OFF);
    Serial.print(".");
    attempt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(ONBOARD_LED, LED_ON); // Solid ON when Wi-Fi is connected
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    digitalWrite(ONBOARD_LED, LED_OFF);
    Serial.println("\nWiFi Failed to connect. Starting in Fallback Mode.");
  }

  // Set up mDNS responder
  if (MDNS.begin(hostName)) {
    Serial.printf("mDNS responder started: http://%s.local\n", hostName);
    MDNS.addService("http", "tcp", 80);
  }

  // 1. Setup Web OTA updater (/update endpoint)
  httpUpdater.setup(&server, "/update", otaUsername, otaPassword);
  Serial.println("Web OTA active at: http://<IP>/update");

  // 2. Setup ArduinoOTA (for IDE direct wireless flashing)
  ArduinoOTA.setHostname(hostName);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd of OTA Update");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("ArduinoOTA ready for IDE flashing.");

  // Configure Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/send_now", HTTP_GET, []() {
    sendTelemetryToServer();
    server.send(200, "application/json", "{\"status\":\"TRIGGERED\",\"http_code\":" + String(lastServerHttpCode) + "}");
  });
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started!");
  
  // Initial sensor read
  readSensors();
}

/* ====================================================================
 * MAIN LOOP
 * ==================================================================== */
void loop() {
  // Onboard LED Wi-Fi Status Indicator: Glows SOLID when Wi-Fi is connected
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(ONBOARD_LED, LED_ON); // Solid ON when Wi-Fi is connected (active-LOW)
  } else {
    digitalWrite(ONBOARD_LED, (millis() % 1000 < 150) ? LED_ON : LED_OFF); // Short pulse if disconnected
  }

  // Handle OTA routines
  ArduinoOTA.handle();

  // Handle mDNS queries
  MDNS.update();

  // Handle incoming HTTP client requests
  server.handleClient();

  // Non-blocking sensor update
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorReadTime >= sensorReadInterval) {
    lastSensorReadTime = currentMillis;
    readSensors();
  }

  // Periodic Telemetry Transmission to HYDRA Server API (every 2.0s per a.md)
  if (currentMillis - lastServerSendMillis >= TELEMETRY_SEND_INTERVAL_MS) {
    lastServerSendMillis = currentMillis;
    sendTelemetryToServer();
  }
}
