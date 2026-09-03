# RAW SENSOR DATA & PARAMETERS

---

## 1. FLOOD NODE (Ultrasonic HC-SR04)



### Parameters List:
* `dist_cm` (Distance in cm)


## 2. FIRE NODE (DHT11 + MQ-135)



### Parameters List:
#### DHT11:
* `temperatureC` (°C)
* `humidity` (%)


#### MQ-135:

* `gasPPM` (PPM value)



## 3. LANDSLIDE NODE (NEO-6M GPS + MPU-6050)



### Parameters List:

#### GPS (NEO-6M):
* `gps.connected` (true / false)
* `gps.hw_alive` (true / false)
* `gps.hw_status` ("ALIVE & STREAMING" / "DEAD / NO DATA")
* `gps.fix` (true / false -> locked or not)
* `gps.fix_stage` ("SEARCHING SATELLITES" / "ACQUIRING FIX" / "3D FIX LOCKED")
* `gps.satellites` (Number of satellites connected)
* `gps.lat` (Latitude coordinate)
* `gps.lng` (Longitude coordinate)
* `gps.alt_m` (Altitude meters)
* `gps.alt_ft` (Altitude feet)
* `gps.speed_kmh` (Speed km/h)
* `gps.course_deg` (Heading degrees 0 - 360)
* `gps.cardinal` (Direction: N, NE, E, SE, S, SW, W, NW)
* `gps.date` (UTC date "YYYY/MM/DD")
* `gps.time` (UTC time "HH:MM:SS")
* `gps.hdop` (Signal precision value)
* `gps.fix_age_ms` (Milliseconds since last update)
* `gps.last_rx_ms` (Time since last byte)
* `gps.chars_rx` (Total characters received)
* `gps.checksum_fail` (Checksum error count)
* `gps.status_detail` (Status text string)

#### MPU-6050:
* `mpu.connected` (true / false)
* `mpu.ax` (Acceleration X in m/s²)
* `mpu.ay` (Acceleration Y in m/s²)
* `mpu.az` (Acceleration Z in m/s²)
* `mpu.ax_g` (Acceleration X in g)
* `mpu.ay_g` (Acceleration Y in g)
* `mpu.az_g` (Acceleration Z in g)
* `mpu.total_accel_g` (Total resultant acceleration in g)
* `mpu.pitch` (Tilt angle pitch in degrees)
* `mpu.roll` (Tilt angle roll in degrees)
* `mpu.gx` (Gyro X in °/s)
* `mpu.gy` (Gyro Y in °/s)
* `mpu.gz` (Gyro Z in °/s)
* `mpu.gx_rad` (Gyro X in rad/s)
* `mpu.gy_rad` (Gyro Y in rad/s)
* `mpu.gz_rad` (Gyro Z in rad/s)
* `mpu.temp_c` (Internal chip temperature in °C)
* `mpu.temp_f` (Internal chip temperature in °F)


