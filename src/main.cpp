/* ====================================================================
 * PLATFORMIO / ARDUINO ESP32 SENSOR TELEMETRY & DUAL OTA WEB SERVER
 * Microcontroller : ESP32 (Dev Module / WROOM-32)
 * Sensors         : NEO-6M GPS (Hardware Serial 2) + MPU-6050 6-DOF IMU (I2C)
 * Aesthetics      : Stark Monochrome (Pure Black & White, 0px Radius)
 * Data Integrity  : 100% Genuine Sensor Readings (Zero Fake / Mock Data)
 * OTA Features    : 1) Browser Web OTA at /update
 *                   2) Network ArduinoOTA for IDE & PlatformIO
 * ==================================================================== */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <TinyGPSPlus.h>

/* ====================================================================
 * 1. WI-FI & OTA CONFIGURATION
 * ==================================================================== */
// Enter your local Wi-Fi router credentials
const char* WIFI_SSID     = "sakshyam";
const char* WIFI_PASSWORD = "sakshyam";

// Fallback Access Point (AP) if router is out of range
const char* AP_SSID       = "ESP32-TELEMETRY";
const char* AP_PASSWORD   = "12345678"; // minimum 8 characters

// mDNS Hostname (Access at http://esp32-telemetry.local)
const char* MDNS_HOSTNAME = "esp32-telemetry";

// OTA Security Credentials (used for both Web OTA /update and ArduinoOTA)
const char* otaUsername   = "admin";
const char* otaPassword   = "admin";

/* ====================================================================
 * 2. HARDWARE PIN DEFINITIONS
 * ==================================================================== */
// NEO-6M GPS Module connected to Hardware Serial 2
#define GPS_RX_PIN 16 // ESP32 GPIO16 (RX2) -> Connect to GPS TX
#define GPS_TX_PIN 17 // ESP32 GPIO17 (TX2) -> Connect to GPS RX
#define GPS_BAUD   9600

// MPU-6050 IMU connected to Hardware I2C (Wire)
#define I2C_SDA_PIN 21 // ESP32 GPIO21 -> Connect to MPU6050 SDA
#define I2C_SCL_PIN 22 // ESP32 GPIO22 -> Connect to MPU6050 SCL

/* ====================================================================
 * 3. GLOBAL OBJECTS & STATE
 * ==================================================================== */
WebServer server(80);
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;
Adafruit_MPU6050 mpu;

bool mpuConnected = false;
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_INTERVAL_MS = 100; // Read IMU at 10 Hz

struct MPUData {
  float ax, ay, az;         // Acceleration in m/s^2
  float ax_g, ay_g, az_g;   // Acceleration in g (1g = 9.80665 m/s^2)
  float total_accel_g;      // Vector magnitude
  float gx, gy, gz;         // Gyro in deg/s
  float gx_rad, gy_rad, gz_rad; // Gyro in rad/s
  float temp_c;             // Temperature in °C
  float temp_f;             // Temperature in °F
  float pitch;              // Pitch angle (degrees)
  float roll;               // Roll angle (degrees)
} mpuData;

