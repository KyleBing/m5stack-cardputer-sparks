# Image processing and RGB565 baking

For Cardputer (ESP32-S3 + SPI RGB565 panel) firmware icons: sources stay PNG; at runtime prefer on-device baked `.rgb565` for direct push, avoiding `drawPngFile` decode every time.

> Relative timings below are order-of-magnitude experience, not precise benchmarks. Same-size comparisons use **RGB565 already in RAM → `pushImage`** as **1×**.

## Why

The panel is ultimately **RGB565**. Decoding PNG on every redraw means:

```
LittleFS read → zlib inflate → PNG filter restore → color/alpha → write screen
```

Slowness is mostly **CPU decode**, not the few dozen pixels. Frequent redraws (list paging, AC mode switch, Mijia grid) feel clearly laggy.

With pre-baked `.rgb565`:

```
LittleFS read raw pixels → pushImage (direct to screen)
```

No zlib, no live Alpha blend; Flash / SPI traffic is lighter too. Transparent areas are composited onto black at bake time (this project's UI is mostly black), so quality matches “decode PNG on screen with M5GFX”.

**Do not** offline-convert to 565 on a PC with Pillow etc.: decode/blend differs from LovyanGFX/pngle and can look dirty on device. Must decode with M5GFX on device, then export pixels.

## Image paths in the system

| Asset | Source (LittleFS) | Runtime preference | Draw entry |
|------|-------------------|------------|----------|
| Device icons | `/icon/device/*.png` (incl. `_active`, `_25w`) | `.rgb565` → PNG | `src/app_device_icons.cpp` |
| IR mode / fan speed / signal | `/icon/ir/*.png` | RAM cache → `.rgb565` → PNG | `src/app_ir.cpp` |
| Brand logos | `/icon/brand/*.png` | `.rgb565` → PNG | `src/app_ir.cpp` |
| Logo | `/logo_60.png`, `/logo_50.png` | `.rgb565` → PNG | `drawAppLogo60`, etc. |
| Arrows / WiFi / battery, etc. | no image files | vector draw | `src/app_icons.cpp` |

Config Web preview still uses **PNG** (browser-friendly); firmware draw prefers bake files.

Sources live in repo `data/`, uploaded via `pio run -t uploadfs` into device Flash. Bake outputs can be pulled back into `data/` and packaged so users need no on-device bake after flash.

## Bake process

### What the device does

```
fillRect black
  → drawPngFile (M5GFX decode + alpha blend onto black)
  → readRect (read back on-screen RGB565)
  → write same-name .rgb565
```

APIs: `bakePngToRgb565File` / `bakeAllPngIconsToRgb565` (`include/app_device_icons.h`).

Batch bake covers:

- every `.png` under `/icon`, including subdirectories
- `/logo_60.png`, `/logo_50.png`

### .rgb565 file format

8-byte header followed by raw pixels:

| Offset | Size | Content |
|--------|------|---------|
| 0 | 4 | magic `R565` |
| 4 | 2 | width (uint16 little-endian) |
| 6 | 2 | height (uint16 little-endian) |
| 8 | w×h×2 | RGB565 pixels, row by row |

Because the header carries the size, non-square icons (brand logos 66×20, fan speed 34×30, etc.) can be baked too. `loadRgb565File` validates the magic and dimensions, falling back to live PNG decode on mismatch.

Trigger: Config Web **`POST /bake-rgb565`** (returns `{"ok":true,"baked":N}`). The Icons App no longer offers key `b` for on-device bake.

### Recommended workflow

Prerequisites: firmware flashed, PNGs in LittleFS, device WiFi online (Config Web usable).

```bash
# 1. Upload data/ with PNGs (if you just changed images)
pio run -e m5stack-cardputer -t uploadfs

# 2. Bake on device, then pull back into data/
python scripts/pull_rgb565_from_device.py <device-IP> --bake
```

Bake only:

```bash
curl -X POST http://<IP>/bake-rgb565
```

Commit the pulled `.rgb565` into the repo so the next `uploadfs` ships bake files.

### Why this helps (summary)

1. **Faster refresh**: hot UI paths use `pushImage`, avoid repeated zlib decode.
2. **Reliable quality**: pixels from on-device M5GFX match real on-screen result.
3. **Fallback**: missing bake still decodes PNG; no need to bake immediately while iterating art.
4. **Reusable assets**: bake once into `data/`; every user flash gets the speedup.

## Draw path comparison

At the same size, typical ranking:

**RGB565 direct push ≫ ARGB8888 alpha blend ≫ decode PNG every time**

| Priority | Approach | Fit |
|--------|------|------|
| Fastest | Pre-bake **RGB565** (alpha onto black) → `pushImage` | Fixed black-bg UI icons (this project's main path) |
| Alpha over any bg | Pre-bake **ARGB8888** → `pushAlphaImage` | Semi-transparent / AA over varying backgrounds |
| Save Flash, easy edits | Keep **PNG**, **RAM cache** after first decode | Few icons, OK with first hitch |
| Avoid repeating | Every `drawPngFile` | List paging, key flash, other hot redraws |

### PNG (`drawPngFile`)

- With Alpha, M5GFX/LovyanGFX blends; usually best quality and most Flash-efficient.
- Doing the **full** path on **every** redraw gets noticeably slow.

### Pre-baked RGB565 (`.rgb565`)

- **No alpha channel**: transparent areas must be composited onto a fixed background first.
- Hand-rolled converters that mishandle Alpha/AA look worse than library-decoded PNG.

### Pre-baked ARGB8888 (`.argb8888`)

```
LittleFS read raw pixels → pushAlphaImage (read bg + blend + write back)
```

- **Skips zlib**, keeps full Alpha.
- Byte order must match `lgfx::argb8888_t`: **B, G, R, A**.
- Slower than RGB565 mainly due to **per-pixel Alpha**, then 4 bytes/pixel.
- This repo's firmware main path is RGB565 bake; treat ARGB as an alternative to understand.

### Decode PNG once + RAM cache

```
First: drawPngFile → readRect store RGB565
Later: pushImage
```

- Quality matches library decode; redraw near RGB565 direct push.
- RAM: `width × height × 2 × slots` (e.g. several 30×30 AC icons ~10+ KB).
- IR App pre-caches mode icons on enter, then `pushImage` on mode change to reduce flicker.

## Relative cost (same resolution)

| Path | Relative cost | Notes |
|------|----------|------|
| RGB565 in RAM → `pushImage` | **1×** | Fastest |
| RGB565 from Flash → `pushImage` | **~1.5–3×** | Extra FS read |
| ARGB8888 in RAM → `pushAlphaImage` | **~3–6×** | Alpha blend |
| ARGB8888 from Flash → `pushAlphaImage` | **~4–8×** | FS + Alpha |
| PNG every `drawPngFile` | **~10–40×** | zlib + decode, high variance |

### Absolute ballpark (feel)

**30×30:**

| Path | Approx |
|------|------|
| RGB565 `pushImage` (RAM) | &lt; 1 ms |
| ARGB8888 `pushAlphaImage` (RAM) | ~2–5 ms |
| PNG live decode | ~10–30+ ms |

**70×70:** roughly another **5–6×** (by area).

If a full page also clears, draws lots of text, and lays out, icon speedup may feel small — the bottleneck may not be icons.

## Flash / RAM footprint (same image)

Formula (uncompressed raw pixels):

- RGB565: `width × height × 2`
- ARGB8888: `width × height × 4`
- PNG: depends on compression; usually much smaller than raw

| Size | RGB565 | ARGB8888 |
|------|--------|----------|
| 25×25 | ≈ 1.25 KB | ≈ 2.5 KB |
| 30×30 | ≈ 1.8 KB | ≈ 3.6 KB |
| 60×60 | ≈ 7.2 KB | ≈ 14.4 KB |
| 70×70 | ≈ 9.8 KB | ≈ 19.6 KB |

This repo's LittleFS partition is about **4.69 MB** (`partitions/no_ota_8MB.csv`). A full device-icon set of raw pixels is on the order of hundreds of KB; keeping PNG + bake doubles that.

**RAM strategy:**

- **Few fixed icons** (e.g. AC modes): OK to keep cached.
- **Many device icons**: read from Flash on demand; do not keep the whole set resident.

## Selection guide

1. **Black UI, max refresh**: RGB565 bake + `pushImage` (RAM-cache current page if needed) — this project's default.
2. **Need transparency/AA on non-pure-black bg**: ARGB8888 + `pushAlphaImage`.
3. **Few images, less asset maintenance**: PNG + first-decode cache.
4. **Many large images in lists**: shrink first (e.g. `_25w`) + bake; then page-level cache, not a full-library resident set.

## Related APIs (M5GFX / LovyanGFX)

| API | Use |
|-----|------|
| `drawPngFile(FS, path, ...)` | Decode PNG from filesystem |
| `pushImage(x, y, w, h, rgb565*)` | Push RGB565, no Alpha |
| `pushAlphaImage(x, y, w, h, argb8888*)` | Push 32bpp with Alpha and blend |
| `readRect(...)` | Read back on-screen pixels for bake / decode cache |

## Related entry points

- Config: `POST /bake-rgb565` (see [Config](/en/apps/config))
- Script: `scripts/pull_rgb565_from_device.py`
- Implementation: `src/app_device_icons.cpp`, `src/app_ir.cpp`, `src/app_web.cpp`
