# NFC

Open from: main menu `e` → [Grove](./ex-i2c) `4` / `n`

**Unit NFC (ST25R3916)** on the left Grove: read / write 13.56 MHz cards, NDEF / stored-card emulation, and read history. Uses `Ex_I2C` (G2=SDA, G1=SCL), same port as Radio / I2C scan.

> Requires **Unit NFC**, not Unit RFID (U031 / `0x28`). 13.56 MHz only; protected blocks need matching keys.

## Screenshots

**Reader / Dump**

<div class="shot-row">

![nfc-main](/shots/app_exi2c_nfc_main.png)
![nfc-read](/shots/app_exi2c_nfc_read.png)
![nfc-dump](/shots/app_exi2c_nfc_dump.png)

</div>

**History / Detail / Write / Emulate**

<div class="shot-row">

![nfc-history](/shots/app_exi2c_nfc_history.png)
![nfc-detail](/shots/app_exi2c_nfc_detail.png)
![nfc-write](/shots/app_exi2c_nfc_write.png)
![nfc-emu](/shots/app_exi2c_nfc_emulate.png)

</div>

**Write / Emulate setup**

<div class="shot-row">

![nfc-write-setup](/shots/app_exi2c_nfc_write_setup.png)
![nfc-write-type](/shots/app_exi2c_nfc_write_type.png)
![nfc-emu-setup](/shots/app_exi2c_nfc_emu_setup.png)
![nfc-emu-type](/shots/app_exi2c_nfc_emu_type.png)

</div>

**Write UID / NDEF**

<div class="shot-row">

![nfc-write-uid](/shots/app_exi2c_nfc_write_uid.png)
![nfc-write-ndef](/shots/app_exi2c_nfc_write_ndef.png)

</div>

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
| Reader | Left status icon; right UID / ATQA/SAK / type / USR/TOT; `blk` grid aligned with the bar (green=ok / red=fail); dump pages list readable blocks only |
| History | Up to 12 recent reads (GPS-style list + scrollbar) |
| Detail | Full record; write back or emulate |
| Rename | Edit a history name |
| Emulation | Default NDEF text tag, or full Type2 clone from history |

## Shortcuts

Full list: `h` Help (multi-page).

| Key | Action |
|-----|--------|
| `h` | Help |
| `r` | Reader: scan until a card is found; History: rename selection |
| `w` | Write current payload to a card (also from Detail) |
| `o` | Toggle “save reads to history” |
| `l` | Open / close History |
| `e` | Reader: default NDEF text emulate; History / Detail: emulate selected record (UID + full dump); again to leave |
| ↑ ↓ etc. | Page Reader dump; select History row |
| `Enter` | History → Detail |
| `ESC` | Emulation / Detail → History → Reader (then back to Grove) |

Default write payload is `Cardputer NFC` (NDEF Text). Reads / writes fail on protected sectors without the right keys.

On a **MIFARE Classic** read, the app first authenticates with default KeyA `FFFFFFFFFFFF` and reads **blk 4** (sector 1, block 0), then continues with a full dump:

| Result | Meaning |
|--------|---------|
| `default key` | Sector still uses the factory default key |
| `Auth Error` | Key was changed (common on property access cards) |
| `n/a (not Classic)` | Not Classic; probe skipped |

The status line and the `key` row both show this result, and it is stored in History.

## Emulation limits

- **Supported**: MIFARE Ultralight family and NTAG 2xx (Type 2). Reads store a full page image; History / Detail `e` replays the original UID + memory.
- **Not supported**: MIFARE Classic / Plus / DESFire (library listener is Type 2 only). Those records show `type not emulatable`.

## Usage

1. Plug Unit NFC into the left Grove; main menu `e` → `n` (or `4`).  
2. Press `r` and hold a card; inspect UID / type / key probe / NDEF, arrows for dump pages.  
3. `o` controls auto-history; `l` to browse, `Enter` for detail, `r` rename, `w` write back, `e` emulate.  
4. Reader `e` starts the default text tag; History / Detail `e` clones a stored Type2 card; `e` or `ESC` leaves emulation.  
5. Leaving the App stops scan / emulation and releases I2C.
