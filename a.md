# HYDRA — Hardware & Firmware Integration Guide
**Autonomous Multi-Node Disaster Monitoring & Warning Network**  
*Target Environment: Embedded Microcontrollers (ESP32 / ESP8266 / Arduino / SIM800L / LoRa / Raspberry Pi)*  
*Server Base URL (Production):* `https://zenithkandel.com.np/hydra`  
*Server Base URL (Local Development):* `http://localhost/codes/hydra` or `http://<YOUR_LOCAL_IP>/codes/hydra`

---

## Quick Architecture Summary

The HYDRA disaster mitigation network is organized into a 3-tier hierarchy:
```
┌────────────────────────────────────────────────────────────────────────┐
│                        LEVEL 0 SENSOR STATIONS                         │
│  [Node 01: Flood HC-SR04]  [Node 02: Fire DHT11+MQ135]  [Node 03: Landslide MPU6050+GPS]
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ HTTP POST (Telemetry Ingest)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                      HYDRA CLOUD / EDGE SERVER                         │
│              https://zenithkandel.com.np/hydra/backend/api             │
│        • Ingests Sensor Readings     • Evaluates Live Thresholds       │
│        • Synthesizes GSM Alerts      • Controls Level 1 & 2 Relays     │
└───────────────────┬────────────────────────────────┬───────────────────┘
                    │                                │
                    │ HTTP GET (Dispatch Signals)    │ HTTP GET (Full Telemetry Payload)
                    ▼                                ▼
┌──────────────────────────────────────┐ ┌───────────────────────────────┐
│     LEVEL 1: VILLAGE MASTER NODE     │ │    LEVEL 2: CITY URBAN HUB    │
│  • High-Decibel Siren / Alarm Relay  │ │  • Pokhara Regional PEOC Kiosk│
│  • Multi-Color Disaster Status LEDs  │ │  • Full Telemetry Mirror      │
│  • SIM800L Cellular GSM SMS Modem    │ │  • Search & Rescue Dispatch   │
└──────────────────────────────────────┘ └───────────────────────────────┘
```

---

## 1. Production API Endpoints Directory

| Node Role | Action | Method | Production Endpoint URL |
| :--- | :--- | :---: | :--- |
| **Node 01: Flood** | Ingest Telemetry | `POST` | `https://zenithkandel.com.np/hydra/backend/api/telemetry/flood.php` |
| **Node 02: Fire** | Ingest Telemetry | `POST` | `https://zenithkandel.com.np/hydra/backend/api/telemetry/fire.php` |
| **Node 03: Landslide** | Ingest Telemetry | `POST` | `https://zenithkandel.com.np/hydra/backend/api/telemetry/landslide.php` |
| **Level 1: Village Node** | Fetch Signals | `GET` | `https://zenithkandel.com.np/hydra/backend/api/nodes/level1.php` |
| **Level 2: City Node** | Fetch Full Feed | `GET` | `https://zenithkandel.com.np/hydra/backend/api/nodes/level2.php` |
| **Consolidated Feed** | All Nodes JSON | `GET` | `https://zenithkandel.com.np/hydra/backend/api/telemetry/feed.php` |

---

## 2. Level 0 Sensor Nodes — Ingesting Data (HTTP POST)

All sensor stations make an **HTTP POST** request with header `Content-Type: application/json`.

### A. Node 01: Flood & Water Level Node (Ultrasonic HC-SR04)
- **URL**: `https://zenithkandel.com.np/hydra/backend/api/telemetry/flood.php`
- **Method**: `POST`
- **Cadence**: Recommended once every 1 to 3 seconds.

#### JSON Request Body:
```json
{
  "node_uid": "NODE-FLOOD-01",
  "dist_cm": 195.0,
  "pulse_us": 11310,
  "rssi": -60,
  "ip_address": "192.168.1.112",
  "radar_zone": "SAFE CLEARANCE BUFFER",
  "interval_ms": 750
}
```

#### Field Specifications:
- `dist_cm` *(float, required)*: Distance measured between the HC-SR04 transducer face and the water meniscus in centimeters.
- `pulse_us` *(int, optional)*: Raw ultrasonic echo return time in microseconds (`pulse_us = dist_cm * 58`).
- `radar_zone` *(string, optional)*: Diagnostic string describing proximity.
- `rssi` *(int, optional)*: Wi-Fi or LoRa signal strength in dBm (e.g. `-60`).

---

