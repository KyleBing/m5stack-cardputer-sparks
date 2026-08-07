# Snake

Entry: main menu `g` → Games page 2, `1`

Classic Snake on a 40 × 20 grid. The body fades from bright head to dark tail so the direction of travel is obvious, and every fifth normal fruit spawns a timed golden one. Wall mode and wrap mode keep separate high scores.

## Screenshots

<div class="shot-row">

![snake](/shots/app_games_snake.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| `;` `,` `.` `/` / arrows / `EASD` | Steer (also starts the game) |
| `Space` / `GO` | Pause / resume; replay after game over |
| `r` | New game |
| `m` | Toggle WALL (die on impact) / WRAP (pass through) — starts a new game |
| `i` | Toggle IMU tilt steering (also recenters the neutral pose) |
| `-` `=` | Speed level 1–5 |
| `h` | Help |
| `ESC` / `GO` | Back to the game shelf |

## How it plays

- **Scoring**: a normal fruit is +1, a golden fruit is +5. Golden fruit lasts 7 seconds and blinks during the last 2.
- **Ramp-up**: each normal fruit shortens the step interval by 3ms down to a 55ms floor. `-` / `=` sets the starting level.
- **Turn buffer**: two direction presses inside one step are queued, so sharp right-angle turns are never swallowed. Reversing 180° is blocked.
- **Top bar**: `S` score, `L` length, `B` best score for the current mode, plus mode and speed level on the right. A gold `IMU` badge lights up next to the best score while tilt steering is active; on boards without an IMU, pressing `i` briefly shows a red `NO IMU`.

## IMU tilt steering

Press `i` to steer by tilting the device. Keyboard steering keeps working, so you can mix both.

- **Neutral pose**: the pose you hold at the moment you press `i` becomes the "centre", so the device does not need to be level. If you change how you hold it, press `i` twice to recalibrate.
- **All four directions have symmetric range**: detection uses the angle the gravity vector has rotated away from the calibrated pose, not the raw reading of a single axis. Even with a steeply held device, tilting further the same way never runs into the ±1g end of the range, so every direction stays reachable.
- **Direction mapping**: tilt up to go up, tilt right to go right. Screen-right is derived from "screen-up × the gravity direction at calibration" instead of being hard-coded to a body axis.
- **Sensitivity**: a turn needs roughly 9° of rotation and only releases below roughly 4.5° (hysteresis), and one axis has to dominate — this keeps a diagonal grip from flapping between directions.
- **Timing**: a turn fires only when the tilt direction changes, so holding a tilt does not retrigger. Tilting on the start screen also starts the game.
- **Tilt dot**: the small box right of the `IMU` badge holds a dot that follows your tilt, so you can confirm all four directions register.

## Records

High scores are stored per mode in `/snake_rec.dat` on flash — one for WALL, one for WRAP. The start screen shows the best score and total games played.
