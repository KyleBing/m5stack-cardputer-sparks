# Global Shortcuts

Most screens share the keys below. Each App may also have its own shortcuts — see that App’s doc page.

## Navigation

| Key | Action |
|-----|--------|
| Letter keys (main menu) | Open the matching App |
| `ESC` / `GO` (side button) | Return to main menu (Keyboard exits with **BtnGO**) |
| `;` `,` / ↑ / ← | Previous page |
| `.` `/` / ↓ / → | Next page |
| `[` `]` | Page some lists / grids (see each App tip / Help) |

## Screenshots

| Key | Action |
|-----|--------|
| `Fn` + `s` | Save the current screen as PNG |

- Prefer TF card; otherwise Flash LittleFS: `/shot/app_<screen>_NNN.png`
- When space is low, the oldest shots are deleted automatically
- On success the screen flashes once (invert); beep is controlled by Options → Sound → `screenshot` (`sound.screenshot`)
- Preview, download, or clear TF / Flash shots at `/shots` in [Config](/en/apps/config) Web

## Top Bar

The main menu and most Apps show WiFi / BLE status, battery, page dots, and similar info in the top bar. Back always uses `ESC` / `GO` — headers no longer draw a return icon.

## Sound Feedback

Some actions (e.g. Mijia on/off, Time key presses, screenshots) can enable/disable beeps via [Options](/en/apps/options) or Config Web, and adjust speaker volume with `sound.volume` (0–100).
