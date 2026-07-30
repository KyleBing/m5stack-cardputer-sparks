# Options

Main menu key: `o`

On-device settings are grouped into screen, sound, clock, calendar, and infrared modules. Changes write to `config.json` (volume is debounced to disk).

## Screenshots

**screen / sound / clock / infrared**

<div class="shot-row">

![options-screen](/shots/app_options_screen.png)
![options-sound](/shots/app_options_sound.png)
![options-clock](/shots/app_options_clock.png)
![options-ir](/shots/app_options_ir.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| ↑ ↓ | Switch module / row |
| ← → | Move focus between label and value |
| `-` `=` | Decrease / increase current value |
| `Tab` | Jump focus between confirm, value, etc. |

## Usage

Common items:

| Config path | Meaning |
|-------------|---------|
| `screen.brightness` | Backlight brightness |
| `screen.invert` | Screen invert (applies immediately) |
| `sound.time_key` | Time page key sound |
| `sound.mijia_on_off` | Mijia on/off beep |
| `sound.volume` | Speaker volume 0–100 |
| `time.default` / `time.pure` / `time.timezone` | Time default mode / Pure / POSIX timezone |
| `calendar.week_start` | Calendar week start: `sunday` / `monday` |
| `infrared.*` | IR default category and brand |

You can also edit the same config via [Config](./config) Web.
