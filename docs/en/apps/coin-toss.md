# Coin Toss

Open with main menu `g`, then Games `1`.

A coin-flip physics toy. Press Space or shake the device to toss; the coin flips in the air, bounces on landing, eases to heads or tails, then a gold banner shows `HEADS` / `TAILS`.

## Shortcuts

| Key | Action |
|-----|--------|
| `Space` / `GO` | Toss (ignored while airborne) |
| Shake device | Toss (IMU rising-edge trigger) |
| `h` | Help |

## Notes

- Front-on the coin is a true circle; only the horizontal axis compresses while flipping for a simple perspective cue.
- Heads shows a side-face silhouette; tails shows a radial emblem. The shadow shrinks and fades with height.
- The outcome is chosen when the toss starts; landing eases the coin onto that face.
- The top label shows `IMU` or `KEY` for the available input source (shake needs BMI270).
