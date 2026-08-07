# Curves

Open with main menu `g`, then Games `7`.

Nine common curves sampled and polyline-drawn in real time. Adjust amplitude `a`, frequency `b`, and phase `p`, and toggle phase animation.

## Screenshots

<div class="shot-row">

![curves-sin](/shots/app_games_curves_sin.png)
![curves-lissa](/shots/app_games_curves_lissa.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| `1`–`9` | Select curve |
| `-` `=` | Amplitude `a` − / + (0.2–2.5) |
| `,` `.` | Frequency `b` − / + (0.2–4.0) |
| `q` `e` | Phase `p` − / + |
| `Space` / `GO` | Toggle animation (`RUN` / `PAUSE`) |
| `r` | Reset params and resume animation |
| `h` | Help |

## Curve list

| Key | Name | Formula |
|-----|------|---------|
| `1` | SIN | `a*sin(bx+p)` |
| `2` | COS | `a*cos(bx+p)` |
| `3` | PARA | `a*x^2` |
| `4` | CUBIC | `a*x^3` |
| `5` | EXP | `a*e^(bx)` |
| `6` | LOG | `a*ln(bx)` |
| `7` | CIRCLE | `x^2+y^2=a^2` |
| `8` | HEART | cardioid |
| `9` | LISSA | `a*sin(bt), a*sin(ct+p)` |

## Notes

- Top-left shows the curve name and formula; top-right shows params and `RUN` / `PAUSE`.
- With animation on, phase advances with frequency; axes are centered and the curve is drawn as a cyan polyline.
- Parametric curves (circle / heart / Lissajous) use denser sampling (180 points); function curves use 120.
