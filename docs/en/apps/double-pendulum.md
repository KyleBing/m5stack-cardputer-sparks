# Double Pendulum

Open with main menu `g`, then Games `2`.

A coupled double-pendulum chaos demo. A lighter tip mass and longer second arm make the path diverge more easily; the tip trail is drawn as a fading color ribbon.

## Screenshots

<div class="shot-row">

![chaos](/shots/app_games_chaos.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| `Space` / `GO` | Reset to the default pose |
| `r` | Randomize angles and clear the trail |
| `-=` | Adjust second-arm length (top label shows `L2`) |
| `h` | Help |

## Notes

- Uses the classic double-pendulum angular accelerations with semi-implicit integration and light damping to avoid energy blow-ups on bad frames.
- Multiple substeps per frame; the tip trail is a ring buffer of about 72 points.
- The near bob is larger and the tip bob smaller to match the mass ratio; the top label always shows `CHAOS`.
