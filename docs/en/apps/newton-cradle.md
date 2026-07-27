# Newton Cradle

Open with main menu `g`, then Games `5`.

A five-ball Newton's cradle simulation. The steel balls swing under gravity, transfer momentum through constrained impulse collisions, and lose energy gradually through light air damping and near-elastic impacts.

## Shortcuts

| Key | Action |
|-----|--------|
| `1` / `2` / `3` | Release that many balls from the left |
| `Space` / `GO` | Replay the current launch |
| `r` | Reset to a one-ball launch |

## Simulation and rendering

- Fixed-length pendulum constraints, gravitational angular acceleration, and time-based air damping.
- Contacts use constrained effective mass; alternating impulse passes propagate momentum through the row within one frame.
- Polished steel balls use per-pixel sphere-normal lighting, an environment reflection band, and specular highlights.
- Metal supports, twin suspension wires, a walnut base, and height-dependent workbench shadows complete the scene.
