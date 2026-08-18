# App Catalog

Browse by main-menu category. Each App page includes: **overview & screenshots**, **shortcuts**, and **usage notes**.

Screenshot naming: `docs/public/shots/app_{app}_{feature}.png` (captured on device with `Fn+s`). Full gallery: [Screenshot Gallery](/en/apps/shots) (wide page, no sidebar).

## Basics

Most screens share the keys below. App-specific keys are on each App page. Full list: [Global Shortcuts](/en/guide/shortcuts).

| Key | Action |
|-----|--------|
| Letter keys | Open the matching App from the main menu |
| `ESC` / `GO` (side button) | Return to the main menu; if Help is open, close Help first |
| `h` | Open / close Help on any screen |
| `;` `,` / ↑ / ← | Previous page |
| `.` `/` / ↓ / → | Next page |
| `[` `]` | Page some lists / grids |
| `Fn` + `s` | Screenshot |

Exception: [Keyboard](./hid-keyboard) sends `` ` `` to the host as Esc and exits with a **long-press BtnGO**; Help is `Fn` + `h` so it is not typed to the host.

## Main Menu

More detail: [Getting Started · Main Menu](/en/guide/getting-started#main-menu).



<div class="shot-row">

![menu-001](/shots/app_menu_001.png)
![menu-002](/shots/app_menu_002.png)
![menu-003](/shots/app_menu_003.png)

</div>

## Smart Home

| Key | App | Description |
|-----|-----|-------------|
| `m` | [Mijia](./mijia) | Local LAN Mijia control |
| — | [Get Device Token](./mijia-token) | Export token / ble.key via cloud tools into `config.json` format |
| `x` | [Infrared](./infrared) | TV / AC IR remote (GPIO44) |
| `n` | [AC Auto](./ac-auto) | BLE HT sensor triggers IR AC on/off |

## Network & Config

| Key | App | Description |
|-----|-----|-------------|
| `u` | [Config](./config) | Edit firmware `config.json` via AP or LAN Web |
| `w` | [WiFi](./wifi) | Connect, scan, switch saved WiFi profiles (up to 5) |

## Time & Power

| Key | App | Description |
|-----|-----|-------------|
| `t` | [Time](./time) | Clock tools: Uptime / Clock / countdown / stopwatch |
| `a` | [Calendar](./calendar) | Full-month grid; month / year nav; today highlight |
| `p` | [Battery](./battery) | Live charge level and ~1-hour history chart |
| `s` | [Sleep](./sleep) | Enter light / deep sleep |

## Productivity

| Key | App | Description |
|-----|-----|-------------|
| `c` | [Cursor](./cursor) | Cursor usage Summary, day / week / month charts |
| `k` | [Keyboard](./hid-keyboard) | USB / BLE HID keyboard |
| `j` | [Morse](./morse) | Morse tones, symbol highlighting, and live waveform |

## System & Info

| Key | App | Description |
|-----|-----|-------------|
| `o` | [Options](./options) | System settings: brightness, sound, time, infrared |
| `i` | [Info](./info) | Memory / storage / chip / firmware / network / runtime info |
| `v` | [Version](./version) | Version / about |

## Hardware Debug & Demos

| Key | App | Description |
|-----|-----|-------------|
| `g` | [Mini Games](./mini-games) | [Coin](./coin-toss), [pendulum](./double-pendulum), [wheel](./prize-wheel), [Dice](./dice), [Newton cradle](./newton-cradle), [Neon FX](./neon-fx), [curves](./curves), [Minesweeper](./minesweeper), [Snake](./snake), [Conway Life](./conway-life), [MATRIX](./matrix), [WAVE](./wave), [PCLOCK](./particle-clock), [LISSA](./lissa) |
| `h` | [Hardware Test](./hardware-test) | [Display](./display), [IMU](./imu), [Font](./font), [Icons](./icons), [LED](./rgb-led), [BLE](./ble), [I2C](./i2c), page-2 [Mic](./mic) |
| `e` | [EX I2C](./ex-i2c) | [Radio](./radio) (TEA5767 / RDA5807M), ExI2 scan, CC1101 433 MHz |

<div class="shot-row">

![exi2c-hub](/shots/app_exi2c_001.png)
![radio-playing](/shots/app_radio_playing.png)
![radio-no-module](/shots/app_radio_no_module.png)

</div>
