# Grove

Main menu key: `e`

External-module shelf: **I2C** (FM radio, NFC, bus scan) and **UART** (GPS; also Cap LoRa-1262 GNSS on Adv) on the left Grove / Cap bus. Up to 8 cards per page; number keys restart at `1` on the current page; letter shortcuts work **from any page**. `ESC` / `GO` returns a child to this shelf; press again on the shelf to return to the main menu.

This page covers **wiring, behavior, and source APIs** for chips the firmware already drives. Radio: [Radio](./radio). NFC: [NFC](./nfc). GPS: [GPS](./gps). Scan: [I2C Scan](./i2c). **CC1101** driver code remains, but the shelf entry is **hidden for now** (unfinished).

## Screenshots

**Shelf**

<div class="shot-row">

![exi2c-hub](/shots/app_exi2c_001.png)

</div>

## Children

| Key | Child | Bus | Chip | Role |
|-----|-------|-----|------|------|
| `1` / `r` | [RADIO](./radio) | Grove I2C | TEA5767 / RDA5807M | FM, seek, station list; RDA also has volume / RDS |
| `2` / `e` | [EXI2](./i2c) | Grove I2C | any ACK | List addresses with a likely chip name and role |
| `3` / `n` | [NFC](./nfc) | Grove I2C | ST25R3916 (Unit NFC) | Read / write 13.56 MHz, NDEF / stored-card emulate, history |
| `4` / `g` | [GPS](./gps) | Grove UART or Cap UART | AT6668 (Unit / Cap LoRa GNSS) | Live / speed / sky / record; auto-detect source |

## Shortcuts

| Key | Action |
|-----|--------|
| `1`–`8` | Open the child on the current page |
| `r` / `e` / `n` / `g` | Jump to RADIO / EXI2 / NFC / GPS |
| `[` `]` / arrows | Flip the shelf (when there are more than 8 items) |
| `ESC` / `GO` | Child → shelf → main menu |
| `h` | Help inside a child (the shelf has none) |

---

## Hardware

Cardputer-ADV exposes two common expansion ports. This shelf groups them, but **the protocols differ — do not mix 5 V onto a 3.3 V radio**.

### Left Grove (Ex_I2C)

HY2.0-4P rubber socket on the left with the screen facing you and the keyboard at the bottom. Firmware object: `M5Cardputer.Ex_I2C`. Related apps call `Ex_I2C.begin()` (boot only `setPort`s; without `begin` a Grove scan is empty).

| Order (top→bottom) | Pin | Role |
|--------------------|-----|------|
| 1 | GND | Ground |
| 2 | 5V | Power (for 5 V modules) |
| 3 | **G2** | **SDA** |
| 4 | **G1** | **SCL** |

Match FM modules by silkscreen **SDA / SCL / VCC / GND**, not by counting pins left-to-right. Audio comes from the **module 3.5 mm jack**, not the Cardputer speaker.

Fallback: if Grove has no FM chip, Radio retries `M5Cardputer.In_I2C` (EXT **G8=SDA / G9=SCL**).

### Rear EXT14 (CC1101 SPI)

Shares SPI with microSD; chip-select is separate. Firmware uses FSPI with `SPISettings(2 MHz, MSBFIRST, SPI_MODE0)`.

| Module pin | GPIO | Role |
|------------|------|------|
| VCC | **3.3 V** | 3.3 V only — **never 5 V** |
| GND | GND | Ground |
| CSN | **G13** | Chip select |
| SCK | **G40** | SPI clock |
| MOSI | **G14** | Host out |
| MISO | **G39** | Host in |
| GDO0 | **G15** | RX/TX done IRQ |
| GDO2 | **G5** | Optional carrier detect (wired; RST is NC) |

EXT14 `5VIN` / `5VOUT` are 5 V — do not use them as CC1101 VCC.

---

## Chips in this firmware

### TEA5767 (FM)