### B. Node 02: Fire & Environmental Node (DHT11 + MQ-135)
- **URL**: `https://zenithkandel.com.np/hydra/backend/api/telemetry/fire.php`
- **Method**: `POST`
- **Cadence**: Recommended once every 2 seconds.

#### JSON Request Body:
```json
{
  "node_uid": "NODE-FIRE-01",
  "temperature_c": 25.2,
  "temperature_f": 77.36,
  "humidity": 58.9,
  "gas_ppm": 385.0,
  "gas_raw_adc": 190,
  "air_quality_status": "Good (Normal)",
  "rssi": -65
}
```

#### Field Specifications:
- `temperature_c` *(float, required)*: Ambient temperature in degrees Celsius from DHT11.
- `humidity` *(float, required)*: Relative humidity percentage (`0.0` - `100.0%`).
- `gas_ppm` *(float, required)*: Calculated CO₂/Smoke/Combustible Gas concentration in Parts Per Million (PPM).
- `gas_raw_adc` *(int, optional)*: Raw 10-bit or 12-bit ADC integer reading from analog pin AO.
- `air_quality_status` *(string, optional)*: E.g., `"Good (Normal)"`, `"Moderate Risk"`, or `"Hazardous Smoke"`.

---

### C. Node 03: Landslide Node (MPU-6050 6-DOF + NEO-6M GPS)
- **URL**: `https://zenithkandel.com.np/hydra/backend/api/telemetry/landslide.php`
- **Method**: `POST`
- **Cadence**: Recommended once every 1 to 2 seconds.

#### JSON Request Body:
```json
{
  "node_uid": "NODE-LANDSLIDE-01",
  "gps": {
    "connected": true,
    "hw_alive": true,
    "hw_status": "ALIVE & STREAMING",
    "fix": true,
    "fix_stage": "3D FIX LOCKED",
    "satellites": 9,
    "lat": 27.6831654,
    "lng": 85.3165593,
    "alt_m": 1980.0,
    "speed_kmh": 0.0,
    "course_deg": 0.0,
    "cardinal": "N"
  },
  "mpu": {
    "connected": true,
    "pitch": 1.1,
    "roll": -0.6,
    "total_accel_g": 1.00,
    "ax": 0.0,
    "ay": 0.0,
    "az": 9.8,
    "gx": 0.0,
    "gy": 0.0,
    "gz": 0.0
  },
  "rssi": -65
}
```

*(Note: The API also accepts a flat format if nested objects are inconvenient on memory-constrained microcontrollers, e.g., `{"pitch": 1.1, "roll": -0.6, "latitude": 27.6831654, "satellites": 9}`).*

---

## 3. Level 1 Node (Village Master Node) — Dispatch Engine (HTTP GET)

The **Level 1 Village Master Node** micro-controller acts as the local community alarm center. It continuously polls the server to receive siren commands, status LED color indications, and SMS messages to be sent via its onboard GSM modem.

- **Endpoint URL**: `https://zenithkandel.com.np/hydra/backend/api/nodes/level1.php`
- **Method**: `GET`
- **Polling Cadence**: Every **1.0 to 1.5 seconds**.

### Response Structure & Logic Rules

#### Case 1: When an Emergency is Active (Server triggers Siren, LEDs & GSM)
```json
{
  "status": "SUCCESS",
  "code": 200,
  "data": {
    "status": "SUCCESS",
    "node": "LEVEL_1_VILLAGE_MASTER",
    "timestamp": "2026-09-04T01:05:00+05:45",
    "siren": "ON",
    "status_led": {
      "red": true,
      "green": false,
      "yellow": true,
      "active_colors": ["RED", "YELLOW"],
      "led_code": "RED+YELLOW"
    },
    "gsm_message": "[HYDRA DISASTER ALERT] Emergency at Ghandruk Station (27.6831°N, 85.3165°E): FLOOD & FIRE! Details: Flood depth 315cm, Fire gas 850ppm. Evacuate immediately!",
    "gsm_receiver_number": "+9779801234567",
    "emergency_type": ["FLOOD", "FIRE"],
    "active_emergencies": ["FLOOD", "FIRE"],
    "emergency": true,
    "breach_summary": "FLOOD, FIRE",
    "breach_details": { ... }
  }
}
```

