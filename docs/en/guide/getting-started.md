# Getting Started

Sparks is a multi-app firmware for the [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer). From the main menu, press a letter key to open the matching App; `ESC` / `GO` returns to the menu.

## Flash & Configure

1. Flash the firmware and LittleFS with PlatformIO or [M5Burner](https://docs.m5stack.com/en/download).
2. Copy from the repo-root `config.example.json`, put device lists, WiFi, Cursor token, etc. into `data/config.json`, then run `uploadfs` — or edit online via the **Config** App Web UI.
3. Power on to the **Sparks** main menu and press the matching letter to open a feature.

## Main Menu

<div class="shot-row">

![menu-page1](/shots/app_menu_page1.png)
![menu-page2](/shots/app_menu_page2.png)

</div>

| Key | Short | App | Key | Short | App | Key | Short | App |
|-----|-------|-----|-----|-------|-----|-----|-------|-----|
| `m` | Mij | [Mijia](/en/apps/mijia) | `u` | Cfg | [Config](/en/apps/config) | `w` | WiFi | [WiFi](/en/apps/wifi) |
| `t` | Time | [Time](/en/apps/time) | `s` | Slp | [Sleep](/en/apps/sleep) | `o` | Opt | [Options](/en/apps/options) |
| `i` | Inf | [Info](/en/apps/info) | `p` | Bat | [Battery](/en/apps/battery) | `c` | Cur | [Cursor](/en/apps/cursor) |
| `v` | Ver | [Version](/en/apps/version) | `j` | Mor | [Morse](/en/apps/morse) | `x` | IR | [Infrared](/en/apps/infrared) |
| `k` | KB | [Keyboard](/en/apps/hid-keyboard) | `g` | IMU | [IMU](/en/apps/imu) | `l` | LED | [RGB LED](/en/apps/rgb-led) |
| `r` | Mic | [Mic](/en/apps/mic) | `y` | FX | [Neon FX](/en/apps/neon-fx) | `z` | Dice | [Dice](/en/apps/dice) |
| `q` | Phys | [Newton Cradle](/en/apps/newton-cradle) |  |  |  |  |  |  |
| `b` | BLE | [BLE](/en/apps/ble) | `d` | Disp | [Display](/en/apps/display) | `a` | Icn | [Icons](/en/apps/icons) |
| `f` | Fnt | [Font](/en/apps/font) | `n` | InI2 | [InI2C](/en/apps/i2c) | `e` | ExI2 | [ExI2C](/en/apps/i2c) |

The menu paginates with the keyboard arrow keys; no **Fn** needed.

## Next Steps

- [App Catalog](/en/apps/) — browse all apps by category  
- [Global Shortcuts](/en/guide/shortcuts) — screenshots, paging, back, and more