| Item | Value |
|------|-------|
| Bus | I2C address **`0x60`** |
| Port | Grove G2/G1 first, then In_I2C G8/G9 |
| Frequency unit | 0.01 MHz (`9850` = 98.50 MHz) |
| Bands | Europe 87.50–108.00 MHz; Japan 76.00–91.00 MHz |
| Step | 0.10 MHz |
| Volume | No chip volume — use the headphone knob |
| Probe | `scanID(0x60)`; writes a 5-byte register block |
| Source | `include/tea5767.h`, `src/tea5767.cpp` |

Features: tune, mute, force mono, soft mute, high-cut (HCC), stereo noise cancel (SNC), 50/75 µs de-emphasis, high/low-side injection (HLSI), hardware seek (SM/SUD/SSL), L/R channel mute, standby. Level ADC updates only after a register write; the UI periodically calls `kickAdc()`.

### RDA5807M (FM + RDS)

| Item | Value |
|------|-------|
| Bus | I2C sequential **`0x10`**, random **`0x11`** |
| Port | Same as TEA5767 |
| Frequency unit | 0.01 MHz |
| Bands | 87–108 / 76–91 / 76–108 / 65–76 / 50–76 MHz |
| Spacing | 25 / 50 / 100 / 200 kHz |
| Volume | Chip 0–15 |
| Probe | Read register `0x00`; chip-ID high byte is `0x58` |
| Source | `include/rda5807m.h`, `src/rda5807m.cpp` |

Features: tune / hardware seek, mute, output high-Z, bass boost, soft mute, soft blend, AFC, RDS/RBDS (PS, RT, CT, AF), optional I2S. An I2C scan write-probe can wake the RF; afterwards `silence()` writes `0x02 = 0` (ENABLE off, audio high-Z).

`FmTuner::begin()` **probes RDA first, then TEA**. If both sit on the bus, RDA wins.

### CC1101 (433 MHz Sub-GHz, shelf entry hidden)

Driver and EXT14 wiring remain, but the **Grove shelf card is hidden** (unfinished). Do not assume **G13 / G15** are free for CC1101 while Cap LoRa GNSS is in use.

