# Sleep

Main menu key: `s`

Countdown confirmation before entering light (Light) or deep (Deep) sleep.

## Screenshots

<div class="shot-row">

![sleep-light](/shots/app_sleep_light.png)
![sleep-deep](/shots/app_sleep_deep.png)

</div>

## Shortcuts

| State | Key | Action |
|-------|-----|--------|
| Light countdown | **BtnGO** | Wake immediately / cancel entry (per UI); memory is not released |
| Light countdown | `s` | Switch to deep sleep |
| Deep countdown | **BtnGO** | Wake note: waking from deep sleep reboots |
| Any countdown | `ESC` / `GO` | Cancel and return to main menu |

## Usage

1. Opening starts a **Light** countdown; press `s` during it to switch to **Deep**.
2. **Light sleep**: wakes quickly; good for short power saving.
3. **Deep sleep**: saves more power; wake reboots into the main flow.
4. Press `ESC` before the countdown ends to abort sleep.
