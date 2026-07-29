# Mic

Hardware Test key: `9` (main menu `h` → `9`)

Live oscilloscope waveform + VU meter + gain control. From v1.0.0, TF recording is **no longer** supported.

## Screenshots

<div class="shot-row">

![mic-001](/shots/app_mic_001.png)
![mic-002](/shots/app_mic_002.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| `-` `=` or `,` `.` | Gain − / + |
| `h` | Help (opens Help and mutes the mic; closing Help resumes) |

## Usage

1. Opening starts the mic and draws the waveform; watch ambient noise and gain to avoid clipping.
2. On exit, Speaker / I2S are released quietly to reduce hum.
3. Recording features are removed; use this App for levels and waveform only.
4. From a Hardware Test child, `ESC` / `GO` returns to the Test shelf.
