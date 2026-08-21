# GPS 定位

进入方式：主菜单 `e` → [Grove](./ex-i2c) `5` / `g`

官方 **AT6668 GPS Unit**（NMEA 0183）。插左侧 Grove；固件把 **G1/G2 切成 UART**（115200），进入时会先 `Ex_I2C.release()`，离开再恢复 I2C，避免占住收音机 / NFC / 扫描。

## 截图

**Live / 无定位 / Speed / Satellites**

<div class="shot-row">

![gps-live](/shots/app_exi2c_gps_live.png)
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

## 模块与接线

买 M5Stack **GPS Unit（AT6668）** 或兼容的 UART NMEA 模块，插到机身**左侧 Grove**（HY2.0-4P）。本 App **不用 I2C**，同一组脚作串口：

| Grove 针脚 | 作用（GPS） |
|------------|-------------|
| GND | 地 |
| 5V | 电源 |
| **G2** | **TX**（模块 → 主机 RX） |
| **G1** | **RX**（主机 TX → 模块） |

波特率 **115200**。与 Radio / NFC 不能同时占用左侧 Grove。

## 页面

| 键 | 页面 | 内容 |
|----|------|------|
| `1` | Live | 速度、经纬高、HDOP、星数、航向、UTC |
| `2` / `s` | Speed | 行程统计、0–50 / 0–100、制动、峰值 g |
| `3` | Satellites | GPS / BeiDou / GLONASS / Galileo / QZSS 可见星与 PDOP/VDOP |
| `4` | Sky plot | 天空图（上北下南）+ SNR 列表 |
| `5` / `l` | History | 已存行程；`Enter` 开曲线，`Bk` 删除 |
| `6` / `o` | Settings | 模块刷新率 1 / 2 / 5 / 10 Hz（PCAS02，写入 NVS） |

顶栏右侧：`NO FIX` / `2D FIX` / `3D FIX`；录制中显示停止图标并闪烁。

## 快捷键

完整说明见 `h` Help（多页）。

| 按键 | 作用 |
|------|------|
| `h` | Help |
| `1`–`6` / `s` `l` `o` | 切页面（见上表） |
| `Space` / **BtnGO** | 开始 / 停止速度录制 |
| `r` | 重置 Live / Speed 统计（录制中会先停再开） |
| `m` | 曲线页循环指标：速度 / 高度 / 加速度 |
| ↑ ↓ 等 | History 选记录；Settings 选刷新率 |
| `Enter` | History → 曲线；Settings 应用刷新率 |
| `Bk` | 删除选中历史 |
| `ESC` | 曲线 → History；Settings → 进入前页面；Help → 关闭 |

## 字段说明

| 字段 | 含义 |
|------|------|
| Lat / Lon / Alt | 纬度 / 经度 / 海拔 (m) |
| HDOP / PDOP / VDOP | 水平 / 位置 / 垂直精度因子（越小越好） |
| Sats | 参与解算 / 可见卫星 |
| Course | 航向（度） |
| 0–30 / 0–50 / 0–100 | 加速到对应车速的时间 |
| 100–0 | 从 100 km/h 制动时间 |
| Accel | 最大加速 / 制动 (g) |

Sky plot：同心圆为仰角 0° / 30° / 60°；点颜色按系统 G/C/R/E/J，大小跟 SNR。

## 使用说明

1. GPS Unit 插左侧 Grove，主菜单 `e` → `g`（或 `5`）。  
2. 室外空旷处等 `3D FIX`；室内常为 `NO FIX`。  
3. `Space` / 侧键开始录制，再按一次停止并写入 History。  
4. `o` 调刷新率（默认模块多为 1 Hz；最高 10 Hz，更费电）。  
5. 退出 App 会停录制、关串口并恢复 Grove I2C。
