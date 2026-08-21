# EX I2C 外设

主菜单按键：`e`

外接模块集合：左侧 Grove 的 **I2C**（收音机、总线扫描）与背面 EXT14 的 **SPI**（CC1101）。选择页每页最多 8 项；数字键按当前页从 `1` 编号，字母快捷键**不依赖当前页**即可直达。子应用中 `ESC` / `GO` 回到本集合；在选择页再按一次返回主菜单。

本页说明固件里**已经落地的芯片接线、功能，以及源码 API**。收音机见 [Radio](./radio)；NFC 见 [NFC](./nfc)；GPS 见 [GPS](./gps)；总线扫描见 [I2C 扫描](./i2c)。

## 截图

**选择页 / 收音机 / GPS Live**

<div class="shot-row">

![exi2c-hub](/shots/app_exi2c_001.png)
![radio-playing](/shots/app_radio_playing.png)
![gps-live](/shots/app_exi2c_gps_live.png)

</div>

## 子应用

| 按键 | 子应用 | 总线 | 芯片 | 功能 |
|------|--------|------|------|------|
| `1` / `r` | [RADIO](./radio) | Grove I2C | TEA5767 / RDA5807M | FM 收音、搜台、电台列表；RDA 另有音量 / RDS |
| `2` / `e` | [EXI2](./i2c) | Grove I2C | 扫描到的任意地址 | 列地址、猜测芯片名与用途 |
| `3` / `c` | CC1101 | EXT14 SPI | CC1101 | 433 MHz 收发测试、RSSI、调频 |
| `4` / `n` | [NFC](./nfc) | Grove I2C | ST25R3916（Unit NFC） | 读 / 写 13.56 MHz、NDEF 模拟、历史 |
| `5` / `g` | [GPS](./gps) | Grove UART | AT6668 GPS Unit | Live / 速度 / 星图 / 录制；G1/G2 切串口 |

## 快捷键

| 按键 | 作用 |
|------|------|
| `1`–`8` | 进入当前页对应子应用 |
| `r` / `e` / `c` / `n` / `g` | 直达 RADIO / EXI2 / CC1101 / NFC / GPS |
| `[` `]` / 方向键 | 选择页翻页（超过 8 项时） |
| `ESC` / `GO` | 子应用 → 选择页 → 主菜单 |
| `h` | 子应用内 Help（选择页无 Help） |

---

## 硬件接口

Cardputer-ADV 对外有两路常用外设口。EX I2C 集合把它们放在同一个入口，但**协议不同，不能混插电源**。

### 左侧 Grove（Ex_I2C）

屏幕朝自己、键盘在下时，机身左侧 HY2.0-4P 橡胶座。固件里对应 `M5Cardputer.Ex_I2C`。进入相关 App 时会 `Ex_I2C.begin()`（启动阶段只 `setPort`，不 begin 则 Grove 扫描全空）。

| 顺序（上→下） | 针脚 | 作用 |
|---------------|------|------|
| 1 | GND | 地 |
| 2 | 5V | 电源（给 5V 模块） |
| 3 | **G2** | **SDA** |
| 4 | **G1** | **SCL** |

收音机模块按丝印 **SDA / SCL / VCC / GND** 对到上表，不要按模块从左到右硬数。音频走**模块 3.5 mm 耳机孔**，不走机身喇叭。

备用：Grove 上探测不到 FM 芯片时，Radio 会再试 `M5Cardputer.In_I2C`（EXT 排针 **G8=SDA / G9=SCL**）。

### 背面 EXT14（CC1101 SPI）

与 microSD **共用 SPI**，片选独立。固件用 FSPI，`SPISettings(2 MHz, MSBFIRST, SPI_MODE0)`。

| 模块脚 | GPIO | 作用 |
|--------|------|------|
| VCC | **3.3 V** | 仅 3.3 V，**禁止接 5V** |
| GND | GND | 地 |
| CSN | **G13** | 片选 |
| SCK | **G40** | SPI 时钟 |
| MOSI | **G14** | 主机输出 |
| MISO | **G39** | 主机输入 |
| GDO0 | **G15** | RX/TX 完成中断 |
| GDO2 | **G5** | 可选载波侦测（固件已接，RST 未接） |

EXT14 上的 `5VIN` / `5VOUT` 是 5 V，不能给 CC1101 当 VCC。

---

## 已接入的芯片

