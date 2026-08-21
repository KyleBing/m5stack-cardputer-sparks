# Global Shortcuts

Most screens share the keys below. Each App may also have its own shortcuts — see that App’s doc page.

## Navigation

| Key | Action |
|-----|--------|
| Letter keys (main menu) | Open the matching App |
| `ESC` / `GO` (side button) | Return to main menu; if Help is open, close Help first (Keyboard exits with a **long-press BtnGO**) |
| `h` | Open / close Help on any screen (Keyboard: `Fn` + `h`) |
| `;` `,` / ↑ / ← | Previous page |
| `.` `/` / ↓ / → | Next page |
| `[` `]` | Page some lists / grids (see each App tip / Help) |

## Screenshots

| Key | Action |
|-----|--------|
| `Fn` + `s` | Save the current screen as PNG |

- Prefer TF card; otherwise Flash LittleFS: `/shot/<group>_<app>_<feature>.png` (e.g. `exi2c_gps_live.png`; duplicates get `_002`)
- When space is low, existing shots are **not** deleted; the device shows `no space for shot`
- On success the screen flashes once (invert); beep is controlled by Options → Sound → `screenshot` (`sound.screenshot`)
- Preview, download, delete one, or clear TF / Flash shots at `/shots` in [Config](/en/apps/config) Web
- Info → Storage: press `c` to clear all shots (`y` confirm / `n` cancel)

## Top Bar

The main menu and most Apps show WiFi / BLE status, battery, page dots, and similar info in the top bar. Back always uses `ESC` / `GO` — headers no longer draw a return icon.

## Sound Feedback

Some actions (e.g. Mijia on/off, Time key presses, screenshots) can enable/disable beeps via [Options](/en/apps/options) or Config Web, and adjust speaker volume with `sound.volume` (0–100).
