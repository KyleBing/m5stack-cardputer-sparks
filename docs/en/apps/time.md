# Time

Main menu key: `t`

Four sub-modes: **Uptime**, **Clock**, **Countdown**, **Stopwatch**. The app is always full-screen (no header); the mode name flashes briefly in the top-left when you switch.

## Screenshots

**Uptime / Clock / Countdown / Stopwatch**

<div class="shot-row">

![time-uptime](/shots/app_time_up_pure.png)
![time-clock](/shots/app_time_ntp_pure.png)
![time-countdown](/shots/app_time_cd_pure.png)
![time-stopwatch](/shots/app_time_sw_pure.png)

</div>

## Shortcuts

### Mode switch (Help summary)

| Key | Action |
|-----|--------|
| `u` | Uptime |
| `t` | Clock |
| `c` | Countdown |
| `s` | Stopwatch |
| `r` | Sync time / reset (depends on mode) |
| **BtnGO** | Start / pause / resume |
| `h` | Help |

### Uptime

| Key | Action |
|-----|--------|
| `h` | Help |

### Clock

| Key | Action |
|-----|--------|
| `r` | NTP sync (needs WiFi) |
| `b` | Toggle large dot-matrix clock (HH:MM only) |
| `h` | Help |

### Countdown · SETUP

| Key | Action |
|-----|--------|
| Arrow keys | Adjust h/m/s fields |
| `0`–`9` | Digit input |
| **BtnGO** | Start |
| `h` | Help |

### Countdown · running / paused

| Key | Action |
|-----|--------|
| **BtnGO** | Pause / resume |
| `r` | Reset |
| `h` | Help |

### Stopwatch

| Key | Action |
|-----|--------|
| **BtnGO** | Start / pause / resume |
| `r` | Reset |
| `h` | Help |

## Usage

1. Default mode comes from config `time.default` (e.g. `up`); timezone is `time.timezone` (e.g. `CST-8`).
2. Clock uses RTC; with network, `r` does NTP.
3. On Clock, `b` enters Big Clock (HH:MM dot-matrix only). After ~1 minute idle, the main loop slows to about one tick per second to save power.
4. **Countdown / Stopwatch** keep timing after leaving the Time App or switching sub-modes — see below.

## Background running

Uptime and Clock only refresh while Time is foreground; **Countdown** and **Stopwatch** state lives in memory and is **not cleared** when switching sub-modes or returning to the main menu.

### Countdown

| Scenario | Behavior |
|----------|----------|
| Running, switch to Clock / Uptime, etc. | Keeps timing; returning to Countdown shows remaining time |
| Running, back to main menu or another App | Keeps timing; **main loop** `pollCountdownBackground` detects expiry |
| Expires while not on Countdown page | **Auto-enters** Time App Countdown end page |
| Expiry alarm | Beep-beep-pause loop, up to **30s**; rings even off CD page (volume from `sound.volume`) |
| Stop alarm | On end page, `x` cancels and returns to setup; or `r` reset |

While PAUSED, remaining ms are saved; resume recomputes end time from `millis()`. Leaving the App does **not** call `leaveCountdownApp` to stop — only expiry, reset, or cancel alarm ends it.

### Stopwatch

| Scenario | Behavior |
|----------|----------|
| Running, switch sub-mode / main menu | `swRunning` and elapsed stay; continues from `millis()` |
| Re-enter Stopwatch | Shows correct elapsed (including time away) |
| Foreground refresh | While running, ~**30ms** refresh, display to **1ms** |
| Reset | Double-beep `r` clears |

No global expiry popup for stopwatch; screen does not refresh while away, but time still advances.

### Sub-mode switch

Within Time, `u` / `t` / `c` / `s` keep Countdown and Stopwatch **running state** (`enterCountdownApp` / `enterStopwatchApp` only redraw; they do not reset phase).
