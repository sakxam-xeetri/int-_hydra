/* ====================================================================
 * ESP32-S3 SUPER MINI - ULTRASONIC RADAR & PROXIMITY BLINK SENSOR
 * Hardware : ESP32-S3 Super Mini + HC-SR04 Ultrasonic Sensor + Alert LED
 * Wi-Fi    : SSID "sakshyam" | Password "sakshyam"
 * UI Theme : Stark High-Contrast Monochrome (Black & White, 0px Radius)
 * Feature  : Proximity Radar (Fast Blink = Near, Slow Blink = Far)
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
const char* AP_SSID       = "ESP32S3-DISTANCE";
const char* AP_PASSWORD   = "12345678";

// mDNS Hostname (Access via http://esp32s3-distance.local)
const char* MDNS_HOSTNAME = "esp32s3-distance";

// OTA Credentials (used at /update)
const char* otaUsername   = "admin";
const char* otaPassword   = "admin";

/* ====================================================================
 * 2. PIN DEFINITIONS (ESP32-S3 SUPER MINI)
 * ==================================================================== */
// Ultrasonic Sensor (HC-SR04 / HC-SR04P / JSN-SR04T)
#define TRIG_PIN         4  // GPIO 4 -> Ultrasonic TRIGGER pin
#define ECHO_PIN         5  // GPIO 5 -> Ultrasonic ECHO pin

// LED Indicators
#define HIGH_DIST_LED    7  // GPIO 7 -> External Indicator LED (Anode via 220Ω resistor)
#define ONBOARD_LED      8  // GPIO 8 -> On-Board Blue LED on ESP32-S3 Super Mini

/* ====================================================================
 * 3. GLOBAL VARIABLES & STATE
 * ==================================================================== */
WebServer server(80);

// Sensor readings
float currentDistanceCm = 0.0;
float currentDistanceIn = 0.0;
float currentDistanceM  = 0.0;
unsigned long pulseDurationUs = 0;
bool isSensorValid = false;

// Dynamic Proximity Blink State
unsigned long currentBlinkIntervalMs = 1000;
unsigned long lastBlinkToggleTime = 0;
bool currentLedState = false;
String proximityZone = "OUT OF RANGE";

unsigned long lastMeasureTime = 0;
const unsigned long MEASURE_INTERVAL_MS = 60; // Measure distance at ~16 Hz for responsive radar

