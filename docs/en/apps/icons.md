# Icons

Open with main menu `h`, then Hardware Test `4`.

Browse firmware UI / Mijia device / IR AC icon assets (including off/on and fan-speed variants).

## Screenshots

<div class="shot-row">

![icons](/shots/app_icons.png)
![icons-ir-fan](/shots/app_icons_001.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| Arrow keys · `;,.` `/` | Previous / next page |
| `h` | Help |

## Usage

1. Page through icons packed into LittleFS.
2. Includes device icons and IR AC mode / fan-speed assets.
3. On-device RGB565 baking moved to Config Web `POST /bake-rgb565`; this App no longer offers `b` bake. Flow and performance notes: [Images & Baking](/en/dev/images).
