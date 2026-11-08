# moerx project instructions

moerx is a deliberately small terminal multiplexer: one C99 source file
(`moerx.c`), no dependencies, no config file.

## Build

```sh
make          # cc -O2 -std=c99 -Wall -Wextra -o moerx moerx.c
make clean
```

There is no test suite. Build must stay warning-free except for the known
pre-existing `g_spawn_err` unused-variable warning. Use a throwaway harness
outside the repo for geometry/logic checks; do not leave test files behind.

## Conventions

- Keep changes inside `moerx.c` where possible; the single-file design is
  intentional.
- `README.md` is user-facing documentation. Update it when shortcuts, behavior,
  or limitations change.
- Match the existing style: 4-space indentation, C99 declarations, `/* */`
  comments.
- Do not remove existing user changes in the working tree; check `git diff`
  before editing and keep unrelated edits intact.

## Layout model (important for navigation/resizing)

- Windows use a binary `Layout` tree: `L_COLS` (left/right split) or
  `L_ROWS` (top/bottom split), with `Layout` x/y/w/h plus a `Pane` leaf.
- `layout_apply()` assigns pane content rectangles. Each pane has a title row
  above its content, so `pane->y = layout->y + 1`.
- Splits reserve one border column/row between children, so pane rectangles do
  not touch; vertical stacks have a two-row visual gap including the title row.
- Pane movement must be based on rectangle edges, not centres. A pane is only
  to the right if `p->x >= a->x + a->w` (and similar for the other directions).
  Centre comparison wrongly classifies tall/wide panes that merely extend past
  the active pane.

## Current work (2026-10-30)

Reworked `select_pane_dir()` in `moerx.c` so `Ctrl-A h/j/k/l` and the arrow
keys choose the pane exactly left/down/up/right:

1. Only panes strictly beyond the active pane's edge in the requested direction
   are candidates.
2. Prefer candidates that overlap on the perpendicular axis.
3. Then choose the smallest edge distance in the direction of travel.
4. Break ties by perpendicular gap, then centre alignment.

Verified with a temporary randomized split-layout simulation: the old
centre-based algorithm chose a pane outside the requested direction in many
cases; the new one never did and always preferred an overlapping candidate when
one existed. `make` succeeds with only the pre-existing warning above.

### Terminal resize robustness

The client now reconciles its size with `TIOCGWINSZ` every `SIZE_CHECK_MS`
(250 ms) instead of only reacting to `SIGWINCH`. Some terminals update the pty
size without delivering a signal, or deliver it before the new size is
visible; the periodic check corrects both. Keep the SIGWINCH self-pipe and flag
as the low-latency path. The session size is set by the most recently attached
or resized client, so reattaching from a larger terminal grows the session even
if an older client is still connected; smaller clients show the top-left part.
Use `Ctrl-A *` to list attached clients when diagnosing size reports.