/* ====================================================================
 * 4. EMBEDDED SHARP MONOCHROME WEB DASHBOARD HTML
 * ==================================================================== */
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 // PROXIMITY RADAR</title>
  <style>
    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      border-radius: 0 !important; /* STRICT ZERO-RADIUS SHARP EDGES */
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

    /* BIG HERO DISTANCE CARD */
    .hero-card {
      border: 2px solid #FFFFFF;
      background: #080808;
      padding: 24px;
      margin-bottom: 16px;
    }

    .hero-label {
      font-size: 11px;
      color: #888888;
      text-transform: uppercase;
      letter-spacing: 2px;
      font-weight: 800;
      display: flex;
      justify-content: space-between;
      margin-bottom: 12px;
    }

    .hero-value-wrap {
      display: flex;
      align-items: baseline;
      gap: 12px;
      flex-wrap: wrap;
    }

    .hero-value {
      font-size: 68px;
      font-weight: 900;
      letter-spacing: -1px;
      line-height: 1;
      color: #FFFFFF;
    }

    .hero-unit {
      font-size: 24px;
      font-weight: 700;
      letter-spacing: 1px;
      color: #AAAAAA;
    }

    .hero-sub {
      margin-top: 14px;
      font-size: 13px;
      color: #999999;
      letter-spacing: 1px;
      display: flex;
      flex-wrap: wrap;
      gap: 16px;
    }

    .hero-sub span {
      font-weight: 700;
      color: #FFFFFF;
    }

    /* RADAR BLINK MONITOR CARD */
    .radar-banner {
      border: 2px solid #FFFFFF;
      padding: 16px 20px;
      margin-bottom: 16px;
      background: #0D0D0D;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 16px;
      align-items: center;
    }

    .radar-zone-title {
      font-size: 10px;
      color: #888888;
      text-transform: uppercase;
      letter-spacing: 1.5px;
      margin-bottom: 4px;
    }

    .radar-zone-val {
      font-size: 20px;
      font-weight: 900;
      letter-spacing: 1px;
      text-transform: uppercase;
    }

    .led-hardware-box {
      display: flex;
      align-items: center;
      justify-content: space-between;
      border: 1px solid #333333;
      padding: 10px 14px;
      background: #000000;
    }

    .led-strobe-indicator {
      display: inline-block;
      width: 14px;
      height: 14px;
      background: #222222;
      border: 1px solid #FFFFFF;
      transition: background 0.05s ease;
    }

    .led-strobe-indicator.lit {
      background: #FFFFFF;
      box-shadow: 0 0 10px #FFFFFF;
    }

    /* RANGE GAUGE BAR */
    .bar-container {
      margin: 16px 0 6px 0;
      position: relative;
    }

    .bar-track {
      height: 16px;
      background: #111111;
      border: 1px solid #444444;
      position: relative;
      overflow: hidden;
    }

    .bar-fill {
      height: 100%;
      background: #FFFFFF;
      width: 0%;
      transition: width 0.08s linear;
    }

    .bar-scale {
      display: flex;
      justify-content: space-between;
      font-size: 10px;
      color: #666666;
      margin-top: 4px;
      letter-spacing: 1px;
    }

    /* EXPLANATION MATRIX */
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
        <h1>ESP32-S3 // PROXIMITY RADAR</h1>
        <p>SUPER MINI BOARD &bull; PROPORTIONAL BLINK (FAST = NEAR, SLOW = FAR)</p>
      </div>
      <div class="badges">
        <span id="conn-badge" class="badge solid"><span class="pulse"></span> LIVE COMMS</span>
        <span id="zone-badge" class="badge outline">RADAR: ACTIVE</span>
        <a href="/update" class="badge link">&#9889; WEB OTA</a>
      </div>
    </header>

    <!-- SECTION 1: LIVE DISTANCE TELEMETRY -->
    <div class="section-title">
      <span>01 // REAL-TIME DISTANCE MEASUREMENT</span>
      <span style="font-size: 10px; color: #888;">SAMPLING @ 16 HZ</span>
    </div>

    <!-- HERO DISTANCE DISPLAY -->
    <div class="hero-card">
      <div class="hero-label">
        <span>MEASURED DISTANCE (HC-SR04)</span>
        <span id="sensor-health">STATUS: ECHO VALID</span>
      </div>
      
      <div class="hero-value-wrap">
        <div class="hero-value" id="dist-cm">--.-</div>
        <div class="hero-unit">CENTIMETERS</div>
      </div>

      <!-- VISUAL RANGE BAR GAUGE (0 - 300 CM) -->
      <div class="bar-container">
        <div class="bar-track">
          <div id="range-fill" class="bar-fill"></div>
        </div>
        <div class="bar-scale">
          <span style="color:#FFF;">0 CM (FAST BLINK)</span>
          <span>150 CM (MEDIUM BLINK)</span>
          <span>300 CM (SLOW BLINK)</span>
        </div>
      </div>

      <div class="hero-sub">
        <div>INCHES: <span id="dist-in">--.-</span> IN</div>
        <div>METERS: <span id="dist-m">--.--</span> M</div>
        <div>TIME OF FLIGHT: <span id="pulse-us">--</span> &mu;S</div>
      </div>
    </div>

    <!-- PROXIMITY RADAR & LED BLINK MONITOR -->
    <div class="radar-banner">
      <div>
        <div class="radar-zone-title">01. PROXIMITY RADAR ZONE:</div>
        <div id="radar-zone-text" class="radar-zone-val">DETECTING...</div>
        <div id="radar-sub-desc" style="font-size: 11px; color: #888; margin-top: 4px;">
          PROPORTIONAL PULSE FREQUENCY
        </div>
      </div>

      <div class="led-hardware-box">
        <div>
          <div style="font-size: 10px; color: #888; text-transform: uppercase;">PHYSICAL LED (GPIO 7 &amp; 8):</div>
          <div id="blink-freq-text" style="font-size: 14px; font-weight: 800; color: #FFF; margin-top: 2px;">
            INTERVAL: -- MS
          </div>
        </div>
        <div style="display: flex; align-items: center; gap: 8px;">
          <span id="led-text-state" style="font-size: 11px; font-weight: 800;">SYNC</span>
          <div id="web-led-indicator" class="led-strobe-indicator"></div>
        </div>
      </div>
    </div>

    <!-- SECTION 2: BLINK RATE MAPPING TABLE -->
    <div class="section-title">
      <span>02 // PROXIMITY BLINK FREQUENCY MAPPING</span>
      <span>AUTOMATIC HARDWARE CONTROL</span>
    </div>

    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>DISTANCE RANGE</th>
            <th>PROXIMITY STATUS</th>
            <th>BLINK FREQUENCY</th>
            <th>INTERVAL</th>
            <th>PURPOSE</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td class="num" style="color: #FFF;">&lt; 20 CM</td>
            <td style="font-weight: 700; color: #FFF;">CRITICAL PROXIMITY</td>
            <td class="num">ULTRA FAST (10 - 20 Hz)</td>
            <td class="num">50 - 100 ms</td>
            <td>Immediate stop warning</td>
          </tr>
          <tr>
            <td class="num">20 CM - 60 CM</td>
            <td style="font-weight: 700;">CLOSE RANGE</td>
            <td class="num">FAST BLINK (5 - 8 Hz)</td>
            <td class="num">120 - 250 ms</td>
            <td>Approaching obstacle</td>
          </tr>
          <tr>
            <td class="num">60 CM - 150 CM</td>
            <td style="font-weight: 700;">MID RANGE</td>
            <td class="num">MODERATE (2 - 3 Hz)</td>
            <td class="num">300 - 650 ms</td>
            <td>Normal detection field</td>
          </tr>
          <tr>
            <td class="num">&gt; 150 CM</td>
            <td style="font-weight: 700;">FAR RANGE</td>
            <td class="num">SLOW BLINK (&lt; 1 Hz)</td>
            <td class="num">800 - 1200 ms</td>
            <td>Distant obstacle detected</td>
          </tr>
          <tr>
            <td class="num" style="color: #666;">OUT OF RANGE</td>
            <td style="color: #666;">NO OBSTACLE</td>
            <td class="num" style="color: #666;">OFF</td>
            <td class="num" style="color: #666;">--</td>
            <td>Standby / Idle mode</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- SECTION 3: SYSTEM DIAGNOSTICS -->
    <div class="section-title">
      <span>03 // SYSTEM DIAGNOSTICS</span>
      <span>ESP32-S3 SUPER MINI</span>
    </div>

    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>IP ADDRESS</th>
            <th>WI-FI RSSI</th>
            <th>SYSTEM UPTIME</th>
            <th>FREE MEMORY</th>
            <th>WIRING PINOUT</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td id="sys-ip" class="num">---.---.---.---</td>
            <td id="sys-rssi" class="num">-- dBm</td>
            <td id="sys-uptime" class="num">00:00:00</td>
            <td id="sys-heap" class="num">-- KB</td>
            <td class="num">TRIG: 4 &bull; ECHO: 5 &bull; LED: 7 &amp; 8</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- FOOTER -->
    <footer>
      <span>ESP32-S3 SUPER MINI &bull; PROPORTIONAL RADAR BLINK</span>
      <span><a href="/update">[ OVER-THE-AIR FIRMWARE UPDATE ]</a></span>
    </footer>

  </div>

  <script>
    let failedFetches = 0;

    async function pollTelemetry() {
      try {
        const response = await fetch('/api/data');
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        failedFetches = 0;

        document.getElementById('conn-badge').className = "badge solid";
        document.getElementById('conn-badge').innerHTML = '<span class="pulse"></span> LIVE COMMS';

        // 1. Distance Telemetry
        if (data.sensor.valid) {
          document.getElementById('dist-cm').innerText = data.sensor.dist_cm.toFixed(1);
          document.getElementById('dist-in').innerText = data.sensor.dist_in.toFixed(1);
          document.getElementById('dist-m').innerText = data.sensor.dist_m.toFixed(2);
          document.getElementById('pulse-us').innerText = data.sensor.pulse_us;
          document.getElementById('sensor-health').innerText = "STATUS: ECHO VALID";
          document.getElementById('sensor-health').style.color = "#FFFFFF";

          // Range bar gauge (0 to 300 cm)
          const pct = Math.min(100, Math.max(0, (data.sensor.dist_cm / 300.0) * 100));
          document.getElementById('range-fill').style.width = pct + '%';
        } else {
          document.getElementById('dist-cm').innerText = "--.-";
          document.getElementById('dist-in').innerText = "--.-";
          document.getElementById('dist-m').innerText = "--.--";
          document.getElementById('pulse-us').innerText = "--";
          document.getElementById('sensor-health').innerText = "STATUS: OUT OF RANGE / NO ECHO";
          document.getElementById('sensor-health').style.color = "#888888";
          document.getElementById('range-fill').style.width = '0%';
        }

        // 2. Proximity Zone & Radar Status
        document.getElementById('radar-zone-text').innerText = data.radar.zone;
        document.getElementById('zone-badge').innerText = "ZONE: " + data.radar.zone;

        if (data.sensor.valid) {
          document.getElementById('blink-freq-text').innerText = "BLINK RATE: " + data.radar.interval_ms + " MS (" + data.radar.rate_desc + ")";
          document.getElementById('radar-sub-desc').innerText = "DISTANCE " + data.sensor.dist_cm.toFixed(1) + " CM &rarr; " + data.radar.rate_desc + " PULSE";
        } else {
          document.getElementById('blink-freq-text').innerText = "BLINK RATE: STANDBY (OFF)";
          document.getElementById('radar-sub-desc').innerText = "NO OBSTACLE DETECTED WITHIN 400 CM";
        }

        // Mirror hardware LED in browser
        const webLed = document.getElementById('web-led-indicator');
        const ledText = document.getElementById('led-text-state');
        if (data.radar.led_state) {
          webLed.className = "led-strobe-indicator lit";
          ledText.innerText = "HIGH";
        } else {
          webLed.className = "led-strobe-indicator";
          ledText.innerText = "LOW";
        }

        // 3. System Diagnostics
        document.getElementById('sys-ip').innerText = data.sys.ip;
        document.getElementById('sys-rssi').innerText = data.sys.rssi + " dBm";
        document.getElementById('sys-uptime').innerText = formatUptime(data.sys.uptime_sec);
        document.getElementById('sys-heap').innerText = Math.round(data.sys.free_heap / 1024) + " KB";

      } catch (err) {
        failedFetches++;
        if (failedFetches > 3) {
          document.getElementById('conn-badge').className = "badge warn";
          document.getElementById('conn-badge').innerText = "OFFLINE [RETRYING...]";
        }
      }
    }

    function formatUptime(totalSecs) {
      const h = Math.floor(totalSecs / 3600).toString().padStart(2, '0');
      const m = Math.floor((totalSecs % 3600) / 60).toString().padStart(2, '0');
      const s = Math.floor(totalSecs % 60).toString().padStart(2, '0');
      return h + ":" + m + ":" + s;
    }

    pollTelemetry();
    setInterval(pollTelemetry, 150); // Fast 150ms polling to track live blink state
  </script>