### TEA5767（FM）

| 项 | 值 |
|----|----|
| 总线 | I2C，地址 **`0x60`** |
| 口 | 优先 Grove G2/G1，其次 In_I2C G8/G9 |
| 频率单位 | 0.01 MHz（`9850` = 98.50 MHz） |
| 频段 | 欧带 87.50–108.00 MHz；日带 76.00–91.00 MHz |
| 步进 | 0.10 MHz |
| 音量 | 芯片无音量寄存器，用耳机旋钮 |
| 探测 | `scanID(0x60)`；写 5 字节寄存器组 |
| 源码 | `include/tea5767.h`、`src/tea5767.cpp` |

功能：调谐、静音、强制单声道、软静音、弱台削高音（HCC）、立体声噪声抑制（SNC）、去加重 50/75 µs、本振高/低边注入（HLSI）、硬件搜台（SM/SUD/SSL）、左右声道静音、standby。电平 ADC 只在写寄存器后更新，界面会周期性 `kickAdc()`。

### RDA5807M（FM + RDS）

| 项 | 值 |
|----|----|
| 总线 | I2C；顺序地址 **`0x10`**，随机地址 **`0x11`** |
| 口 | 同 TEA5767 |
| 频率单位 | 0.01 MHz |
| 频段 | 87–108 / 76–91 / 76–108 / 65–76 / 50–76 MHz |
| 步进 | 25 / 50 / 100 / 200 kHz |
| 音量 | 芯片 0–15 |
| 探测 | 读寄存器 `0x00`，芯片 ID 高字节为 `0x58` |
| 源码 | `include/rda5807m.h`、`src/rda5807m.cpp` |

功能：调谐 / 硬件搜台、静音、高阻输出、低音增强、软静音、弱立体声混单声道、AFC、RDS/RBDS（站名 PS、电台文本 RT、时钟 CT、备选频率 AF）、可选 I2S。I2C 扫描写探测可能误开射频，扫完会 `silence()`：写 `0x02 = 0`（ENABLE=0、音频高阻）。

`FmTuner::begin()` **先探 RDA，再探 TEA**。两颗同时在线时走 RDA。

### CC1101（433 MHz Sub-GHz）

