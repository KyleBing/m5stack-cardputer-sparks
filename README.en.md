# <img src="/design/logo_no_padding.png" width="50px"> Sparks

[中文](./README.md) | **English**

Personal firmware for Cardputer-ADV (**v1.11**), focused on Mi Home control, IR / AC automation, and Cursor usage info.

_The firmware UI is entirely in English, with many abbreviations — a solid English foundation helps a lot._


<img alt="2026-07-17  cardputer adv-24-2000x2000" src="https://github.com/user-attachments/assets/2e922a2a-303a-48e4-aa7e-3d736752aa22" />

<img width="1774" height="2591" alt="screenshots v1 10" src="https://github.com/user-attachments/assets/c3c07475-57c3-471c-a2ab-6f1c4f39d3f7" />




## 1. Features

Built on M5Stack libraries. Available apps:

On any screen, press `h` for Help if you're unsure how to use it.

| App | Name | Shortcut | Description |
|-----|------|----------|-------------|
| Mijia | Mijia | `m` | Device status & control, with hotkey quick switch |
| AP/LAN | Config | `u` | Web config server for editing `config.json` |
| WiFi | WiFi | `w` | WiFi setup with multiple saved profiles |
| Time | Time | `t` | Uptime, clock, stopwatch, countdown (fullscreen) |
| Sleep | Sleep | `s` | Light / deep sleep without powering off |
| Options | Options | `o` | Display, sound, clock, calendar, infrared prefs |
| Info | Info | `i` | Memory, storage, chip, firmware, network, runtime |
| Battery | Battery | `p` | Live battery level & 12h history chart |
| Cursor | Cursor Dashboard | `c` | Cursor info, token balance, usage (24h / 7d / 30d) |
| Calendar | Calendar | `a` | Month grid, browse months / years, today highlight |
| Version | Version | `v` | Firmware version & about |
| Morse | Morse | `j` | Keypress Morse code audio |
| Infrared | Infrared | `x` | TV & AC IR remote for major brands |
| AC Auto | AC Auto | `n` | BLE thermo/hygro → IR AC on/off automation |
| Keyboard | HID Keyboard | `k` | Bluetooth & USB keyboard |
| Mini Games | Mini Games | `g` | Minesweeper, Snake, Life, particle clock, and more (14) |
| Hardware Test | Hardware Test | `h` | Display / IMU / Font / Icons / LED / BLE / I2C / Mic |


## 2. Documentation

Full firmware docs:

- English: [Docs](https://kylebing.github.io/m5stack-cardputer-sparks/en/)
- 中文：[在线文档](https://kylebing.github.io/m5stack-cardputer-sparks/)

## 3. Flash layout & usage

Cardputer-ADV (StampS3) has **8 MB** on-chip Flash. The partition table is `partitions/no_ota_8MB.csv` in this repo (no OTA dual slot; same layout as the Release merged image).

| Partition | Offset | Size | Role |
|-----------|--------|------|------|
| bootloader | `0x0` | ~32 KB reserved | Boot |
| partitions | `0x8000` | 4 KB | Partition table |
| nvs | `0x9000` | 20 KB | NVS (WiFi, etc.) |
| otadata | `0xE000` | 8 KB | Bootloader compat (OTA unused) |
| **app0** | `0x10000` | **3.19 MB** (3264 KB) | Firmware (factory) |
| **spiffs / LittleFS** | `0x340000` | **4.69 MB** (4800 KB) | Filesystem (`config.json`, icons, logs, shots, …) |
| coredump | `0x7F0000` | 64 KB | Crash dump |

Flash offsets match Release notes: firmware `0x10000`, filesystem `0x340000`, full image `0x0`.

### Current usage (v1.11 local build, approximate)

| Item | Used | Partition | Usage |
|------|------|-----------|-------|
| Firmware (Sketch / app0) | ~**2.49 MB** (2551 KB) | 3.19 MB | **~78%** |
| LittleFS assets (`data/`) | ~**0.66 MB** (671 KB, ~244 files) | 4.69 MB | **~14%** |

Notes:

- Firmware size changes with features; on-device **Info → Memory** shows live Sketch / LittleFS bars.
- The LittleFS image fills the full 4.69 MB region; “used” above is source file bytes — FS metadata and runtime writes (config, logs, screenshots, game records) add more on device.
- After changing partitions, reflash the table + firmware + LittleFS (`upload` and `uploadfs`, or a full `merged.bin`); data from the old layout is not migrated.


## 4. Flashing

See the [Releases](https://github.com/KyleBing/m5stack-cardputer-sparks/releases) page.


## 5. Why Cardputer

I've always loved pixel displays — especially low-power monochrome ones, like old Nokias that rely on reflected light.  
For a while I wanted to build a small gadget with that kind of screen and some fun features. After doing the math, a watch would have been cheaper, so I dropped it.

The idea never really left. Chatting with Gemini pointed me toward M5Stack — first the [Stick](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit).  
The downside of such tiny devices is few buttons: navigating menus with a d-pad (or less) on cheap keys is tedious and feels awkward.  
Then I found the [Cardputer](https://shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3) — lots of keys compared to the Stick. Perfect.  
I've long liked BlackBerry-style full keyboards and letter-launch shortcuts (e.g. O for settings). With this many keys, Cardputer works great as an app launcher: one letter, one app, quick multi-app switching.  
Once I had the device, it felt like a foundation for the ideas I'd been sitting on — so I put my favorite tools and experiments on it, and keep adding new ones.

ESP32 tools like Cardputer run a fixed firmware image, so boot is fast — faster than Android or Linux, which I love. This firmware is tuned for that; cold boot is about **1 second**.

I especially like using it to control Mi Home devices at home — a better stand-in than Xiaoai for the things I care about.
