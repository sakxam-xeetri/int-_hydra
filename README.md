# ESP8266 Environmental Telemetry Node (DHT11 + MQ-135) with Live Web UI & OTA

This project turns an **ESP8266** (NodeMCU or Wemos D1 Mini) into a self-hosted environmental monitor that measures **Temperature, Humidity, Heat Index, and Air Quality (MQ-135 Gas / Smoke / CO2 estimation)**. It serves an auto-refreshing, modern dark-mode web dashboard and features **Dual OTA Updates** (Browser-based Web OTA and Arduino IDE Network OTA).

---

## 📌 Features

- **Real-Time Sensor Telemetry**:
  - **DHT11**: Temperature (°C & °F), Relative Humidity (%), and Heat Index (°C).
  - **MQ-135**: Raw ADC reading, Sensor Voltage, Temperature/Humidity compensated resistance, and estimated PPM air quality status.
- **Hosted Modern Web Dashboard**:
  - Clean, dark-mode glassmorphic UI.
  - Non-blocking asynchronous JSON polling every 2 seconds (`/data` endpoint).
  - Health/connection status indicator and live system stats (WiFi RSSI, IP, Uptime, Free Heap).
- **Dual OTA (Over-The-Air) Firmware Updates**:
  - **Web OTA**: Upload compiled `.bin` firmware directly via browser at `http://<esp-ip>/update`.
  - **ArduinoOTA**: Upload directly from Arduino IDE via Network Port.
- **mDNS Support**: Access using `http://esp8266-air.local` (on supported networks/devices like iOS, macOS, Windows with Bonjour/mDNS).

---

## 🔌 Wiring & Pin Connections

> ⚠️ **IMPORTANT FOR MQ-135**: The MQ-135 internal heater element requires **5V (VIN / VU pin)** to operate accurately. Powering MQ-135 with 3.3V will cause underheating and faulty/erratic air quality readings.

### ESP8266 (NodeMCU / Wemos D1 Mini) Pinout

| Sensor | Sensor Pin | ESP8266 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **DHT11** | VCC | **3.3V** (or 5V) | Power supply |
| **DHT11** | GND | **GND** | Ground |
| **DHT11** | DATA / OUT | **D2** (GPIO 4) | Data line (uses internal or module pull-up) |
| **MQ-135** | VCC | **VIN** (5V) | **Must be 5V** for the heater coil |
| **MQ-135** | GND | **GND** | Ground (Common Ground) |
| **MQ-135** | AOUT | **A0** (ADC0) | Analog output (0 - 3.3V on NodeMCU) |

*(Note: DOUT on MQ-135 is the digital comparator threshold pin and can be left unconnected).*

---

## 📦 Required Libraries

In the Arduino IDE, go to **Tools** > **Manage Libraries...** and install:

1. **DHT sensor library** by *Adafruit* (version 1.4.x or later)
2. **Adafruit Unified Sensor** by *Adafruit* (required dependency for DHT)

The following libraries are included with the ESP8266 core (no external installation required):
- `ESP8266WiFi.h`
- `ESP8266WebServer.h`
- `ESP8266mDNS.h`
- `ESP8266HTTPUpdateServer.h`
- `ArduinoOTA.h`

---

## ⚙️ Configuration & Uploading with PlatformIO IDE

### 1. Configure Wi-Fi Credentials
Open [`src/main.cpp`](file:///e:/Downloads/HYDRA/src/main.cpp) and update your Wi-Fi credentials:
```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### 2. Upload via USB Cable (First Time)
1. Plug your ESP8266 into your computer via USB.
2. In VS Code / PlatformIO IDE:
   - Click the **PlatformIO** alien icon on the left sidebar.
   - Under `nodemcuv2`, click **Upload** (or click the **`→` (Upload)** arrow button in the bottom status bar).
   - Or run in terminal:
     ```powershell
     pio run -t upload
     ```
3. Open the **Serial Monitor** (plug icon in status bar or `pio device monitor`) at **115200 baud** to see the assigned IP address.

### 3. Upload Wirelessly via OTA using PlatformIO
Once the firmware is flashed and connected to Wi-Fi, you can upload wirelessly from PlatformIO without plugging in USB:
1. In [`platformio.ini`](file:///e:/Downloads/HYDRA/platformio.ini), the `[env:nodemcuv2_ota]` environment is pre-configured with:
   ```ini
   upload_protocol = espota
   upload_port = esp8266-air.local   ; or replace with your ESP8266 IP address
   ```
2. In PlatformIO IDE, expand **nodemcuv2_ota** and click **Upload** (or run `pio run -e nodemcuv2_ota -t upload`).
3. You can also upload via your web browser by heading to `http://<ESP_IP>/update`.


---

## 🌐 Accessing the Live Dashboard

Once the ESP8266 connects to your Wi-Fi:
1. Open any web browser on your phone, laptop, or PC connected to the same network.
2. Navigate to:
   - `http://<ESP_IP_ADDRESS>` (printed in Serial Monitor)
   - Or: `http://esp8266-air.local`
3. The dashboard will automatically update every 2 seconds via live background JSON telemetry.

---

## 💨 MQ-135 Warm-Up & Calibration Note
- The MQ-135 has an internal tin dioxide ($SnO_2$) heating layer. When powered on for the first time, allow it to preheat for **24 to 48 hours** for full burn-in. For daily use, allow **1 to 3 minutes** for the reading to stabilize after boot.
- The firmware automatically applies a **temperature & humidity correction factor** to the MQ-135 reading based on the live DHT11 telemetry.