#### Case 2: When System is Nominal (No Emergency)
```json
{
  "status": "SUCCESS",
  "code": 200,
  "data": {
    "status": "SUCCESS",
    "node": "LEVEL_1_VILLAGE_MASTER",
    "timestamp": "2026-09-04T01:05:00+05:45",
    "siren": "OFF",
    "status_led": {
      "red": false,
      "green": false,
      "yellow": false,
      "active_colors": [],
      "led_code": "OFF"
    },
    "gsm_message": null,
    "gsm_receiver_number": null,
    "emergency_type": null,
    "active_emergencies": [],
    "emergency": false,
    "breach_summary": null,
    "breach_details": []
  }
}
```

### Hardware Execution Rules for Level 1 Firmware:

1. **Siren Control (Relay / Buzzer Pin)**:
   - If `siren == "ON"`, set the Siren Relay Pin to `HIGH` (keep siren running continuously).
   - If `siren == "OFF"`, set the Siren Relay Pin to `LOW`.

2. **Status LEDs (Color Indicators)**:
   - `status_led.red`: Corresponds to **FIRE**. If `true`, turn Red LED `ON`; else `OFF`.
   - `status_led.green`: Corresponds to **LANDSLIDE**. If `true`, turn Green LED `ON`; else `OFF`.
   - `status_led.yellow`: Corresponds to **FLOOD**. If `true`, turn Yellow LED `ON`; else `OFF`.
   - *Multiple LEDs can be illuminated simultaneously* if multiple hazards occur at once!

3. **GSM Cellular SMS Dispatch (SIM800L / A6 / EC20)**:
   - Check if `gsm_message != null` and `gsm_receiver_number != null`.
   - Maintain a simple debounce or "last sent message" hash so the microcontroller sends the SMS **once per incident change**, rather than repeatedly spamming the cellular network on every poll.
   - If `gsm_message == null`, no SMS should be sent.

---

## 4. Level 2 Node (City Urban Hub Gateway) — Telemetry Feed (HTTP GET)

The **Level 2 City Node** micro-controller or Raspberry Pi / Kiosk requests the full aggregated payload to display telemetry on a dashboard or TFT screen.

- **Endpoint URL**: `https://zenithkandel.com.np/hydra/backend/api/nodes/level2.php`
- **Method**: `GET`
- **Polling Cadence**: Every **2.0 to 5.0 seconds**.

### Response Structure Overview:
```json
{
  "status": "SUCCESS",
  "code": 200,
  "data": {
    "network": "HYDRA-DISASTER-COMMAND-NETWORK",
    "station": "LEVEL_2_URBAN_RECEIVER_GATEWAY",
    "city_hub": "Pokhara Regional Emergency Operations Center (PEOC)",
    "system_status": "NOMINAL_MONITORING",
    "emergency": false,
    "emergency_types": [],
    "siren": "OFF",
    "village_node_status": {
      "siren": "OFF",
      "status_led": { "red": false, "green": false, "yellow": false },
      "link_status": "ACTIVE_UPLINK"
    },
    "nodes_telemetry": {
      "level_0_flood": {
        "hardware_distance": 195.0,
        "water_depth_cm": 315.0,
        "radar_zone": "SAFE CLEARANCE BUFFER"
      },
      "level_0_fire": {
        "temperature_c": 25.2,
        "humidity_pct": 58.9,
        "gas_ppm": 385.0,
        "air_quality_status": "Good (Normal)"
      },
      "level_0_landslide": {
        "gps": {
          "latitude": 27.6831654,
          "longitude": 85.3165593,
          "altitude_m": 1980.0,
          "satellites": 9,
          "speed_kmh": 0.0
        },
        "mpu_imu": {
          "pitch_deg": 1.1,
          "roll_deg": -0.6,
          "tilt_deg": 1.25,
          "total_accel_g": 1.00
        }
      }
    },
    "recommended_sar_teams": [ ... ]
  }
}
```

---

## 5. Ready-to-Flash C++ Code Examples (ESP32 / Arduino)

### Example 1: ESP32 Level 1 Node (Village Master)
Handles HTTP GET polling, Siren Relay, 3 Color LEDs, and SIM800L GSM alert dispatching:

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Wi-Fi Credentials
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// API Endpoint
const char* API_LEVEL1 = "https://zenithkandel.com.np/hydra/backend/api/nodes/level1.php";

// Hardware Pin Assignments
#define PIN_SIREN_RELAY  25  // Active HIGH relay for high-decibel siren
#define PIN_LED_RED      18  // Fire indicator
#define PIN_LED_GREEN    19  // Landslide indicator
#define PIN_LED_YELLOW   21  // Flood indicator

