# Neon FX

Open with main menu `g`, then Games `6`.

A high-frame-rate neon animation demo. Full-screen 8-bit palette Sprite: the pixel-index field is generated only when changing the pattern or moving the core; normal frames rotate the 256-color palette and push the canvas to the display. Also includes a software-rendered rotating cube (`CUBE`).

## Screenshots

<div class="shot-row">

![neonfx-01](/shots/app_games_neonfx_01.png)
![neonfx-02](/shots/app_games_neonfx_02.png)
![neonfx-03](/shots/app_games_neonfx_03.png)
![neonfx-04](/shots/app_games_neonfx_04.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| `e` `a` `s` `d` | Move core / orbit cube |
| `c` | Cycle color themes |
| `m` or `,` `.` | Cycle Vortex / Plasma / Tunnel / Cube |
| `-` `=` | Decrease / increase speed |
| `r` | Reverse flow / rotation |
| `Space` / `GO` | Trigger a bright pulse |
| `h` | Help (includes measured FPS) |

## Notes

- No header / tip bar — animation fills 240×135; measured FPS overlays the top-left corner.
- The full-screen Sprite uses about 32 KB and is released immediately on exit.
- `CUBE` is a DIY perspective projection + painter's algorithm fill — no external 3D library.
- Press `ESC` / `GO` to return to the main menu.
