# HYDRA Multi-Node Telemetry & Parameter Specification

Comprehensive data dictionary and protocol documentation for the **HYDRA Early Warning & Disaster Monitoring Network**. This document defines every parameter transmitted by each sensor node across HTTP JSON endpoints, wireless networks, and serial telemetry.

---

## 📑 Node Architectural Overview

| Node Name | Firmware Source | Microcontroller | Primary Sensors | Monitored Hazard | Primary Endpoint | Default mDNS Host |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **🔥 Fire & Environmental Node** | [`ESP8266_Sensors_Dashboard.ino`](file:///e:/Downloads/HYDRA/ESP8266_Sensors_Dashboard.ino) / [`src/main.cpp`](file:///e:/Downloads/HYDRA/src/main.cpp) | ESP8266 (NodeMCU / Wemos D1 Mini) | DHT11 + MQ-135 | Wildfire, Smoke, High Temp, Air Quality | `GET /data` | `http://esp8266-air.local` |
| **🌊 Flood & Water Level Node** | [`ESP32S3_Ultrasonic_Dashboard.ino`](file:///e:/Downloads/HYDRA/ESP32S3_Ultrasonic_Dashboard.ino) | ESP32-S3 Super Mini | HC-SR04 Ultrasonic Transceiver | Flash Flood, River/Canal Rise, Proximity | `GET /api/data` | `http://esp32s3-distance.local` |
| **⛰️ Landslide & Seismic Node** | [`ESP32_GPS_MPU6050_Dashboard.ino`](file:///e:/Downloads/HYDRA/ESP32_GPS_MPU6050_Dashboard.ino) | ESP32 Dev Module (WROOM-32) | MPU-6050 6-DOF IMU + NEO-6M GPS | Slope Incline, Mudslide, Ground Tremor, GPS Shift | `GET /api/data` | `http://esp32-telemetry.local` |

---

## 1. 🔥 Fire & Environmental Monitoring Node

### 📌 Overview & Pin Configuration
The **Fire Node** monitors atmospheric heat, ambient relative humidity, and airborne combustion byproducts (smoke, CO₂, NH₃, alcohol, benzene) using an analog gas sensor and a digital thermo-hygrometer.

- **DHT11 Data Pin**: `D2` (GPIO 4)
- **MQ-135 Analog Out (AOUT)**: `A0` (ADC0) — *Powered via 5V VIN for internal heating element*

---

### 📡 Network Transmission Spec
- **Protocol**: HTTP / REST GET
- **Endpoint**: `/data`
- **Port**: `80`
- **Format**: JSON (`application/json`)
- **Refresh Frequency**: 2000 ms (2.0 seconds)

---

### 📊 Parameter Data Dictionary

| Parameter Key | Data Type | Units | Range / Value Set | Physical Meaning & Description |
| :--- | :--- | :--- | :--- | :--- |
| `temperatureC` | `float` | °C | `0.00` to `50.00` | Ambient temperature measured by DHT11 in degrees Celsius. |
| `temperatureF` | `float` | °F | `32.00` to `122.00` | Ambient temperature converted to degrees Fahrenheit (`(T * 1.8) + 32`). |
| `humidity` | `float` | % RH | `20.00` to `90.00` | Relative ambient humidity percentage measured by DHT11. |
| `heatIndexC` | `float` | °C | Real number | Computed perceived temperature (Rothfusz equation combining ambient temperature and relative humidity). |
| `gasRawADC` | `int` | Raw ADC | `0` to `1023` | 10-bit analog voltage reading from MQ-135 sensor pin `A0` (averaged over 10 consecutive samples). |
| `gasVoltage` | `float` | Volts (V) | `0.000` to `3.300` | Calculated analog voltage at ESP8266 ADC input pin (`(gasRawADC / 1023.0) * 3.3V`). |
| `gasPPM` | `float` | PPM | `0.00` to `9999.00` | Estimated hazardous gas / smoke concentration in Parts Per Million. Compensated dynamically for temperature and humidity. |
| `airQualityStatus` | `string` | Categorical | *(See table below)* | Human-readable severity classification of air quality and combustion risk. |
| `rssi` | `int` | dBm | `-100` to `0` | Wi-Fi Received Signal Strength Indicator. Values > -70 dBm represent solid link quality. |
| `ip` | `string` | IPv4 | `"x.x.x.x"` | Local IPv4 address assigned to the ESP8266 node by DHCP. |
| `uptimeSeconds` | `unsigned long`| Seconds (s) | `0` to `4294967` | Total system execution time since last boot or restart (`millis() / 1000`). |
| `freeHeap` | `uint32` | Bytes | `0` to `81920` | Free dynamically allocatable SRAM memory (`ESP.getFreeHeap()`). |

#### Air Quality & Fire Hazard States (`airQualityStatus`)
```
 0 PPM ────────── 450 PPM ────────── 800 PPM ────────── 1200 PPM ────────── 1800 PPM ──────────> [FIRE/SMOKE]
 [Fresh Air]     [Normal Indoor]    [Moderate]         [Poor/Gaseous]     [Hazardous Contaminants!]
```
- `"Sensor Preheating (1-3 min)"`: System boot warmup period before heater stabilization.
- `"Excellent / Fresh Air"`: `gasRawADC < 200` or `gasPPM < 450`
- `"Good (Normal Indoor)"`: `gasRawADC < 400` or `gasPPM < 800`
- `"Moderate / Ventilate"`: `gasRawADC < 600` or `gasPPM < 1200`
- `"Poor (Stale / Gaseous)"`: `gasRawADC < 800` or `gasPPM < 1800`
- `"Hazardous / High Contaminants!"`: `gasRawADC >= 800` or `gasPPM >= 1800` (Potential Fire/Smoke trigger)

---

### 📦 Sample Fire Node JSON Payload
```json
{
  "temperatureC": 28.40,
  "temperatureF": 83.12,
  "humidity": 64.00,
  "heatIndexC": 30.15,
  "gasRawADC": 342,
  "gasVoltage": 1.102,
  "gasPPM": 512.40,
  "airQualityStatus": "Good (Normal Indoor)",
  "rssi": -62,
  "ip": "192.168.1.105",
  "uptimeSeconds": 1420,
  "freeHeap": 41280
}
```

---

## 2. 🌊 Flood & Water Level Monitoring Node

### 📌 Overview & Pin Configuration
The **Flood Node** is deployed above drainage channels, riverbanks, bridges, or retention reservoirs. An ultrasonic acoustic transceiver calculates clearance distance down to the water surface. As water rises, the measured distance drops toward zero, triggering escalating proximity zones and proportional flashing beacons.

- **Ultrasonic Trigger (`TRIG`)**: GPIO `4`
- **Ultrasonic Echo (`ECHO`)**: GPIO `5`
- **Strobe Warning LED**: GPIO `7` (via 220Ω resistor)
- **Onboard Status LED**: GPIO `8`

---

### 📡 Network Transmission Spec
- **Protocol**: HTTP / REST GET
- **Endpoint**: `/api/data`
- **Port**: `80`
- **Format**: JSON (`application/json`)
- **Measurement Frequency**: 60 ms (~16.6 Hz high-speed acquisition)

---

### 📊 Parameter Data Dictionary

#### Top-Level Object Structure:
```json
{
  "sys": { ... },
  "sensor": { ... },
  "radar": { ... }
}
```

| JSON Path | Data Type | Units | Range / Value Set | Physical Meaning & Description |
| :--- | :--- | :--- | :--- | :--- |
| **`sys.ip`** | `string` | IPv4 | `"x.x.x.x"` | Node IP address on local network or `192.168.4.1` on fallback AP. |
| **`sys.rssi`** | `int` | dBm | `-100` to `0` | Wi-Fi RF signal strength at ESP32-S3 antenna. |
| **`sys.uptime_sec`** | `unsigned long`| Seconds (s) | `0` to `4294967` | Total operational seconds since power-on. |
| **`sys.free_heap`** | `uint32` | Bytes | `0` to `393216` | Free heap memory available in ESP32-S3 SRAM. |
| **`sensor.valid`** | `boolean` | Flag | `true` / `false` | `true` if valid acoustic return echo was received within 2.0 cm to 400.0 cm range. |
| **`sensor.dist_cm`** | `float` | cm | `2.0` to `400.0` | Distance from sensor head to water surface in centimeters (`(pulse_us * 0.0343) / 2`). |
| **`sensor.dist_in`** | `float` | inches | `0.8` to `157.5` | Clearance distance in inches (`dist_cm / 2.54`). |
| **`sensor.dist_m`** | `float` | meters (m) | `0.02` to `4.00` | Clearance distance in meters (`dist_cm / 100.0`). |
| **`sensor.pulse_us`** | `unsigned long`| µs | `116` to `23323` | High-precision round-trip transit duration of ultrasonic pulse. |
| **`radar.zone`** | `string` | Categorical | *(See table below)* | Flood risk assessment stage based on distance threshold. |
| **`radar.interval_ms`**| `unsigned long`| Milliseconds (ms)| `50` to `1000` | Dynamic blinking period for strobe LED. Shorter interval = closer flood water. |
| **`radar.rate_desc`** | `string` | Categorical | `"OFF"`, `"SLOW"`, `"MEDIUM"`, `"FAST"`, `"VERY FAST"` | Qualitative strobe flash speed description. |
| **`radar.led_state`** | `boolean` | Binary | `true` / `false` | Real-time binary state of the warning strobe / indicator LED (`HIGH` or `LOW`). |

#### Flood Warning Zones & Strobe Dynamics
| Water Clearance (`dist_cm`) | `radar.zone` Category | `radar.interval_ms` | `radar.rate_desc` | Flood Hazard Condition |
| :--- | :--- | :--- | :--- | :--- |
| **< 20.0 cm** | `"CRITICAL PROXIMITY (FASTEST BLINK)"` | `50 ms - 100 ms` | `"VERY FAST"` | 🚨 **FLASH FLOOD IMMINENT / OVERFLOW** |
| **20.0 cm – 59.9 cm** | `"CLOSE RANGE (FAST BLINK)"` | `100 ms - 250 ms` | `"FAST"` | ⚠️ **HIGH WATER / EVACUATION ALERT** |
| **60.0 cm – 149.9 cm**| `"MID RANGE (MODERATE BLINK)"` | `250 ms - 600 ms` | `"MEDIUM"` | 🌊 **RISING WATER / ADVISORY STAGE** |
| **150.0 cm – 400.0 cm**| `"FAR RANGE (SLOW BLINK)"` | `600 ms - 1000 ms`| `"SLOW"` | 🟢 **NORMAL BASIN LEVEL / SAFE CLEARANCE** |
| **> 400.0 cm / No Echo**| `"OUT OF RANGE (> 400 CM)"` / `"NO ECHO / STANDBY"` | `0 ms` (OFF) | `"OFF"` | ⚪ **SENSOR TIMEOUT / DRY BED / OBSTRUCTED** |

> **Flood Crest Calculation Formula**:  
> If the sensor is mounted at benchmark height $H_{\text{mount}}$ (e.g. 300 cm above riverbed):  
> $$\text{Water Depth} = H_{\text{mount}} - \text{sensor.dist\_cm}$$

---

### 📦 Sample Flood Node JSON Payload
```json
{
  "sys": {
    "ip": "192.168.1.112",
    "rssi": -55,
    "uptime_sec": 8940,
    "free_heap": 298412
  },
  "sensor": {
    "valid": true,
    "dist_cm": 18.4,
    "dist_in": 7.2,
    "dist_m": 0.18,
    "pulse_us": 1072
  },
  "radar": {
    "zone": "CRITICAL PROXIMITY (FASTEST BLINK)",
    "interval_ms": 78,
    "rate_desc": "VERY FAST",
    "led_state": true
  }
}
```

---

## 3. ⛰️ Landslide & Seismic / Slope Displacement Node

### 📌 Overview & Pin Configuration
The **Landslide Node** monitors mountain slope stability, mudslide occurrence, ground shaking, and spatial displacement using a 6-DOF Inertial Measurement Unit (IMU) coupled with a high-sensitivity satellite GPS positioning module.

- **NEO-6M GPS UART2**: 
  - GPS `TX` &rarr; ESP32 `GPIO 16` (`RX2`)
  - GPS `RX` &rarr; ESP32 `GPIO 17` (`TX2`)
  - Baud Rate: `9600`
- **MPU-6050 6-DOF IMU (I2C)**: 
  - `SDA` &rarr; ESP32 `GPIO 21`
  - `SCL` &rarr; ESP32 `GPIO 22`
  - Bus Frequency: `100 kHz` / `400 kHz` Standard I2C

---

### 📡 Network Transmission Spec
- **Protocol**: HTTP / REST GET
- **Endpoint**: `/api/data`
- **Port**: `80`
- **Format**: JSON (`application/json`)
- **IMU Read Rate**: 100 ms (10 Hz high-frequency dynamics)

---

### 📊 Parameter Data Dictionary

#### Top-Level Object Structure:
```json
{
  "sys": { ... },
  "gps": { ... },
  "mpu": { ... }
}
```

#### 3.1 System Telemetry Block (`sys`)
| Parameter Key | Data Type | Units | Range | Physical Meaning |
| :--- | :--- | :--- | :--- | :--- |
| `sys.ip` | `string` | IPv4 | `"x.x.x.x"` | Node IP address on network. |
| `sys.rssi` | `int` | dBm | `-100` to `0` | Wi-Fi link strength at landslide observation post. |
| `sys.uptime_sec` | `unsigned long`| Seconds | `0` to `4294967` | Continuous operating time since boot. |
| `sys.free_heap` | `uint32` | Bytes | `0` to `327680` | Available free ESP32 RAM. |

---

#### 3.2 GPS Satellite & Geodetic Positioning Block (`gps`)
| Parameter Key | Data Type | Units | Range / Format | Physical Meaning & Diagnostic Value |
| :--- | :--- | :--- | :--- | :--- |
| `gps.hw_alive` | `boolean` | Flag | `true` / `false` | `true` if UART bytes received from NEO-6M within past 2500 ms. |
| `gps.hw_status` | `string` | Enum | `"ALIVE & STREAMING"`, `"COMM TIMEOUT / STALLED"`, `"DEAD / NO DATA RECEIVED"` | Hardware serial communication health check. |
| `gps.fix_stage` | `string` | State | `"OFFLINE"`, `"SEARCHING SATELLITES"`, `"ACQUIRING FIX"`, `"3D FIX LOCKED (X SATS)"` | Satellite acquisition lifecycle phase. |
| `gps.status_detail` | `string` | Text | Human text | Detailed troubleshooting and RF signal reception diagnostic. |
| `gps.last_rx_ms` | `unsigned long`| ms | `0` to `999999` | Milliseconds elapsed since the last raw NMEA byte was parsed. |
| `gps.connected` | `boolean` | Flag | `true` / `false` | Confirms whether valid NMEA sentences were ever detected. |
| `gps.fix` | `boolean` | Flag | `true` / `false` | `true` only when a valid 3D GPS navigation fix exists and fix age is < 5000 ms. |
| `gps.satellites` | `int` | Count | `0` to `24` | Number of GPS/GLONASS satellites actively tracked. Requires $\ge 4$ for 3D fix. |
| `gps.hdop` | `float` | Ratio | `0.5` to `50.0` | Horizontal Dilution of Precision. Values < 2.0 denote high military/survey accuracy. |
| `gps.lat` | `float` | Degrees | `-90.000000` to `+90.000000` | Geodetic Latitude coordinate (6 decimal places $\approx 0.11\text{ m}$ resolution). |
| `gps.lng` | `float` | Degrees | `-180.000000` to `+180.000000`| Geodetic Longitude coordinate (6 decimal places). |
| `gps.alt_m` | `float` | meters (m) | `-100.0` to `9000.0` | Elevation above Mean Sea Level (MSL) in meters. |
| `gps.alt_ft` | `float` | feet (ft) | `-328.0` to `29527.0` | Elevation above Mean Sea Level (MSL) in feet. |
| `gps.speed_kmh` | `float` | km/h | `0.0` to `500.0` | Ground velocity (detects active downslope landslide sliding speed). |
| `gps.speed_mph` | `float` | mph | `0.0` to `310.0` | Ground velocity in miles per hour. |
| `gps.course_deg` | `float` | Degrees | `0.0` to `360.0` | Heading vector relative to True North. |
| `gps.cardinal` | `string` | Compass | `"N"`, `"NE"`, `"E"`, `"SE"`, `"S"`, `"SW"`, `"W"`, `"NW"`, `"--"` | Cardinal direction of terrain movement. |
| `gps.date` | `string` | Date | `"YYYY/MM/DD"` | UTC calendar date synchronized directly from atomic clocks on GPS satellites. |
| `gps.time` | `string` | Time | `"HH:MM:SS"` | UTC 24-hour time synchronized directly from GPS atomic clocks. |
| `gps.fix_age_ms` | `unsigned long`| ms | `0` to `60000` | Age of position lock. Values < 1000 ms ensure real-time telemetry freshness. |
| `gps.chars_rx` | `unsigned long`| Count | `0` to `4294967` | Total NMEA characters processed over UART since boot. |
| `gps.checksum_fail`| `unsigned short`| Count | `0` to `65535` | Count of corrupted NMEA sentences rejected by checksum validation. |

---

#### 3.3 IMU Seismic & Ground Motion Block (`mpu`)
| Parameter Key | Data Type | Units | Range | Physical Meaning & Diagnostic Value |
| :--- | :--- | :--- | :--- | :--- |
| `mpu.connected` | `boolean` | Flag | `true` / `false` | I2C communication status with MPU-6050 (Address `0x68`). |
| `mpu.ax` | `float` | $\text{m/s}^2$ | `-156.9` to `+156.9` | Linear acceleration along X-axis (forward/back slope plane). |
| `mpu.ay` | `float` | $\text{m/s}^2$ | `-156.9` to `+156.9` | Linear acceleration along Y-axis (lateral slope plane). |
| `mpu.az` | `float` | $\text{m/s}^2$ | `-156.9` to `+156.9` | Linear acceleration along Z-axis (vertical gravity axis). |
| `mpu.ax_g` | `float` | $g$ | `-16.000` to `+16.000` | Normalized X-axis acceleration expressed in $g$ ($1g \approx 9.80665\text{ m/s}^2$). |
| `mpu.ay_g` | `float` | $g$ | `-16.000` to `+16.000` | Normalized Y-axis acceleration expressed in $g$. |
| `mpu.az_g` | `float` | $g$ | `-16.000` to `+16.000` | Normalized Z-axis acceleration expressed in $g$. |
| **`mpu.total_accel_g`**| `float` | $g$ | `0.00` to `27.70` | **Resultant vector magnitude** $\sqrt{a_x^2 + a_y^2 + a_z^2}$. Baseline is $1.00g$ at rest. Spikes $> 1.30g$ signify earthquakes, tremors, or debris collapse. |
| `mpu.gx` | `float` | °/s | `-2000.0` to `+2000.0` | Angular rotational velocity around X-axis. |
| `mpu.gy` | `float` | °/s | `-2000.0` to `+2000.0` | Angular rotational velocity around Y-axis. |
| `mpu.gz` | `float` | °/s | `-2000.0` to `+2000.0` | Angular rotational velocity around Z-axis. |
| `mpu.gx_rad` | `float` | rad/s | `-34.90` to `+34.90` | Angular velocity around X-axis in radians per second. |
| `mpu.gy_rad` | `float` | rad/s | `-34.90` to `+34.90` | Angular velocity around Y-axis in radians per second. |
| `mpu.gz_rad` | `float` | rad/s | `-34.90` to `+34.90` | Angular velocity around Z-axis in radians per second. |
| `mpu.temp_c` | `float` | °C | `-40.0` to `+85.0` | MPU-6050 internal silicon die temperature in Celsius. |
| `mpu.temp_f` | `float` | °F | `-40.0` to `+185.0` | MPU-6050 internal silicon die temperature in Fahrenheit. |
| **`mpu.pitch`** | `float` | Degrees (°) | `-90.0` to `+90.0` | **Front-to-back tilt incline angle** $\arctan(a_x / \sqrt{a_y^2 + a_z^2}) \cdot \frac{180}{\pi}$. Detects slope downhill creep. |
| **`mpu.roll`** | `float` | Degrees (°) | `-180.0` to `+180.0` | **Side-to-side bank/cant angle** $\arctan(a_y / \sqrt{a_x^2 + a_z^2}) \cdot \frac{180}{\pi}$. Detects ground slipping. |

#### Landslide Warning Thresholds
- **Earthquake / Ground Tremor Trigger**: `total_accel_g > 1.35g` or `total_accel_g < 0.65g` (freefall slip).
- **Slope Displacement Trigger**: $|\Delta\text{pitch}| > 5.0^\circ$ or $|\Delta\text{roll}| > 5.0^\circ$ from calibrated baseline.
- **Surface Slide Velocity Trigger**: `speed_kmh > 2.0 km/h` while coordinates (`lat`, `lng`) drift continuously.

---

### 📦 Sample Landslide Node JSON Payload
```json
{
  "sys": {
    "ip": "192.168.1.118",
    "rssi": -68,
    "uptime_sec": 3612,
    "free_heap": 218450
  },
  "gps": {
    "hw_alive": true,
    "hw_status": "ALIVE & STREAMING",
    "fix_stage": "3D FIX LOCKED (9 SATS)",
    "status_detail": "Active 3D satellite lock acquired. High-precision navigation coordinates valid.",
    "last_rx_ms": 120,
    "connected": true,
    "fix": true,
    "satellites": 9,
    "hdop": 1.15,
    "lat": 27.717245,
    "lng": 85.324061,
    "alt_m": 1398.4,
    "alt_ft": 4587.9,
    "speed_kmh": 0.1,
    "speed_mph": 0.0,
    "course_deg": 182.4,
    "cardinal": "S",
    "date": "2026/09/03",
    "time": "12:35:48",
    "fix_age_ms": 180,
    "chars_rx": 48201,
    "checksum_fail": 2
  },
  "mpu": {
    "connected": true,
    "ax": 0.412,
    "ay": -0.215,
    "az": 9.782,
    "ax_g": 0.042,
    "ay_g": -0.022,
    "az_g": 0.998,
    "total_accel_g": 1.00,
    "gx": 0.12,
    "gy": -0.08,
    "gz": 0.04,
    "gx_rad": 0.002,
    "gy_rad": -0.001,
    "gz_rad": 0.001,
    "temp_c": 26.8,
    "temp_f": 80.2,
    "pitch": 2.4,
    "roll": -1.3
  }
}
```

---

## 4. 🔗 Gateway & Integration Protocol

The parameters from all three nodes feed into HYDRA's two dispatch and command hubs:

### A. 📟 LoRa Emergency Packet Protocol (`example.ino` / SX1278 433 MHz)
Long-range, off-grid alerts transmit via the protocol format:
$$\text{Payload} = \texttt{"TX[DEVICE\_ID],[ALERT\_CODE]"}$$
- `DEVICE_ID`: Node identifier integer (e.g. `101` for Fire, `102` for Flood, `103` for Landslide).
- `ALERT_CODE`: Single letter `A` through `O` or numerical index `0` to `14`:

| Alert Index | Code | Alert Type | Corresponding Trigger Node | Priority |
| :--- | :--- | :--- | :--- | :--- |
| `0` | `A` | **EMERGENCY** | General emergency alert | `CRITICAL` (0) |
| `3` | `D` | **EVACUATION NEEDED** | Any node reaching critical stage | `CRITICAL` (0) |
| `8` | `I` | **WEATHER ALERT** | Extreme temperature or humidity | `MEDIUM` (2) |
| `11` | `L` | **LANDSLIDE** | Landslide Node (`pitch/roll` or `total_accel_g` breach) | `HIGH` (1) |
| `13` | `N` | **EQUIPMENT FAILURE**| Node sensor disconnect / timeout | `MEDIUM` (2) |
| `4` | `E` | **STATUS OK** | Normal baseline telemetry | `OK` (3) |

### B. 📱 GSM Cellular Call & SMS Gateway (`ESP32_GSM_Call_SMS_Dashboard.ino`)
When thresholds are breached, the GSM gateway triggers automated emergency dispatch:
- **Phone Call**: Directly dials emergency response numbers (`ATD+<PHONE_NUMBER>;`) to immediately alert field officers.
- **SMS Dispatch**: Transmits SMS alerts containing the latest critical telemetry:
  ```text
  [HYDRA ALERT]
  NODE: LANDSLIDE-01
  LOCATION: 27.717245 N, 85.324061 E
  PITCH: 14.8 DEG | ROLL: 11.2 DEG
  SHOCK: 1.82 G
  TIME: 2026/09/03 12:35:48 UTC
  ```

---

## 5. 🌐 Consolidated Multi-Node Aggregated Data Schema

For central dashboard servers (Node-RED, MQTT Broker, InfluxDB, or Cloud REST APIs), the three individual payloads can be aggregated into this unified telemetry model:

```json
{
  "network": "HYDRA-EMERGENCY-NET",
  "timestamp": "2026-09-03T12:35:48Z",
  "nodes": {
    "fire_node": {
      "status": "ONLINE",
      "ip": "192.168.1.105",
      "temperature_c": 28.40,
      "humidity_pct": 64.00,
      "gas_ppm": 512.40,
      "air_quality": "Good (Normal Indoor)",
      "hazard_level": "LOW"
    },
    "flood_node": {
      "status": "ONLINE",
      "ip": "192.168.1.112",
      "clearance_cm": 18.4,
      "zone": "CRITICAL PROXIMITY (FASTEST BLINK)",
      "blink_interval_ms": 78,
      "hazard_level": "CRITICAL"
    },
    "landslide_node": {
      "status": "ONLINE",
      "ip": "192.168.1.118",
      "latitude": 27.717245,
      "longitude": 85.324061,
      "altitude_m": 1398.4,
      "satellites": 9,
      "pitch_deg": 2.4,
      "roll_deg": -1.3,
      "total_accel_g": 1.00,
      "hazard_level": "NOMINAL"
    }
  }
}
```