// Hardware Serial 2 for SIM800L GSM Modem
#define GSM_RX_PIN 16
#define GSM_TX_PIN 17
HardwareSerial gsmSerial(2);

// SMS Tracking
String lastSentMessage = "";

void sendSMS(const String& phoneNumber, const String& message) {
  Serial.println("[GSM] Sending SMS to " + phoneNumber);
  gsmSerial.println("AT+CMGF=1"); // Set SMS text mode
  delay(200);
  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(phoneNumber);
  gsmSerial.println("\"");
  delay(300);
  gsmSerial.print(message);
  delay(100);
  gsmSerial.write(26); // Ctrl+Z to send
  delay(3000);
  Serial.println("[GSM] SMS Dispatch Complete");
}

void setup() {
  Serial.begin(115200);
  gsmSerial.begin(9600, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);

  pinMode(PIN_SIREN_RELAY, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);

  // Default state: all off
  digitalWrite(PIN_SIREN_RELAY, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected! IP: " + WiFi.localIP().toString());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(API_LEVEL1);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(4096);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error && doc["status"] == "SUCCESS") {
        JsonObject data = doc["data"];

        // 1. Siren Control
        const char* sirenState = data["siren"];
        bool isSirenOn = (strcmp(sirenState, "ON") == 0);
        digitalWrite(PIN_SIREN_RELAY, isSirenOn ? HIGH : LOW);

        // 2. Status LEDs Control
        JsonObject leds = data["status_led"];
        digitalWrite(PIN_LED_RED,    leds["red"].as<bool>()    ? HIGH : LOW);
        digitalWrite(PIN_LED_GREEN,  leds["green"].as<bool>()  ? HIGH : LOW);
        digitalWrite(PIN_LED_YELLOW, leds["yellow"].as<bool>() ? HIGH : LOW);

        // 3. GSM Alert Dispatch
        if (!data["gsm_message"].isNull() && !data["gsm_receiver_number"].isNull()) {
          String msg = data["gsm_message"].as<String>();
          String phone = data["gsm_receiver_number"].as<String>();

          // Send only if the message content has changed
          if (msg != lastSentMessage) {
            sendSMS(phone, msg);
            lastSentMessage = msg;
          }
        } else {
          // Reset tracker when emergency clears
          lastSentMessage = "";
        }

        Serial.printf("[L1] Siren: %s | LEDs: [R:%d, G:%d, Y:%d]\n",
          sirenState,
          leds["red"].as<bool>(),
          leds["green"].as<bool>(),
          leds["yellow"].as<bool>()
        );
      }
    } else {
      Serial.printf("[HTTP] GET failed, error code: %d\n", httpCode);
    }
    http.end();
  }
  delay(1200); // Poll every 1.2 seconds
}
```

---

### Example 2: ESP32 Node 01 (Flood Sensor Post)
Reads HC-SR04 ultrasonic sensor and transmits depth to server:

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* API_FLOOD = "https://zenithkandel.com.np/hydra/backend/api/telemetry/flood.php";

#define TRIG_PIN 5
#define ECHO_PIN 18

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

float measureDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 35000); // 35ms timeout
  if (duration == 0) return -1.0;
  return (float)(duration * 0.0343 / 2.0);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    float distance = measureDistanceCm();
    if (distance > 2.0 && distance < 450.0) {
      HTTPClient http;
      http.begin(API_FLOOD);
      http.addHeader("Content-Type", "application/json");

      DynamicJsonDocument doc(512);
      doc["node_uid"] = "NODE-FLOOD-01";
      doc["dist_cm"] = distance;
      doc["pulse_us"] = (int)(distance * 58);
      doc["rssi"] = WiFi.RSSI();

      String jsonString;
      serializeJson(doc, jsonString);

      int httpResponseCode = http.POST(jsonString);
      Serial.printf("Transmitted Flood: %.1f cm (HTTP %d)\n", distance, httpResponseCode);
      http.end();
    }
  }
  delay(2000);
}
```

---

## 6. Secret Admin Calibration Console

To calibrate thresholds, test simulations, or change the GSM alert receiver number:
- **Secret URL**: `https://zenithkandel.com.np/hydra/admin/threshold`
- *(Or local URL: `http://localhost/codes/hydra/admin/threshold`)*
- Zero links lead to this page to maintain builder confidentiality.
- Changes made on this console persist directly to the database and update Level 1 & Level 2 hardware responses within 1 second.
