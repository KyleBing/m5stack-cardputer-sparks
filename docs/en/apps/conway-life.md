# Conway Life

Entry: main menu `g` → Games page 2, `2`

Conway's Game of Life on a 60 × 30 toroidal grid (edges wrap around), running the standard B3/S23 rules. Cells are coloured by age — near white when just born, dimmer the longer they survive — so movement and trails read at a glance.

## Screenshots

<div class="shot-row">

![life-01](/shots/app_games_life_01.png)
![life-02](/shots/app_games_life_02.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| `Space` / `GO` | Run / pause |
| `n` | Advance a single generation |
| `r` | Random soup (~30% density) |
| `c` | Clear |
| `1`–`6` | Load pattern: glider / glider gun / pulsar / LWSS / R-pentomino / acorn |
| `;` `,` `.` `/` / arrows / `EASD` | Move the cursor (auto-pauses for editing) |
| `Enter` | Toggle the cell under the cursor |
| `-` `=` | Speed level 1–5 |
| `h` | Help |
| `ESC` / `GO` | Back to the game shelf |

## Rules

Every generation updates at once, based only on how many of the 8 neighbours are alive:

| Now | Live neighbours | Next |
|-----|-----------------|------|
| Alive | < 2 | Dies (underpopulation) |
| Alive | 2 or 3 | Survives |
| Alive | > 3 | Dies (overcrowding) |
| Dead | = 3 | Born |

## Built-in patterns

| Key | Pattern | Behaviour |
|-----|---------|-----------|
| `1` | GLIDER | Smallest spaceship, crawls diagonally |
| `2` | GUN | Gosper glider gun, period 30, emits gliders forever |
| `3` | PULSAR | Large period-3 oscillator |
| `4` | LWSS | Lightweight spaceship, flies horizontally |
| `5` | R-PENT | Five cells that take 1103 generations to settle |
| `6` | ACORN | Seven cells that explode into a thousand generations |

## Notes

- The top bar shows generation `G`, population `P`, the current pattern name and the run state.
- A fully static board is marked `STILL`, a period-2 loop `OSC`, and an empty board `DEAD`. Evolution stops automatically once still or dead.
- Because the edges wrap, a glider leaving the right side comes back on the left, so patterns on a small grid usually end up interfering with themselves.
