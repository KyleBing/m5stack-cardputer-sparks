# GPS 定位

进入方式：主菜单 `e` → [Grove](./ex-i2c) `4` / `g`

**AT6668** NMEA 0183（约 115200）。支持两种硬件源，进入 App 时**自动探测**（先 Cap，再 Grove）：

| 源 | 硬件 | 主机 UART | 说明 |
|----|------|-----------|------|
| **Cap LoRa** | Cap LoRa-1262 内置 GNSS（ATGM336H @ AT6668） | **RX=G15 / TX=G13** | 不占用左侧 Grove I2C；Adv + Cap 时可继续用 Radio / NFC |
| **Grove** | GPS Unit（AT6668）或兼容 UART NMEA 模块 | **RX=G1 / TX=G2** | 进入时 `Ex_I2C.release()`，离开再恢复 I2C |

Settings 页只读显示当前源（`Cap LoRa G15/G13` / `Grove G1/G2` / `none`）。探测约 1 s；两侧都无 `$` 时仍监听 Grove，慢启动模块稍后出数会标成 Grove。

冷启动（断电或长时间未用）搜星大约 **20 多秒**；热启动（刚定位过不久）大约 **1 秒**。室外空旷处更快；室内常为 `NO FIX`。

## 截图

**Empty（NO FIX / 搜星前）**

<div class="shot-row">

![gps-live-empty](/shots/app_exi2c_gps_live_empty.png)
![gps-sats-empty](/shots/app_exi2c_gps_satellites_empty.png)
![gps-sky-empty](/shots/app_exi2c_gps_sky_empty.png)
![gps-speed](/shots/app_exi2c_gps_speed.png)

</div>

**有定位（3D FIX）**

<div class="shot-row">

![gps-live](/shots/app_exi2c_gps_live.png)
![gps-sats](/shots/app_exi2c_gps_satellites.png)
![gps-sky](/shots/app_exi2c_gps_sky.png)
![gps-history](/shots/app_exi2c_gps_history.png)
![gps-settings](/shots/app_exi2c_gps_settings.png)

</div>

**Chart：Speed / Alt / Accel / All / Map**

<div class="shot-row">

![gps-chart](/shots/app_exi2c_gps_chart.png)
![gps-chart-alt](/shots/app_exi2c_gps_chart_alt.png)
![gps-chart-accel](/shots/app_exi2c_gps_chart_accel.png)
![gps-chart-all](/shots/app_exi2c_gps_chart_all.png)
![gps-chart-map](/shots/app_exi2c_gps_chart_map.png)

</div>

## 模块与接线

### Grove GPS Unit

买 M5Stack **GPS Unit（AT6668）** 或兼容 UART NMEA 模块，插**左侧 Grove**（HY2.0-4P）。本路径**不用 I2C**，同一组脚作串口：

| Grove 针脚 | 作用（GPS） |
|------------|-------------|
| GND | 地 |
| 5V | 电源 |
| **G2** | **TX**（模块 → 主机 RX） |
| **G1** | **RX**（主机 TX → 模块） |

波特率 **115200**。走 Grove 源时与 Radio / NFC 不能同时占用左侧 Grove。

### Cap LoRa-1262 GNSS

Cardputer-Adv 扣上 **Cap LoRa-1262** 即可（内置陶瓷天线）。GNSS 走 Cap 总线 UART，与官方 Arduino 示例一致：`Serial1` **RX=15 / TX=13**。本 App **只用 GNSS**，不初始化 SX1262 LoRa。Cap 上的 HY2.0-4P 仍可接其它 Grove I2C 设备。

## 页面

| 键 | 页面 | 内容 |
|----|------|------|
| `1` | Live | 速度、经纬高、HDOP、星数、航向、UTC |
| `2` / `s` | Speed | 行程统计、0–50 / 0–100、制动、峰值 g |
| `3` | Satellites | GPS / BeiDou / GLONASS / Galileo / QZSS 可见星与 PDOP/VDOP |
| `4` | Sky plot | 天空图（上北下南）+ SNR 列表 |
| `5` / `l` | History | 已存行程；`Enter` 开曲线，`Bk` 删除 |
| `6` / `o` | Settings | 当前数据源（只读）+ 刷新率 1 / 2 / 5 / 10 Hz（PCAS02，NVS） |

顶栏右侧：`NO FIX` / `2D FIX` / `3D FIX`；录制中显示停止图标并闪烁。

## 快捷键

完整说明见 `h` Help（多页）。

| 按键 | 作用 |
|------|------|
| `h` | Help |
| `1`–`6` / `s` `l` `o` | 切页面（见上表） |
| `Space` / **BtnGO** | 开始 / 停止速度录制 |
| `r` | 重置 Live / Speed 统计（录制中会先停再开） |
| `m` | 曲线页循环：速度 / 高度 / 加速度 / 三合一 / 路线图 |
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

1. 接好 Grove GPS Unit，或 Adv + Cap LoRa-1262；主菜单 `e` → `g`（或 `4`）。  
2. Settings（`o`）可确认当前源；室外空旷处等 `3D FIX`：冷启动约 20 多秒，热启动约 1 秒。  
3. `Space` / 侧键开始录制，再按一次停止并写入 History。  
4. 刷新率默认多为 1 Hz；最高 10 Hz，更费电。  
5. 退出 App 会停录制、关串口；若用过 Grove 源则恢复 Grove I2C。

## 记录与路线

- History 打开后按 `m` 循环：速度 / 高度 / 加速度 / 三合一 / **Map**（经纬度轨迹，左路线右摘要）。
- 导入 / 导出：进入 [Config](./config) Web → **GPS**（`/gps`）。格式为 **GPX 1.1**（地图软件通用）；测速峰值、融合速度、加速度、星数等写在 `cardputer:` 扩展里，便于完整往返。满 12 条再导入会丢掉最旧的一条。
