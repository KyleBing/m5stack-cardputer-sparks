# Keyboard

Main menu key: `k`

Use Cardputer as a **USB** or **BLE** HID keyboard / mouse. This App **cannot** return to the menu with `ESC` — use **BtnGO**.

No header on the main input screen. Left pill column shows Fn / Aa / Opt / Ctrl / Alt; center shows a large last-key echo; footer shows link status, green device name, and a yellow slot badge.

## Screenshots

**USB / BLE**

<div class="shot-row">

![hid-usb](/shots/app_hidkeyboard_005.png)
![hid-ble](/shots/app_hidkeyboard_003.png)
![hid-ble-paired](/shots/app_hidkeyboard_002.png)

</div>

**Hosts list**

<div class="shot-row">

![hid-hosts-empty](/shots/app_hidkeyboard_001.png)
![hid-hosts](/shots/app_hidkeyboard_004.png)
![hid-hosts-rename](/shots/app_hidkeyboard.png)

</div>

**Help**

<div class="shot-row">

![hid-help](/shots/app_hidkb_001.png)

</div>

## Shortcuts

### Mode and exit

| Key | Action |
|------|------|
| `Fn` + `u` | USB HID |
| `Fn` + `b` | BLE HID |
| **Hold `Fn`** | Toggle IMU→mouse pointer |
| `Fn` + `p` | BLE host list (switch / pair) |
| **BtnGO** | Exit to main menu (and disconnect BLE) |
| `Fn` + `h` | Help |

### IMU mouse (hold Fn)

Letters are used for clicks (not typed). Digits set sensitivity. Other function keys still go to the host.

| Key | Action |
|------|------|
| Left-half letters `qwerty asdfg zxcv` (`ygv` left) | Left click |
| Right-half letters `uiop hjkl bnm` (`uhb` right) | Right click |
| `1`–`9` / `0` | Sensitivity 1..10 |

Center shows a mouse icon; right side is the sensitivity bar. If BLE pointer does nothing, forget the device on the host and re-pair.

### BLE host list (`Fn+p`)

Stores up to **5** paired hosts; only one connected at a time.

| Key | Action |
|------|------|
| `1`–`5` / `;` `,` `.` `/` | Select slot |
| Enter / Space | Switch to that host (disconnect current; on the target, tap `Cardputer KB` in Bluetooth) |
| `n` | New pairing (needs free slot; rejects reconnect from old hosts) |
| `r` | Rename current slot alias (Enter saves; empty name shows MAC again; `` ` `` cancels) |
| `d` | Delete current slot pairing |
| `p` / `h` | Close list |

### Fn layer (Help page 2)

| Key | Action |
|------|------|
| `` ` `` (bare) | Esc (to host) |
| `Fn` + Backspace | Delete |
| `Fn` + `;` `,` `.` `/` | Arrow keys |
| `Fn` + `1`–`0` | F1–F10 |
| `Fn` + `-` `=` | F11 / F12 |
| `Fn` + `A`/`a` | CapsLock |
| `Fn` + modifiers | Right-side modifier mapping (see Help) |

## Usage

1. Use a cable for USB, or `Fn+b` for BLE keyboard then pair on the host (`Fn+p` → `n`).
2. Multiple hosts: `Fn+p` opens the list, select a slot, Enter to switch. `reconnecting #N` means waiting for the target PC to auto-reconnect (usually within a few seconds; otherwise tap `Cardputer KB` in Bluetooth again).
3. New pairing with `n`: rejects old hosts grabbing the link, then search/pair on the new PC.
4. Type normal characters directly; bare `` ` `` sends Esc; other function keys use the Fn layer.
5. Hold `Fn` for IMU mouse; `ygv` left / `uhb` right click; `1`–`0` set sensitivity; other function keys still work.
6. Always exit with **BtnGO** (disconnects BLE); `` ` `` is Esc to the host.
7. BLE combo keyboard+mouse: after firmware update, forget `Cardputer KB` and re-pair if the pointer does nothing.
