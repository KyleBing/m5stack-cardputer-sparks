# GPS

Open from: main menu `e` → [EX I2C](./ex-i2c) `5` / `g`

Official **AT6668 GPS Unit** (NMEA 0183). Plug into the left Grove; firmware switches **G1/G2 to UART** (115200). On enter it `Ex_I2C.release()`s; on leave it restores I2C so Radio / NFC / scan keep working.

## Screenshots

**Live / no fix / Speed / Satellites**

<div class="shot-row">

![gps-live](/shots/app_exi2c_gps_live.png)
![gps-nofix](/shots/app_exi2c_gps_nofix.png)
![gps-speed](/shots/app_exi2c_gps_speed.png)
![gps-sats](/shots/app_exi2c_gps_satellites.png)

</div>

**Sky plot / History / Chart / Settings**

<div class="shot-row">

![gps-sky](/shots/app_exi2c_gps_sky.png)
![gps-history](/shots/app_exi2c_gps_history.png)
![gps-chart](/shots/app_exi2c_gps_chart.png)
![gps-settings](/shots/app_exi2c_gps_settings.png)

</div>

## Module & wiring

Use an M5Stack **GPS Unit (AT6668)** or a compatible UART NMEA module on the **left Grove** (HY2.0-4P). This App is **not I2C** — the same pins run as serial:

| Grove pin | Role (GPS) |
|-----------|------------|
| GND | Ground |
| 5V | Power |
| **G2** | **TX** (module → host RX) |
| **G1** | **RX** (host TX → module) |

Baud **115200**. Do not share the left Grove with Radio / NFC at the same time.

## Pages

| Key | Page | Content |
|-----|------|---------|
| `1` | Live | Speed, lat/lon/alt, HDOP, sats, course, UTC |
| `2` / `s` | Speed | Trip stats, 0–50 / 0–100, braking, peak g |
| `3` | Satellites | GPS / BeiDou / GLONASS / Galileo / QZSS + PDOP/VDOP |
| `4` | Sky plot | Sky map (heading-up) + SNR list |
| `5` / `l` | History | Saved runs; `Enter` opens chart, `Bk` deletes |
| `6` / `o` | Settings | Module rate 1 / 2 / 5 / 10 Hz (PCAS02, stored in NVS) |

Header right: `NO FIX` / `2D FIX` / `3D FIX`; while recording a stop icon blinks.

## Shortcuts

Full list: `h` Help (multi-page).

| Key | Action |
|-----|--------|
| `h` | Help |
| `1`–`6` / `s` `l` `o` | Switch pages (table above) |
| `Space` / **BtnGO** | Start / stop speed recording |
| `r` | Reset Live / Speed stats (restarts record if active) |
| `m` | On chart: cycle Speed / Altitude / Accel |
| ↑ ↓ etc. | Select history row or settings rate |
| `Enter` | History → chart; Settings apply rate |
| `Bk` | Delete selected history |
| `ESC` | Chart → History; Settings → previous page; Help → close |

## Fields

| Field | Meaning |
|-------|---------|
| Lat / Lon / Alt | Latitude / longitude / altitude (m) |
| HDOP / PDOP / VDOP | Dilution of precision (lower is better) |
| Sats | Used / visible satellites |
| Course | Heading (degrees) |
| 0–30 / 0–50 / 0–100 | Time to reach that speed |
| 100–0 | Brake time from 100 km/h |
| Accel | Peak accel / brake (g) |

Sky plot: rings are elev 0° / 30° / 60°; dots G/C/R/E/J by system, size by SNR.

## Usage

1. Plug the GPS Unit into the left Grove; main menu `e` → `g` (or `5`).  
2. Wait outdoors for `3D FIX`; indoors you often stay on `NO FIX`.  
3. `Space` / side button starts a run; press again to stop and save to History.  
4. `o` sets update rate (modules often ship at 1 Hz; max 10 Hz uses more power).  
5. Leaving the App stops recording, ends UART, and restores Grove I2C.
