# Info

Main menu key: `i`

Read-only system info across 6 pages: Memory / Storage / Chip / Fw / Net / Run.

## Screenshots

**Memory / Storage**

<div class="shot-row">

![info-memory](/shots/app_info_memory.png)
![info-storage](/shots/app_info_storage.png)

</div>

**Chip / Firmware / Network / Runtime**

<div class="shot-row">

![info-chip](/shots/app_info_chip.png)
![info-fw](/shots/app_info_firmware.png)
![info-net](/shots/app_info_network.png)
![info-run](/shots/app_info_runtime.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| `[` `]` | Previous / next page |
| `c` (Storage only) | Clear all screenshots (`y` confirm / `n` cancel) |
| Arrow and other page keys | Page |

Footer shows `N/6` page index; Storage also shows `c clear`.

## Usage

| Page | Contents |
|------|----------|
| Memory | Heap / PSRAM / Sketch / LittleFS usage and bars |
| Storage | Local Flash (LittleFS) and TF used / free; shot count; `c` clears shots; n/a when no TF |
| Chip | Chip model and features |
| Fw | Firmware version, build time, etc. |
| Net | WiFi / IP / RSSI, etc. |
| Run | Uptime and related |

When debugging memory or WiFi issues, check Memory and Net first. Field meanings, allocation, and common shortage cases: [Memory Notes](/en/dev/memory).
