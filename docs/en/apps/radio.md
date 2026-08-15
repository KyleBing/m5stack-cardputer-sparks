# Radio

Main menu key: `r`

FM radio on the left Grove port. Buy a ready-made **TEA5767** or **RDA5807M** 4-pin module (I2C `0x60` or `0x10`/`0x11`); both share one UI. Audio comes from the **module headphone jack**.

On enter it probes left Grove (Ex_I2C). No chip → `NO MOD`.

## Screenshots

**Playing / no module / station list**

<div class="shot-row">

![radio-playing](/shots/app_radio_playing.png)
![radio-no-module](/shots/app_radio_no_module.png)
![radio-stations](/shots/app_radio_station_list.png)

</div>

## Module & wiring

Buy a **ready-made 4-pin board**. Neither TEA5767 nor RDA5807M needs soldering. Search for `TEA5767` or `RDA5807M` and pick a module that already brings out **SDA / SCL / VCC / GND** and has a 3.5 mm headphone jack. Plug it in with Dupont or a Grove jumper.

Do not buy a bare board that still needs headers, an antenna, or an audio jack soldered on.

### Which connector

Use the **left-side Grove** (HY2.0-4P), not the rear EXT header. With the screen facing you and the keyboard at the bottom, that 4-pin rubber socket is **Ex_I2C**. Top → bottom:

| Order (top→bottom) | Pin | Role |
|--------------------|-----|------|
| 1 | GND | Ground |
| 2 | 5V | Power |
| 3 | **G2** | **SDA** |
| 4 | **G1** | **SCL** |

### Four pins

Pin order on the module varies by seller. Match **silkscreen names** to the left Grove; do not count left-to-right:

| Module 4-pin | Left Grove |
|--------------|------------|
| **SDA** | **G2** |
| **SCL** | **G1** |
| **VCC** / VDD | **5V** |
| **GND** | **GND** |

Headphones go in the module’s 3.5 mm jack, not the Cardputer speaker. The antenna is usually already on the board. [ExI2C](./i2c) should then show TEA5767 at `0x60`, or RDA5807M at `0x10` / `0x11`.

> TEA5767 has no chip volume — use the headphone volume. RDA5807M: main-screen `-=` or Tuner → Volume.

## Shortcuts

Full list: `h` Help (multi-page).

| Key | Action |
|-----|--------|
| `h` | Help |
| `←` `→` | Step tune / stop scan |
| `↑` `↓` | Seek / flip direction |
| `-` `=` | RDA: volume · TEA: step tune |
| `[` `]` | Previous / next saved station |
| `a` | Auto scan and save |
| `m` / `o` | Mute / force mono |
| `l` | Station list |
| `t` | Open / close Tuner |
| `i` | RDS info (RDA only) |
| `1`–`0` | Jump to station slot |

In the list: arrows select, `Enter` tunes and exits, `r` rename, `n` add current freq, `d` / Backspace delete, `p` pin to top.

## Tuner

From the main radio screen press **`t`** to open Tuner (settings list); press `t` or `ESC` again to return. Changes apply to the chip immediately and are saved.

| Key | Action |
|-----|--------|
| ↑ ↓ / `;` `.` etc. | Move between items |
| `Enter` / `Space` / `=` | Next value |
| `-` | Previous value |
| `t` | Close Tuner |
| `h` | Tuner Help |

### TEA5767 items

| Item | Values | Meaning |
|------|--------|---------|
| Band | EU / JP | EU 87.5–108 MHz; JP 76–91 MHz |
| Deemph | 50us / 75us | De-emphasis: EU/JP 50µs, North America 75µs |
| Seek | Soft / Chip | Soft = software step seek; Chip = hardware SM seek |
| Stop | Lo / Mid / Hi | Hardware seek stop threshold (SSL; higher = pickier) |
| Inject | High / Low | Local-oscillator injection side (HLSI); flip if a station is noisy |
| SMute | On / Off | Soft mute on weak stations |
| HiCut | On / Off | High-cut on weak stations (HCC) |
| SNC | On / Off | Stereo noise cancelling |
| MuteLR | Off / L / R | Mute left or right channel |

### RDA5807M items

Extra bands, step, volume, and RDS vs TEA:

| Item | Values | Meaning |
|------|--------|---------|
| Band | EU / JP / Wide / East / Low | EU 87–108; JP 76–91; Wide 76–108; East 65–76; Low 50–76 |
| Step | 100k / 200k / 50k / 25k | Tune step |
| Deemph | 50us / 75us | Same as TEA |
| Seek | Soft / Chip | Same as TEA |
| SeekTh | 0–15 | Hardware seek threshold |
| Wrap | On / Off | Wrap at band edges while seeking |
| Volume | 0–15 | Chip volume |
| Bass | On / Off | Bass boost |
| SMute | On / Off | Soft mute on weak stations |
| SBlend | On / Off | Blend weak stereo toward mono |
| RDS | On / Off | European RDS |
| RBDS | On / Off | North-American RBDS |
| AFC | On / Off | Auto frequency control; leave On |

Main screen `i` shows RDS (PS / RT, …); RDA only.

## Usage

1. Plug the ready-made 4-pin module into the **left Grove** (G2=SDA, G1=SCL); headphones into the module jack.  
2. Open with `r`. Header shows the chip name; dial green = saved station, cyan = active.  
3. Seek with arrows, or `a` for auto scan; `l` to manage stations.  
4. Press `t` for Tuner (band / seek mode — tables above).  
5. Leaving the App puts the chip in standby.
