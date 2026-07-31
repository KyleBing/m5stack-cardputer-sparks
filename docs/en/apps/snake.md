# Snake

Entry: main menu `g` → Games page 2, `1`

Classic Snake on a 40 × 20 grid. The body fades from bright head to dark tail so the direction of travel is obvious, and every fifth normal fruit spawns a timed golden one. Wall mode and wrap mode keep separate high scores.

## Shortcuts

| Key | Action |
|-----|--------|
| `;` `,` `.` `/` / arrows / `WASD` | Steer (also starts the game) |
| `Space` / `GO` | Pause / resume; replay after game over |
| `r` | New game |
| `m` | Toggle WALL (die on impact) / WRAP (pass through) — starts a new game |
| `-` `=` | Speed level 1–5 |
| `h` | Help |
| `ESC` / `GO` | Back to the game shelf |

## How it plays

- **Scoring**: a normal fruit is +1, a golden fruit is +5. Golden fruit lasts 7 seconds and blinks during the last 2.
- **Ramp-up**: each normal fruit shortens the step interval by 3ms down to a 55ms floor. `-` / `=` sets the starting level.
- **Turn buffer**: two direction presses inside one step are queued, so sharp right-angle turns are never swallowed. Reversing 180° is blocked.
- **Top bar**: `S` score, `L` length, `B` best score for the current mode, plus mode and speed level on the right.

## Records

High scores are stored per mode in `/snake_rec.dat` on flash — one for WALL, one for WRAP. The start screen shows the best score and total games played.
