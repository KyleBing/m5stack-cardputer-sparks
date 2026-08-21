# NFC

Open from: main menu `e` → [EX I2C](./ex-i2c) `4` / `n`

**Unit NFC (ST25R3916)** on the left Grove: read / write 13.56 MHz cards, NDEF tag emulation, and read history. Uses `Ex_I2C` (G2=SDA, G1=SCL), same port as Radio / I2C scan.

> Requires **Unit NFC**, not Unit RFID (U031 / `0x28`). 13.56 MHz only; protected blocks need matching keys.

## Module & wiring

Plug into the **left Grove** (HY2.0-4P). Match silkscreen **SDA / SCL / VCC / GND**:

| Module | Left Grove |
|--------|------------|
| **SDA** | **G2** |
| **SCL** | **G1** |
| **VCC** | **5V** |
| **GND** | **GND** |

On enter the Unit is initialized; failure shows `unit begin failed`. Do not share the Grove with GPS (GPS reuses the same pins as UART).

## Views

| View | Role |
|------|------|
| Reader | Status / UID / type / card info / NDEF; dump pages after a read |
| History | Up to 12 recent reads (LittleFS) |
| Detail | Full record; can write back to a card |
| Rename | Edit a history name |
| Emulation | Emulate an NDEF tag (default text `Cardputer NFC`) |

## Shortcuts

Full list: `h` Help (multi-page).

| Key | Action |
|-----|--------|
| `h` | Help |
| `r` | Reader: scan until a card is found; History: rename selection |
| `w` | Write current payload to a card (also from Detail) |
| `o` | Toggle “save reads to history” |
| `y` | Open / close History |
| `e` | Enter / leave NDEF emulation |
| ↑ ↓ etc. | Page Reader dump; select History row |
| `Enter` | History → Detail |
| `ESC` | Detail → History → Reader (then back to EX I2C) |

Default write payload is `Cardputer NFC` (NDEF Text). Reads / writes fail on protected sectors without the right keys.

## Usage

1. Plug Unit NFC into the left Grove; main menu `e` → `n` (or `4`).  
2. Press `r` and hold a card; inspect UID / type / NDEF, arrows for dump pages.  
3. `o` controls auto-history; `y` to browse, `Enter` for detail, `r` rename, `w` write back.  
4. `e` starts tag emulation (phone NFC can read the default text); `e` again returns to Reader.  
5. Leaving the App stops scan / emulation and releases I2C.