</body>
</html>
)rawliteral";

/* ====================================================================
 * 5. EMBEDDED SHARP MONOCHROME WEB OTA HTML
 * ==================================================================== */
const char OTA_INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 // FIRMWARE OTA</title>
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
    <span class="badge">[ ESP32-S3 FIRMWARE RECOVERY ]</span>
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
          document.getElementById('status').innerText = 'FLASH COMPLETE! REBOOTING ESP32-S3 NODE...';
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

/* ====================================================================
 * 6. ULTRASONIC SENSOR MEASUREMENT & DYNAMIC PROXIMITY BLINK
 * ==================================================================== */

void readUltrasonicSensor() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  pulseDurationUs = pulseIn(ECHO_PIN, HIGH, 25000);

  if (pulseDurationUs > 0) {
    float dist = ((float)pulseDurationUs * 0.0343) / 2.0;

    if (dist >= 2.0 && dist <= 400.0) {
      currentDistanceCm = dist;
      currentDistanceIn = dist / 2.54;
      currentDistanceM  = dist / 100.0;
      isSensorValid     = true;

      // Dynamic Proportional Blink Calculation:
      // Near (4cm) -> 50ms interval (Very Fast ~10-20 Hz)
      // Far  (250cm) -> 1000ms interval (Slow ~0.5-1 Hz)
      float clamped = constrain(currentDistanceCm, 4.0, 250.0);
      currentBlinkIntervalMs = (unsigned long)map((long)(clamped * 10), 40, 2500, 50, 1000);

      // Categorize Zone for Diagnostics
      if (currentDistanceCm < 20.0) {
        proximityZone = "CRITICAL PROXIMITY (FASTEST BLINK)";
      } else if (currentDistanceCm < 60.0) {
        proximityZone = "CLOSE RANGE (FAST BLINK)";
      } else if (currentDistanceCm < 150.0) {
        proximityZone = "MID RANGE (MODERATE BLINK)";
      } else {
        proximityZone = "FAR RANGE (SLOW BLINK)";
      }
    } else {
      isSensorValid = false;
      proximityZone = "OUT OF RANGE (> 400 CM)";
    }
  } else {
    isSensorValid = false;
    proximityZone = "NO ECHO / STANDBY";
  }
}