| 项 | 值 |
|----|----|
| 总线 | SPI（非 I2C），库 [RadioLib](https://github.com/jgromes/RadioLib) |
| 口 | EXT14，见上表 |
| 默认频率 | 433.92 MHz |
| 可调范围 | 387.00–464.00 MHz，步进 0.25 MHz |
| 调制 | `begin(freq, 4.8 kbps, 4.8 kHz 频偏, 58 kHz RX BW, 10 dBm, 32 bit 前导)` |
| 源码 | `include/app_cc1101.h`、`src/app_cc1101.cpp` |

功能：初始化 / 再初始化、发测试包 `CP-<序号>`、监听约 3 s 收包、方向键改频率、500 ms 刷新 RSSI。无芯片显示 `NOT FOUND`。离开 App 时 `standby()`。

### I2C 扫描地址表（仅猜测）

扫描地址 **8–119**（`0x08`–`0x77`）。绿点=表内已知映射，灰点=未知（显示 `--` / `unknown`）。ExI2 表是常见 Grove / Unit **猜测**，不是驱动清单。固件真正驱动的外接芯片只有上列 FM 与 CC1101。

| 地址 | 猜测芯片 | 用途 |
|------|----------|------|
| `0x10` / `0x11` | RDA5807M | radio |
| `0x18` | ES8311 | codec |
| `0x23` | BH1750 | light |
| `0x26` | MiniScale | weight |
| `0x29` | VL53L0X | ToF |
| `0x34` | TCA8418 | keyboard |
| `0x3C` / `0x3D` | SSD1306 | OLED |
| `0x41` | 8Encoder | encoder |
| `0x43` | 8Angle | angle |
| `0x44` | SHT3x | ENV |
| `0x48` | ADS1115 | ADC |
| `0x50` | EEPROM | memory |
| `0x51` | BM8563 | RTC |
| `0x57` | UnitUS | sonar |
| `0x5A` | MLX90614 | NCIR |
| `0x5F` | CardKB | keyboard |
| `0x60` | TEA5767 | radio |
| `0x61` | PbHub | hub |
| `0x68` / `0x69` | BMI270 | IMU |
| `0x70` | QMP6988 | ENV |
| `0x76` / `0x77` | BMP280 | ENV |

板载确认（Hardware Test → InI2）：`0x18` ES8311、`0x34` TCA8418、`0x68` BMI270。

---

## App 层 API

入口约定与其它 App 相同：`enter*` / `leave*` / `update*` / `handle*`，Help 走 `close*` / `is*HelpVisible`。

### `app_ex_i2c`（集合）

```cpp
void enterExI2cApp();
void leaveExI2cApp();
void updateExI2cApp();
void handleExI2cApp(const Keyboard_Class::KeysState& status);
bool handleExI2cBack();          // 子应用内回 hub；已在 hub 返回 false
bool closeExI2cHelp();           // Radio / 扫描 / CC1101 / NFC / GPS Help 委托
bool isExI2cHelpVisible();
bool isExI2cRadioActive();       // 截图 slug
bool isExI2cCc1101Active();
```

`handleExI2cBack()` 在 `main.cpp` 里先于回主菜单调用。离开集合时会停 Radio / 静音总线上的 FM / 停 CC1101 / 停 NFC / 停 GPS。

### `app_radio`

```cpp
void enterRadioApp();
void leaveRadioApp();
void silenceFmRadioOnBus(m5::I2C_Class& bus); // 扫描写探测后 mute + standby
void updateRadioApp();
void handleRadioApp(const Keyboard_Class::KeysState& status);
bool closeRadioHelp();
bool closeRadioStations();
bool closeRadioSeek();
bool isRadioHelpVisible();
```

### `app_i2c_scan`

```cpp
void drawI2cScanApp(m5::I2C_Class& bus, const char* title, bool internal_bus);
void handleI2cScanApp(const String& key, m5::I2C_Class& bus,
                      const char* title, bool internal_bus);
bool closeI2cScanHelp(m5::I2C_Class& bus, const char* title, bool internal_bus);
bool isI2cScanHelpVisible();
void resetI2cScanHelp();
```

EX I2C 集合传入 `M5Cardputer.Ex_I2C, "ExI2", false`。Hardware Test 的 InI2 / ExI2 共用同一套函数。`drawI2cScanApp` 内部 `bus.scanID(found)`，随后 `silenceFmRadioOnBus(bus)`。

### `app_cc1101`

```cpp
void enterCc1101App();
void leaveCc1101App();
void updateCc1101App();
void handleCc1101App(const Keyboard_Class::KeysState& status);
bool closeCc1101Help();
bool isCc1101HelpVisible();
```

| 按键 | 作用 |
|------|------|
| `r` | 按当前频率重新 `begin` |
| `t` | 发送 `CP-<序号>` |
| `l` | `startReceive()`，约 3 s 超时 |
| 方向键 | ±0.25 MHz |
| `h` | 接线与按键 Help |

---

## 驱动 API

Radio App 经 `FmTuner` 门面调用两颗 FM 芯片；芯片专属能力仍从 `tea()` / `rda()` 取出。频率一律 **0.01 MHz**。

### `FmTuner`（`include/fm_tuner.h`）

```cpp
bool begin(m5::I2C_Class& bus);          // RDA → TEA
void detach();
static void silenceIfPresent(m5::I2C_Class& bus);

Chip chip() const;                       // None / Tea5767 / Rda5807m
bool isRda() const;
const char* chipName() const;

uint16_t freqMin() const;
uint16_t freqMax() const;
uint16_t freqStep() const;
void setFrequency(uint16_t freq_centi, bool wait_settle = true);
bool readStatus(Status& out);            // RSSI 统一到 0–15
void refreshSignal();                    // TEA：kickAdc

void setMute(bool on);
void setMono(bool on);
void setStandby(bool on);
void startSearch(bool up);
void abortSearch();

Tea5767& tea();
Rda5807m& rda();
```

`Status`：`freq_centi`、`rssi`（0–15）、`raw_rssi`、`quality`、`stereo`、`ready`、`band_limit`、`valid_station`、`rds_ready` / `rds_synced`。

TEA 专属 setter（RDA 模式下不写寄存器）：`setJapanBand`、`setDeemphasis75`、`setHighSideInjection`、`setSoftMute`、`setHighCut`、`setStereoNoiseCancel`、`setSeekStop`、`setChannelMute`。

### `Tea5767`

```cpp
bool begin(m5::I2C_Class& bus);
bool probe() const;
bool silence(m5::I2C_Class& bus);

void setFrequency(uint16_t freq_centi, bool wait_settle = true);
uint16_t getFrequency();
bool readStatus(Status& out);
void kickAdc();
void startSearch(bool up);
void abortSearch();

void setMute(bool on);
void setMono(bool on);
void setSoftMute(bool on);
void setHighCut(bool on);
void setStereoNoiseCancel(bool on);
void setJapanBand(bool on);
void setDeemphasis75(bool on);
void setHighSideInjection(bool on);
void setSeekStop(SeekStop level);     // Low / Mid / High
void setChannelMute(ChannelMute ch);  // Off / Left / Right
void setStandby(bool on);
void setPort1(bool high);
void setPort2(bool high);
```

### `Rda5807m`

```cpp
bool begin(m5::I2C_Class& bus, bool reset = true);
bool probe(uint16_t* chip_id = nullptr);
bool initialize();
bool softReset();
bool silence(m5::I2C_Class& bus);
bool setStandby(bool standby);

bool setBand(Band band);
bool setSpacing(Spacing spacing);
bool setFrequency(uint16_t freq_centi, bool wait = true, uint32_t timeout_ms = 500);
bool startTune(uint16_t freq_centi);
bool pollTune(Status* status = nullptr);
bool startSeek(bool up);
bool abortSeek();
bool waitSeek(uint32_t timeout_ms = 5000, Status* status = nullptr);

bool setMute(bool mute);
bool setMono(bool mono);
bool setHighZ(bool high_z);
bool setVolume(uint8_t volume);          // 0–15
bool setBass(bool enabled);
bool setDeemphasis50(bool use_50us);
bool setSoftMute(bool enabled);
bool setSoftBlend(bool enabled);
bool setAfc(bool enabled);
bool setSeekThreshold(uint8_t threshold);
bool setSeekWrap(bool wrap);
bool setRds(bool enabled);
bool setRbds(bool enabled);
bool pollRds(RdsGroup* group = nullptr);

const char* programService() const;      // PS
const char* radioText() const;           // RT
uint16_t programId() const;              // PI
```

诊断：`readRegister` / `writeRegister` 仅公开寄存器，写限制在 `0x02`–`0x08`。

### RadioLib `CC1101`

固件用法（不是完整 RadioLib 列表）：

```cpp
g_radio.begin(freq_mhz, 4.8f, 4.8f, 58.0f, 10, 32);
g_radio.setFrequency(freq_mhz);
g_radio.transmit(payload);
g_radio.startReceive();
g_radio.available();
g_radio.readData(buf, len);
g_radio.getRSSI();
g_radio.standby();
```

### `m5::I2C_Class`（总线）

全局：`M5Cardputer.Ex_I2C`、`M5Cardputer.In_I2C`。扫描与驱动都走这一层：

| 方法 | 用途 |
|------|------|
| `begin()` | 初始化 Grove / 内部口 |
| `isEnabled()` | 是否已 init |
| `scanID(bool result[120])` / `scanID(addr)` | 扫描或探单个地址 |
| `getSDA()` / `getSCL()` | 当前引脚 |
| `writeRegister*` / `readRegister*` | 寄存器读写 |

底层整理见仓库 `api/M5Unified.md` 的 I2C_Class 一节。

---

## 使用说明

1. 主菜单按 `e` 打开 EX I2C。暖绿卡片：`1` RADIO、`2` EXI2、`3` CC1101、`4` NFC、`5` GPS。
2. **收音机**：4 pin 成品板插左侧 Grove（G2=SDA、G1=SCL），耳机插模块孔。无芯片显示 `NO MOD`。操作见 [Radio](./radio)。
3. **扫描**：插上外设后进 EXI2，或 Hardware Test `8`。`r` 再扫。扫 FM 地址可能短暂出声，扫完会 mute + standby。
4. **CC1101**：3.3 V + EXT14 SPI。`r` 初始化，`t` 发测试包，`l` 听包，方向键改频率。
5. **NFC**：Unit NFC（ST25R3916）插左侧 Grove。见 [NFC](./nfc)。
6. **GPS**：AT6668 Unit 插左侧 Grove；App 把 G1/G2 切成 UART。见 [GPS](./gps)。
7. 退出子应用或整个集合时，FM / CC1101 / NFC / GPS 会停总线或 standby，避免占口。
