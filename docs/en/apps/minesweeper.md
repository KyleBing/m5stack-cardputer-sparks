# Minesweeper

Entry: main menu `g` → Games `8`

A full-rules Minesweeper with per-difficulty records for fastest clear and win streak. The first cell you dig and its eight neighbours are guaranteed mine-free, so every game opens with a cleared area and you can never lose on the first click.

## Shortcuts

| Key | Action |
|-----|--------|
| `;` `,` `.` `/` / arrows / `WASD` | Move the cursor (hold to repeat) |
| `Space` / `Enter` / `GO` | Dig; on a revealed number it performs a chord |
| `f` | Place / remove a flag |
| `1` `2` `3` | Switch to EASY / NORMAL / HARD (starts a new game) |
| `r` | New game |
| `b` | Open / close the records page |
| `h` | Help |
| `ESC` / `GO` | Back to the game shelf |

## Difficulties

| Level | Board | Mines | Density |
|-------|-------|-------|---------|
| EASY | 10 × 7 | 10 | 14.3% |
| NORMAL | 15 × 9 | 22 | 16.3% |
| HARD | 22 × 11 | 50 | 20.7% |

## How it plays

- **Safe first dig**: mines are placed after the first dig, excluding that cell and its neighbours, so the board always opens up.
- **Flood reveal**: digging a zero expands outwards to the number border. Flagged cells are never auto-revealed.
- **Chord**: put the cursor on a revealed number whose flag count matches it and press `Space` to open all remaining neighbours at once. This is the main speed-up move — and a misplaced flag will blow you up here.
- **Timer**: starts on the first dig, stops on win or loss, displays up to 999 seconds.
- **Loss**: the mine you hit turns red, every other mine is exposed, and wrong flags get a red cross.
- **Win**: remaining mines are auto-flagged and a golden banner appears, showing `NEW BEST!` when you beat your record.

## Records

Press `b` for the records page, listed per difficulty:

| Column | Meaning |
|--------|---------|
| BEST | Fastest clear in seconds, `--` if never cleared |
| WIN/PLAY | Games won / games played |
| STREAK | Longest win streak ever |

The bottom line shows the **current** streak for the selected difficulty, which resets to zero on a loss.

Records live in `/mines_rec.dat` on flash and survive reboots and firmware updates (only reflashing the filesystem clears them).