| Item | Value |
|------|-------|
| Bus | SPI (not I2C), [RadioLib](https://github.com/jgromes/RadioLib) |
| Port | EXT14, table above |
| Default frequency | 433.92 MHz |
| Range | 387.00–464.00 MHz, 0.25 MHz step |
| Modulation | `begin(freq, 4.8 kbps, 4.8 kHz deviation, 58 kHz RX BW, 10 dBm, 32-bit preamble)` |
| Source | `include/app_cc1101.h`, `src/app_cc1101.cpp` |

Features (code present): init / re-init, send test packet `CP-<seq>`, listen ~3 s, arrow keys change frequency, RSSI refresh every 500 ms. No chip → `NOT FOUND`. Leaving the App calls `standby()`.

### I2C scan map (guesses only)

Scan range **8–119** (`0x08`–`0x77`). Green dot = known map, gray = unknown (`--` / `unknown`). The ExI2 table is a **likely Grove / Unit match**, not a driver list. External chips with real drivers are mainly the FM tuners, NFC, GPS (and the hidden CC1101).

| Address | Likely chip | Role |
|---------|-------------|------|
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

Onboard confirmed (Hardware Test → InI2): `0x18` ES8311, `0x34` TCA8418, `0x68` BMI270.

---

## App APIs

Same enter / leave / update / handle convention as other apps; Help uses `close*` / `is*HelpVisible`.

### `app_ex_i2c` (shelf)

```cpp
void enterExI2cApp();
void leaveExI2cApp();
void updateExI2cApp();
void handleExI2cApp(const Keyboard_Class::KeysState& status);
bool handleExI2cBack();          // child → hub; false if already on hub
bool closeExI2cHelp();           // delegates Radio / scan / CC1101 / NFC / GPS Help
bool isExI2cHelpVisible();
bool isExI2cRadioActive();       // screenshot slug
bool isExI2cCc1101Active();
```

`main.cpp` calls `handleExI2cBack()` before leaving for the main menu. Leaving the shelf stops Radio, mutes any FM on the bus, and stands by CC1101 / NFC / GPS.

### `app_radio`

```cpp
void enterRadioApp();
void leaveRadioApp();
void silenceFmRadioOnBus(m5::I2C_Class& bus); // mute + standby after a scan probe
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

The Grove shelf passes `M5Cardputer.Ex_I2C, "ExI2", false`. Hardware Test InI2 / ExI2 share the same functions. `drawI2cScanApp` runs `bus.scanID(found)` then `silenceFmRadioOnBus(bus)`.

### `app_cc1101`

```cpp
void enterCc1101App();
void leaveCc1101App();
void updateCc1101App();
void handleCc1101App(const Keyboard_Class::KeysState& status);
bool closeCc1101Help();
bool isCc1101HelpVisible();
```

| Key | Action |
|-----|--------|
| `r` | `begin` again at the current frequency |
| `t` | Send `CP-<seq>` |
| `l` | `startReceive()`, ~3 s timeout |
| Arrows | ±0.25 MHz |
| `h` | Wiring / key Help |

---

## Driver APIs

The Radio App talks to both FM chips through `FmTuner`; chip-specific extras still come from `tea()` / `rda()`. Frequencies are always **0.01 MHz**.

### `FmTuner` (`include/fm_tuner.h`)

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
bool readStatus(Status& out);            // RSSI scaled to 0–15
void refreshSignal();                    // TEA: kickAdc

void setMute(bool on);
void setMono(bool on);
void setStandby(bool on);
void startSearch(bool up);
void abortSearch();

Tea5767& tea();
Rda5807m& rda();
```

`Status`: `freq_centi`, `rssi` (0–15), `raw_rssi`, `quality`, `stereo`, `ready`, `band_limit`, `valid_station`, `rds_ready` / `rds_synced`.

TEA-only setters (no register writes in RDA mode): `setJapanBand`, `setDeemphasis75`, `setHighSideInjection`, `setSoftMute`, `setHighCut`, `setStereoNoiseCancel`, `setSeekStop`, `setChannelMute`.

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

Diagnostics: `readRegister` / `writeRegister` only for public registers; writes limited to `0x02`–`0x08`.

### RadioLib `CC1101`

What this firmware actually calls (not the full RadioLib surface):

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

### `m5::I2C_Class`

Globals: `M5Cardputer.Ex_I2C`, `M5Cardputer.In_I2C`. Scan and drivers both sit on this layer:

| Method | Use |
|--------|-----|
| `begin()` | Init Grove / internal port |
| `isEnabled()` | Whether init ran |
| `scanID(bool result[120])` / `scanID(addr)` | Full scan or single-address probe |
| `getSDA()` / `getSCL()` | Current pins |
| `writeRegister*` / `readRegister*` | Register access |

See `api/M5Unified.md` (I2C_Class) in the repo for the lower-level list.

---

## Usage

1. Main menu `e` opens Grove. Warm-green cards: `1` RADIO, `2` EXI2, `3` NFC, `4` GPS (CC1101 entry hidden for now).
2. **Radio**: plug a ready-made 4-pin board into the left Grove (G2=SDA, G1=SCL); headphones into the module jack. No chip → `NO MOD`. Keys: [Radio](./radio).
3. **Scan**: open EXI2 after plugging a device, or Hardware Test `8`. `r` rescans. Probing FM addresses may briefly unmute; the App mutes and standbys afterwards.
4. **NFC**: Unit NFC (ST25R3916) on the left Grove. See [NFC](./nfc).
5. **GPS**: Grove AT6668 Unit, or Adv + Cap LoRa-1262 GNSS; auto-detect on enter. See [GPS](./gps).
6. Leaving a child or the whole shelf stops FM / NFC / GPS so ports are not left busy.