void updateProximityLedBlink() {
  if (!isSensorValid) {
    if (currentLedState) {
      currentLedState = false;
      digitalWrite(HIGH_DIST_LED, LOW);
      digitalWrite(ONBOARD_LED, LOW);
    }
    return;
  }

  // Non-blocking proportional blinking
  unsigned long now = millis();
  if (now - lastBlinkToggleTime >= currentBlinkIntervalMs) {
    lastBlinkToggleTime = now;
    currentLedState = !currentLedState;
    digitalWrite(HIGH_DIST_LED, currentLedState ? HIGH : LOW);
    digitalWrite(ONBOARD_LED, currentLedState ? HIGH : LOW);
  }
}

/* ====================================================================
 * 7. WEB SERVER HANDLERS & OTA
 * ==================================================================== */

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  String rateDesc;
  if (!isSensorValid) {
    rateDesc = "OFF";
  } else if (currentBlinkIntervalMs <= 100) {
    rateDesc = "VERY FAST";
  } else if (currentBlinkIntervalMs <= 250) {
    rateDesc = "FAST";
  } else if (currentBlinkIntervalMs <= 600) {
    rateDesc = "MEDIUM";
  } else {
    rateDesc = "SLOW";
  }

  String json = "{";

  // System Diagnostics
  json += "\"sys\":{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptime_sec\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap());
  json += "},";

  // Ultrasonic Sensor Telemetry
  json += "\"sensor\":{";
  json += "\"valid\":" + String(isSensorValid ? "true" : "false") + ",";
  json += "\"dist_cm\":" + String(isSensorValid ? currentDistanceCm : 0.0, 1) + ",";
  json += "\"dist_in\":" + String(isSensorValid ? currentDistanceIn : 0.0, 1) + ",";
  json += "\"dist_m\":" + String(isSensorValid ? currentDistanceM : 0.0, 2) + ",";
  json += "\"pulse_us\":" + String(pulseDurationUs);
  json += "},";

  // Proximity Radar & LED Blink State
  json += "\"radar\":{";
  json += "\"zone\":\"" + proximityZone + "\",";
  json += "\"interval_ms\":" + String(isSensorValid ? currentBlinkIntervalMs : 0) + ",";
  json += "\"rate_desc\":\"" + rateDesc + "\",";
  json += "\"led_state\":" + String(currentLedState ? "true" : "false");
  json += "}";

  json += "}";

  server.send(200, "application/json", json);
}

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

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("[ArduinoOTA] Network flash initiated: " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[ArduinoOTA] Flash complete. Rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[ArduinoOTA] Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[ArduinoOTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found on ESP32-S3 Distance Server");
}

/* ====================================================================
 * 8. SETUP & LOOP
 * ==================================================================== */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==============================================");
  Serial.println("  ESP32-S3 SUPER MINI - PROXIMITY RADAR");
  Serial.println("==============================================");

  // Initialize GPIO Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(HIGH_DIST_LED, OUTPUT);
  pinMode(ONBOARD_LED, OUTPUT);

  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(HIGH_DIST_LED, LOW);
  digitalWrite(ONBOARD_LED, LOW);

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
    Serial.print("[WIFI] Connected! Assigned IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] Router connection timed out. Starting SoftAP Fallback...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WIFI] SoftAP active! SSID: '%s' | Password: '%s'\n", AP_SSID, AP_PASSWORD);
    Serial.print("[WIFI] Access dashboard at: http://");
    Serial.println(WiFi.softAPIP());
  }

  // Setup mDNS
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("[mDNS] Responding at: http://%s.local\n", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  // Setup HTTP Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, handleData);
  setupWebOTA();
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[HTTP] Web server listening on port 80.");

  setupArduinoOTA();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  // Periodically read ultrasonic distance
  unsigned long now = millis();
  if (now - lastMeasureTime >= MEASURE_INTERVAL_MS) {
    lastMeasureTime = now;
    readUltrasonicSensor();
  }

  // Continuously update LED blinking based on current distance
  updateProximityLedBlink();
}