/* ====================================================================
 * 4. EMBEDDED SHARP MONOCHROME DASHBOARD HTML
 * ==================================================================== */
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 // TELEMETRY NODE</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; border-radius: 0 !important; }
    body { background-color: #000000; color: #FFFFFF; font-family: ui-monospace, "Cascadia Code", Menlo, Consolas, "Courier New", monospace; padding: 16px; line-height: 1.35; -webkit-font-smoothing: antialiased; }
    .container { max-width: 1200px; margin: 0 auto; }
    header { border: 1px solid #FFFFFF; padding: 16px; margin-bottom: 16px; background: #050505; display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 12px; }
    .title-group h1 { font-size: 18px; letter-spacing: 2px; font-weight: 900; text-transform: uppercase; }
    .title-group p { font-size: 11px; color: #888888; letter-spacing: 1px; margin-top: 2px; }
    .sys-badges { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    .badge { display: inline-flex; align-items: center; gap: 6px; padding: 4px 8px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; border: 1px solid #FFFFFF; text-decoration: none; }
    .badge.solid { background: #FFFFFF; color: #000000; }
    .badge.outline { background: #000000; color: #FFFFFF; }
    .badge.warn { border-style: dashed; color: #FFFFFF; background: #1A1A1A; }
    .badge.link { background: #000000; color: #FFFFFF; border-color: #FFFFFF; cursor: pointer; }
    .badge.link:hover { background: #FFFFFF; color: #000000; }
    .pulse { display: inline-block; width: 8px; height: 8px; background: #000000; animation: blink 1s steps(1) infinite; }
    .badge.outline .pulse { background: #FFFFFF; }
    @keyframes blink { 50% { opacity: 0; } }
    .section-title { font-size: 13px; letter-spacing: 2px; text-transform: uppercase; font-weight: 900; border-left: 4px solid #FFFFFF; padding-left: 8px; margin: 20px 0 10px 0; display: flex; justify-content: space-between; align-items: center; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 12px; margin-bottom: 16px; }
    .card { border: 1px solid #333333; background: #080808; padding: 14px; position: relative; }
    .card:hover { border-color: #FFFFFF; }
    .card-label { font-size: 10px; color: #777777; text-transform: uppercase; letter-spacing: 1.5px; font-weight: 700; margin-bottom: 6px; display: flex; justify-content: space-between; }
    .card-value { font-size: 24px; font-weight: 800; letter-spacing: 0.5px; color: #FFFFFF; word-break: break-all; }
    .card-sub { font-size: 11px; color: #888888; margin-top: 4px; font-weight: 500; }
    .table-container { border: 1px solid #333333; background: #080808; overflow-x: auto; margin-bottom: 16px; }
    table { width: 100%; border-collapse: collapse; font-size: 12px; text-align: left; }
    th, td { padding: 10px 14px; border-bottom: 1px solid #222222; border-right: 1px solid #222222; }
    th:last-child, td:last-child { border-right: none; }
    tr:last-child td { border-bottom: none; }
    th { background: #111111; color: #999999; font-size: 10px; text-transform: uppercase; letter-spacing: 1px; font-weight: 700; }
    td.num { font-weight: 700; color: #FFFFFF; font-size: 13px; }
    .gauge-wrapper { margin-top: 6px; height: 6px; background: #222222; position: relative; }
    .gauge-fill { height: 100%; background: #FFFFFF; width: 50%; transition: width 0.15s linear; }
    .btn { display: inline-block; padding: 8px 14px; font-size: 11px; font-weight: 800; text-transform: uppercase; letter-spacing: 1px; color: #000000; background: #FFFFFF; border: 1px solid #FFFFFF; text-decoration: none; cursor: pointer; margin-top: 8px; }
    .btn:hover { background: #000000; color: #FFFFFF; }
    .btn.disabled { background: #222222; color: #666666; border-color: #333333; pointer-events: none; }
    footer { border-top: 1px solid #333333; padding: 14px 0; font-size: 10px; color: #666666; display: flex; justify-content: space-between; align-items: center; letter-spacing: 1px; text-transform: uppercase; flex-wrap: wrap; gap: 8px; }
    footer a { color: #FFFFFF; text-decoration: none; border-bottom: 1px solid #FFFFFF; }
    footer a:hover { background: #FFFFFF; color: #000000; }
    .alert-box { border: 1px solid #FFFFFF; background: #000000; padding: 10px 14px; font-size: 11px; margin-bottom: 12px; display: none; }
    .alert-box.active { display: block; }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <div class="title-group">
        <h1>ESP32 // TELEMETRY MONITOR</h1>
        <p>HARDWARE SERIAL2 (NEO-6M) &bull; I2C BUS (MPU-6050) &bull; DUAL OTA ACTIVE</p>
      </div>
      <div class="sys-badges">
        <span id="conn-badge" class="badge solid"><span class="pulse"></span> LIVE COMMS</span>
        <span id="gps-lock-badge" class="badge warn">GPS: SEARCHING</span>
        <span id="mpu-status-badge" class="badge outline">MPU: DETECTING</span>
        <a href="/update" class="badge link">&#9889; WEB OTA REFLASH</a>
      </div>
    </header>

    <div id="error-banner" class="alert-box">
      [ALERT] <span id="error-msg">SYSTEM INITIALIZING...</span>
    </div>

    <!-- 1. GPS TELEMETRY -->
    <div class="section-title">
      <span>01 // NEO-6M GPS SATELLITE TELEMETRY</span>
      <span id="gps-updated-tag" style="font-size: 10px; color: #888;">WAITING FOR NMEA...</span>
    </div>

    <div class="grid">
      <div class="card">
        <div class="card-label"><span>COORDINATES (WGS84)</span><span>LAT / LON</span></div>
        <div class="card-value" id="gps-coords">--.------, --.------</div>
        <div class="card-sub" id="gps-coords-detail">NO ACTIVE POSITION FIX</div>
        <a id="maps-link" href="#" target="_blank" class="btn disabled">OPEN ON GOOGLE MAPS</a>
      </div>

      <div class="card">
        <div class="card-label"><span>GROUND SPEED</span><span>VELOCITY</span></div>
        <div class="card-value"><span id="gps-speed-kmh">--.-</span> <span style="font-size: 13px;">KM/H</span></div>
        <div class="card-sub"><span id="gps-speed-mph">--.-</span> MPH &bull; COURSE: <span id="gps-course">--&deg;</span> (<span id="gps-cardinal">--</span>)</div>
      </div>

      <div class="card">
        <div class="card-label"><span>ALTITUDE / ACCURACY</span><span>HDOP</span></div>
        <div class="card-value"><span id="gps-alt-m">--.-</span> <span style="font-size: 13px;">M</span></div>
        <div class="card-sub"><span id="gps-alt-ft">--.-</span> FT &bull; HDOP: <span id="gps-hdop">--.-</span> (LOWER IS BETTER)</div>
      </div>

      <div class="card">
        <div class="card-label"><span>CONSTELLATION STATS</span><span>TRACKING</span></div>
        <div class="card-value"><span id="gps-sats">0</span> <span style="font-size: 13px;">SATELLITES</span></div>
        <div class="card-sub">FIX AGE: <span id="gps-age">--</span> MS &bull; RX BYTES: <span id="gps-chars">0</span></div>
      </div>
    </div>

    <!-- GPS TIME & NMEA DIAGNOSTICS TABLE -->
    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>UTC DATE</th>
            <th>UTC TIME (ZULU)</th>
            <th>CHARS PROCESSED</th>
            <th>CHECKSUM FAILURES</th>
            <th>RAW SENTENCES WITH FIX</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td id="gps-date" class="num">----/--/--</td>
            <td id="gps-time" class="num">--:--:--</td>
            <td id="gps-chars-diag" class="num">0</td>
            <td id="gps-cs-fail" class="num">0</td>
            <td id="gps-fix-stat" class="num">NO FIX YET</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- 2. MPU-6050 IMU TELEMETRY -->
    <div class="section-title">
      <span>02 // MPU-6050 6-AXIS INERTIAL SENSOR</span>
      <span id="mpu-updated-tag" style="font-size: 10px; color: #888;">I2C 0x68</span>
    </div>

    <div class="grid">
      <div class="card">
        <div class="card-label"><span>ACCELEROMETER (TOTAL)</span><span>G-FORCE</span></div>
        <div class="card-value"><span id="mpu-accel-total">--.-</span> <span style="font-size: 13px;">G</span></div>
        <div class="card-sub">NORMAL GRAVITY BASELINE ~1.00 G</div>
      </div>

      <div class="card">
        <div class="card-label"><span>ATTITUDE (PITCH / ROLL)</span><span>INCLINATION</span></div>
        <div class="card-value"><span id="mpu-pitch">--.-</span>&deg; / <span id="mpu-roll">--.-</span>&deg;</div>
        <div class="card-sub">COMPUTED FROM REAL-TIME ACCELEROMETER VECTORS</div>
        <div class="gauge-wrapper"><div id="pitch-gauge" class="gauge-fill"></div></div>
      </div>

      <div class="card">
        <div class="card-label"><span>IMU DIE TEMPERATURE</span><span>ON-CHIP</span></div>
        <div class="card-value"><span id="mpu-temp-c">--.-</span> <span style="font-size: 13px;">&deg;C</span></div>
        <div class="card-sub"><span id="mpu-temp-f">--.-</span> &deg;F (INTERNAL SENSOR TEMP)</div>
      </div>
    </div>

    <!-- DETAILED 6-DOF AXIS BREAKDOWN TABLE -->
    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>AXIS</th>
            <th>ACCELERATION (m/s&sup2;)</th>
            <th>ACCELERATION (G)</th>
            <th>ANGULAR VELOCITY (&deg;/s)</th>
            <th>ANGULAR VELOCITY (rad/s)</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <th style="color:#FFF;">X-AXIS</th>
            <td id="mpu-ax-ms" class="num">--.---</td>
            <td id="mpu-ax-g" class="num">--.---</td>
            <td id="mpu-gx-deg" class="num">--.---</td>
            <td id="mpu-gx-rad" class="num">--.---</td>
          </tr>
          <tr>
            <th style="color:#FFF;">Y-AXIS</th>
            <td id="mpu-ay-ms" class="num">--.---</td>
            <td id="mpu-ay-g" class="num">--.---</td>
            <td id="mpu-gy-deg" class="num">--.---</td>
            <td id="mpu-gy-rad" class="num">--.---</td>
          </tr>
          <tr>
            <th style="color:#FFF;">Z-AXIS</th>
            <td id="mpu-az-ms" class="num">--.---</td>
            <td id="mpu-az-g" class="num">--.---</td>
            <td id="mpu-gz-deg" class="num">--.---</td>
            <td id="mpu-gz-rad" class="num">--.---</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- 3. SYSTEM & OTA DIAGNOSTICS -->
    <div class="table-container">
      <table>
        <thead>
          <tr>
            <th>IP ADDRESS</th>
            <th>WI-FI RSSI</th>
            <th>SYSTEM UPTIME</th>
            <th>FREE HEAP</th>
            <th>OTA SERVICE</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td id="sys-ip" class="num">---.---.---.---</td>
            <td id="sys-rssi" class="num">-- dBm</td>
            <td id="sys-uptime" class="num">00:00:00</td>
            <td id="sys-heap" class="num">-- KB</td>
            <td class="num"><a href="/update" style="color:#FFF; text-decoration: underline;">READY AT /update</a></td>
          </tr>
        </tbody>
      </table>
    </div>

    <footer>
      <span>ESP32 MONOCHROME REAL TELEMETRY NODE &bull; DUAL OTA (WEB &amp; ARDUINOOTA)</span>
      <span><a href="/update">[ FLASH FIRMWARE / OTA ]</a></span>
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
        document.getElementById('error-banner').className = "alert-box";

        document.getElementById('sys-ip').innerText = data.sys.ip;
        document.getElementById('sys-rssi').innerText = data.sys.rssi + ' dBm';
        document.getElementById('sys-uptime').innerText = formatUptime(data.sys.uptime_sec);
        document.getElementById('sys-heap').innerText = Math.round(data.sys.free_heap / 1024) + ' KB';

        // 1. GPS DATA (NO FAKE DATA)
        const gpsBadge = document.getElementById('gps-lock-badge');
        const mapsLink = document.getElementById('maps-link');

        document.getElementById('gps-chars').innerText = data.gps.chars_rx;
        document.getElementById('gps-chars-diag').innerText = data.gps.chars_rx;
        document.getElementById('gps-cs-fail').innerText = data.gps.checksum_fail;
        document.getElementById('gps-sats').innerText = data.gps.satellites;

        if (data.gps.connected === false) {
          gpsBadge.className = "badge warn";
          gpsBadge.innerText = "GPS: COMM ERROR (NO NMEA)";
          document.getElementById('gps-coords').innerText = "NO HARDWARE RX";
          document.getElementById('gps-coords-detail').innerText = "VERIFY PIN 16 (RX2) -> GPS TX WIRING";
        } else if (data.gps.fix === true) {
          gpsBadge.className = "badge solid";
          gpsBadge.innerText = "GPS: 3D FIX LOCKED (" + data.gps.satellites + " SATS)";
          
          const latStr = data.gps.lat.toFixed(6);
          const lngStr = data.gps.lng.toFixed(6);
          document.getElementById('gps-coords').innerText = latStr + ", " + lngStr;
          document.getElementById('gps-coords-detail').innerText = "PRECISION FIX &bull; LAT " + latStr + "&deg; | LNG " + lngStr + "&deg;";
          
          mapsLink.className = "btn";
          mapsLink.href = "https://www.google.com/maps?q=" + latStr + "," + lngStr;

          document.getElementById('gps-speed-kmh').innerText = data.gps.speed_kmh.toFixed(1);
          document.getElementById('gps-speed-mph').innerText = data.gps.speed_mph.toFixed(1);
          document.getElementById('gps-course').innerText = data.gps.course_deg.toFixed(1) + "°";
          document.getElementById('gps-cardinal').innerText = data.gps.cardinal || "--";
          document.getElementById('gps-alt-m').innerText = data.gps.alt_m.toFixed(1);
          document.getElementById('gps-alt-ft').innerText = data.gps.alt_ft.toFixed(1);
          document.getElementById('gps-hdop').innerText = data.gps.hdop.toFixed(2);
          document.getElementById('gps-date').innerText = data.gps.date;
          document.getElementById('gps-time').innerText = data.gps.time;
          document.getElementById('gps-fix-stat').innerText = "3D LOCK ACTIVE";
          document.getElementById('gps-age').innerText = data.gps.fix_age_ms;
          document.getElementById('gps-updated-tag').innerText = "NMEA VALID";
        } else {
          gpsBadge.className = "badge warn";
          gpsBadge.innerText = "GPS: SEARCHING (" + data.gps.satellites + " SATS)";
          document.getElementById('gps-coords').innerText = "--.------, --.------";
          document.getElementById('gps-coords-detail').innerText = "ACQUIRING SATELLITE LOCK (MOVE ANTENNA NEAR WINDOW/OUTDOORS)";
          
          mapsLink.className = "btn disabled";
          mapsLink.href = "#";

          document.getElementById('gps-speed-kmh').innerText = "--.-";
          document.getElementById('gps-speed-mph').innerText = "--.-";
          document.getElementById('gps-course').innerText = "--°";
          document.getElementById('gps-cardinal').innerText = "--";
          document.getElementById('gps-alt-m').innerText = "--.-";
          document.getElementById('gps-alt-ft').innerText = "--.-";
          document.getElementById('gps-hdop').innerText = data.gps.hdop > 0 ? data.gps.hdop.toFixed(2) : "--.-";
          document.getElementById('gps-date').innerText = data.gps.date || "----/--/--";
          document.getElementById('gps-time').innerText = data.gps.time || "--:--:--";
          document.getElementById('gps-fix-stat').innerText = "SEARCHING (0 FIX)";
          document.getElementById('gps-age').innerText = data.gps.fix_age_ms > 0 ? data.gps.fix_age_ms : "--";
          document.getElementById('gps-updated-tag').innerText = "WAITING FOR 3D FIX";
        }

        // 2. MPU-6050 DATA (NO FAKE DATA)
        const mpuBadge = document.getElementById('mpu-status-badge');
        if (data.mpu.connected === true) {
          mpuBadge.className = "badge solid";
          mpuBadge.innerText = "MPU6050: ONLINE";
          document.getElementById('mpu-updated-tag').innerText = "I2C OK (0x68)";

          document.getElementById('mpu-accel-total').innerText = data.mpu.total_accel_g.toFixed(2);
          document.getElementById('mpu-pitch').innerText = (data.mpu.pitch >= 0 ? "+" : "") + data.mpu.pitch.toFixed(1);
          document.getElementById('mpu-roll').innerText = (data.mpu.roll >= 0 ? "+" : "") + data.mpu.roll.toFixed(1);
          
          const clampedPitch = Math.max(-90, Math.min(90, data.mpu.pitch));
          const gaugePct = ((clampedPitch + 90) / 180) * 100;
          document.getElementById('pitch-gauge').style.width = gaugePct + "%";

          document.getElementById('mpu-temp-c').innerText = data.mpu.temp_c.toFixed(1);
          document.getElementById('mpu-temp-f').innerText = data.mpu.temp_f.toFixed(1);

          document.getElementById('mpu-ax-ms').innerText = (data.mpu.ax >= 0 ? "+" : "") + data.mpu.ax.toFixed(3);
          document.getElementById('mpu-ay-ms').innerText = (data.mpu.ay >= 0 ? "+" : "") + data.mpu.ay.toFixed(3);
          document.getElementById('mpu-az-ms').innerText = (data.mpu.az >= 0 ? "+" : "") + data.mpu.az.toFixed(3);

          document.getElementById('mpu-ax-g').innerText = (data.mpu.ax_g >= 0 ? "+" : "") + data.mpu.ax_g.toFixed(3);
          document.getElementById('mpu-ay-g').innerText = (data.mpu.ay_g >= 0 ? "+" : "") + data.mpu.ay_g.toFixed(3);
          document.getElementById('mpu-az-g').innerText = (data.mpu.az_g >= 0 ? "+" : "") + data.mpu.az_g.toFixed(3);

          document.getElementById('mpu-gx-deg').innerText = (data.mpu.gx >= 0 ? "+" : "") + data.mpu.gx.toFixed(2);
          document.getElementById('mpu-gy-deg').innerText = (data.mpu.gy >= 0 ? "+" : "") + data.mpu.gy.toFixed(2);
          document.getElementById('mpu-gz-deg').innerText = (data.mpu.gz >= 0 ? "+" : "") + data.mpu.gz.toFixed(2);

          document.getElementById('mpu-gx-rad').innerText = (data.mpu.gx_rad >= 0 ? "+" : "") + data.mpu.gx_rad.toFixed(3);
          document.getElementById('mpu-gy-rad').innerText = (data.mpu.gy_rad >= 0 ? "+" : "") + data.mpu.gy_rad.toFixed(3);
          document.getElementById('mpu-gz-rad').innerText = (data.mpu.gz_rad >= 0 ? "+" : "") + data.mpu.gz_rad.toFixed(3);
        } else {
          mpuBadge.className = "badge warn";
          mpuBadge.innerText = "MPU6050: DISCONNECTED";
          document.getElementById('mpu-updated-tag').innerText = "I2C ERROR";

          document.getElementById('mpu-accel-total').innerText = "--";
          document.getElementById('mpu-pitch').innerText = "--";
          document.getElementById('mpu-roll').innerText = "--";
          document.getElementById('mpu-temp-c').innerText = "--";
          document.getElementById('mpu-temp-f').innerText = "--";

          document.getElementById('mpu-ax-ms').innerText = "SENSOR DISCONNECTED";
          document.getElementById('mpu-ay-ms').innerText = "CHECK SDA (21)";
          document.getElementById('mpu-az-ms').innerText = "CHECK SCL (22)";
        }

      } catch (err) {
        failedFetches++;
        if (failedFetches > 2) {
          document.getElementById('conn-badge').className = "badge warn";
          document.getElementById('conn-badge').innerText = "OFFLINE [RETRYING...]";
          document.getElementById('error-banner').className = "alert-box active";
          document.getElementById('error-msg').innerText = "UNABLE TO REACH ESP32 TELEMETRY ENDPOINT (/api/data): " + err.message;
        }
      }
    }

    function formatUptime(totalSeconds) {
      const hrs = Math.floor(totalSeconds / 3600).toString().padStart(2, '0');
      const mins = Math.floor((totalSeconds % 3600) / 60).toString().padStart(2, '0');
      const secs = Math.floor(totalSeconds % 60).toString().padStart(2, '0');
      return hrs + ":" + mins + ":" + secs;
    }

    pollTelemetry();
    setInterval(pollTelemetry, 1000);
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
    <span class="badge">[ ESP32 OVER-THE-AIR FIRMWARE UPDATE ]</span>
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

/* ====================================================================
 * 6. HARDWARE SENSOR DRIVERS
 * ==================================================================== */

void initMPU6050() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);

  if (!mpu.begin(0x68, &Wire)) {
    Serial.println("[MPU6050] Address 0x68 not found. Checking 0x69...");
    if (!mpu.begin(0x69, &Wire)) {
      Serial.println("[MPU6050] Sensor not found on I2C bus! Check SDA/SCL wiring.");
      mpuConnected = false;
      return;
    }
  }

  mpuConnected = true;
  Serial.println("[MPU6050] Sensor online and initialized.");
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void readMPU6050() {
  if (!mpuConnected) {
    if (millis() % 5000 < 100) {
      initMPU6050();
    }
    return;
  }

  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    mpuConnected = false;
    return;
  }

  mpuData.ax = a.acceleration.x;
  mpuData.ay = a.acceleration.y;
  mpuData.az = a.acceleration.z;

  mpuData.ax_g = mpuData.ax / 9.80665;
  mpuData.ay_g = mpuData.ay / 9.80665;
  mpuData.az_g = mpuData.az / 9.80665;
  mpuData.total_accel_g = sqrt((mpuData.ax_g * mpuData.ax_g) + 
                               (mpuData.ay_g * mpuData.ay_g) + 
                               (mpuData.az_g * mpuData.az_g));

  mpuData.gx_rad = g.gyro.x;
  mpuData.gy_rad = g.gyro.y;
  mpuData.gz_rad = g.gyro.z;
  mpuData.gx = g.gyro.x * 57.2957795;
  mpuData.gy = g.gyro.y * 57.2957795;
  mpuData.gz = g.gyro.z * 57.2957795;

  mpuData.temp_c = temp.temperature;
  mpuData.temp_f = (temp.temperature * 1.8) + 32.0;

  mpuData.pitch = atan2(-mpuData.ax, sqrt(mpuData.ay * mpuData.ay + mpuData.az * mpuData.az)) * 57.2957795;
  mpuData.roll  = atan2(mpuData.ay, mpuData.az) * 57.2957795;
}

/* ====================================================================
 * 7. WEB SERVER HANDLERS & OTA CONTROLLER
 * ==================================================================== */

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  bool hasFix = gps.location.isValid() && (gps.location.age() < 5000);
  bool gpsConnected = (gps.charsProcessed() > 0);

  char dateBuf[16] = "----/--/--";
  char timeBuf[16] = "--:--:--";
  if (gps.date.isValid()) {
    snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d", 
             gps.date.year(), gps.date.month(), gps.date.day());
  }
  if (gps.time.isValid()) {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", 
             gps.time.hour(), gps.time.minute(), gps.time.second());
  }

  String json = "{";

  // System
  json += "\"sys\":{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptime_sec\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap());
  json += "},";

  // GPS
  json += "\"gps\":{";
  json += "\"connected\":" + String(gpsConnected ? "true" : "false") + ",";
  json += "\"fix\":" + String(hasFix ? "true" : "false") + ",";
  json += "\"satellites\":" + String(gps.satellites.isValid() ? gps.satellites.value() : 0) + ",";
  json += "\"hdop\":" + String(gps.hdop.isValid() ? gps.hdop.hdop() : 0.0, 2) + ",";
  json += "\"lat\":" + String(hasFix ? gps.location.lat() : 0.0, 6) + ",";
  json += "\"lng\":" + String(hasFix ? gps.location.lng() : 0.0, 6) + ",";
  json += "\"alt_m\":" + String(gps.altitude.isValid() ? gps.altitude.meters() : 0.0, 1) + ",";
  json += "\"alt_ft\":" + String(gps.altitude.isValid() ? gps.altitude.feet() : 0.0, 1) + ",";
  json += "\"speed_kmh\":" + String(gps.speed.isValid() ? gps.speed.kmph() : 0.0, 1) + ",";
  json += "\"speed_mph\":" + String(gps.speed.isValid() ? gps.speed.mph() : 0.0, 1) + ",";
  json += "\"course_deg\":" + String(gps.course.isValid() ? gps.course.deg() : 0.0, 1) + ",";
  json += "\"cardinal\":\"" + String(gps.course.isValid() ? TinyGPSPlus::cardinal(gps.course.deg()) : "--") + "\",";
  json += "\"date\":\"" + String(dateBuf) + "\",";
  json += "\"time\":\"" + String(timeBuf) + "\",";
  json += "\"fix_age_ms\":" + String(gps.location.isValid() ? gps.location.age() : 0) + ",";
  json += "\"chars_rx\":" + String(gps.charsProcessed()) + ",";
  json += "\"checksum_fail\":" + String(gps.failedChecksum());
  json += "},";

  // MPU-6050
  json += "\"mpu\":{";
  json += "\"connected\":" + String(mpuConnected ? "true" : "false") + ",";
  if (mpuConnected) {
    json += "\"ax\":" + String(mpuData.ax, 3) + ",";
    json += "\"ay\":" + String(mpuData.ay, 3) + ",";
    json += "\"az\":" + String(mpuData.az, 3) + ",";
    json += "\"ax_g\":" + String(mpuData.ax_g, 3) + ",";
    json += "\"ay_g\":" + String(mpuData.ay_g, 3) + ",";
    json += "\"az_g\":" + String(mpuData.az_g, 3) + ",";
    json += "\"total_accel_g\":" + String(mpuData.total_accel_g, 2) + ",";
    json += "\"gx\":" + String(mpuData.gx, 2) + ",";
    json += "\"gy\":" + String(mpuData.gy, 2) + ",";
    json += "\"gz\":" + String(mpuData.gz, 2) + ",";
    json += "\"gx_rad\":" + String(mpuData.gx_rad, 3) + ",";
    json += "\"gy_rad\":" + String(mpuData.gy_rad, 3) + ",";
    json += "\"gz_rad\":" + String(mpuData.gz_rad, 3) + ",";
    json += "\"temp_c\":" + String(mpuData.temp_c, 1) + ",";
    json += "\"temp_f\":" + String(mpuData.temp_f, 1) + ",";
    json += "\"pitch\":" + String(mpuData.pitch, 1) + ",";
    json += "\"roll\":" + String(mpuData.roll, 1);
  } else {
    json += "\"ax\":0,\"ay\":0,\"az\":0,\"ax_g\":0,\"ay_g\":0,\"az_g\":0,\"total_accel_g\":0,";
    json += "\"gx\":0,\"gy\":0,\"gz\":0,\"gx_rad\":0,\"gy_rad\":0,\"gz_rad\":0,";
    json += "\"temp_c\":0,\"temp_f\":0,\"pitch\":0,\"roll\":0";
  }
  json += "}";

  json += "}";

  server.send(200, "application/json", json);
}

void setupWebOTA() {
  // Serve the Web OTA Form (HTTP GET)
  server.on("/update", HTTP_GET, []() {
    if (strlen(otaUsername) > 0 && strlen(otaPassword) > 0) {
      if (!server.authenticate(otaUsername, otaPassword)) {
        return server.requestAuthentication();
      }
    }
    server.send_P(200, "text/html", OTA_INDEX_HTML);
  });

  // Handle Binary Upload (HTTP POST)
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
  Serial.println("[ArduinoOTA] Network background listener ready.");
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Route Not Found on ESP32 Telemetry Server");
}

/* ====================================================================
 * 8. SETUP & LOOP
 * ==================================================================== */

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==============================================");
  Serial.println("  ESP32 TELEMETRY SERVER (GPS + MPU-6050 + OTA)");
  Serial.println("==============================================");

  // Initialize Hardware Serial 2 for NEO-6M GPS (RX: 16, TX: 17)
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.printf("[GPS] Hardware Serial 2 started at %d baud (RX: GPIO%d, TX: GPIO%d)\n", 
                GPS_BAUD, GPS_RX_PIN, GPS_TX_PIN);

  // Initialize I2C and MPU-6050 IMU
  initMPU6050();

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
    Serial.println("[WIFI] Connection timed out. Starting SoftAP Fallback...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WIFI] SoftAP active! SSID: '%s' | Password: '%s'\n", AP_SSID, AP_PASSWORD);
    Serial.print("[WIFI] Access at: http://");
    Serial.println(WiFi.softAPIP());
  }

  // Setup mDNS
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("[mDNS] Responding at: http://%s.local\n", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  // Setup Web Server Routes & OTA Services
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, handleData);
  setupWebOTA();
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[HTTP] Web server started on port 80.");

  // Setup ArduinoOTA for PlatformIO Network Flashing
  setupArduinoOTA();
}

void loop() {
  // Handle HTTP requests and OTA background packets
  server.handleClient();
  ArduinoOTA.handle();

  // Feed GPS parser from Hardware Serial 2
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Read MPU-6050 at 10 Hz
  unsigned long now = millis();
  if (now - lastSensorReadTime >= SENSOR_INTERVAL_MS) {
    lastSensorReadTime = now;
    readMPU6050();
  }
}
