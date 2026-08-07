# Config

Main menu key: `u`

Via the device SoftAP or a connected WiFi, the device becomes a Web server. On the same LAN, a computer can open the config page at the Cardputer IP or AP address.

You can manage devices, groups, WiFi, brightness, IR defaults, screenshot download, and more. Settings are saved in `config.json` on the device Flash storage.

## Screenshots

**LAN / AP / Help**

<div class="shot-row">

![config-lan](/shots/app_config_001.png)
![config-ap](/shots/app_config_002.png)
![config-help](/shots/app_config_003.png)

</div>

## Shortcuts

| State | Key | Action |
|------|------|------|
| Connecting / AP | `a` | Skip LAN, switch to AP hotspot |
| LAN ready | `a` | Switch to AP hotspot mode |
| Failed | Re-enter `u` | Retry |
| Any | `h` | Help |
| In Help | `a` | AP |
| In Help | `l` | Retry LAN |

Back to menu: `ESC` / `GO`.

## Usage

1. On enter, try saved WiFi for LAN first; on failure or `a`, start SoftAP.
2. The screen shows the IP or hotspot SSID; open that address in a phone / computer browser.
3. Common Web entry points:
   - Device and group editing
   - `/wifi`: up to 5 WiFi profiles and Active
   - `/shots`: screenshot preview, download, delete one, clear TF / Flash
   - `/about`: firmware version
   - RGB565 bake: `POST /bake-rgb565` (generate icon bake files on device; see [Image processing and baking](/en/dev/images))
4. Saves write to LittleFS; some items (invert, volume) take effect immediately.

After setup, press `ESC` back to the menu, then use [Mijia](./mijia) / [WiFi](./wifi).


## Mijia device management

The Web tool can also add device info manually and set device groups.

<img alt="web-config-mijia-devices" src="https://github.com/user-attachments/assets/63a85026-83ec-458f-b945-0bfe19dd0c49" />

<img alt="web-config-mijia-device-group" src="https://github.com/user-attachments/assets/4a143e74-a2f3-445f-8ee7-0cc6ec53f8c5" />
