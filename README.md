# moerx

A deliberately small terminal multiplexer. One C file, no config file, no
dependencies.

```sh
make
./moerx            # session "0"
./moerx work       # separate named session
./moerx --help     # all shortcuts
```

Detach with `Ctrl-A d`. Run `moerx` again to reattach.

Every shortcut is a chain that starts with the `Ctrl-A` prefix: press `Ctrl-A`,
release it, then press the rest of the chain. `Ctrl-B` is not a prefix; it is
sent straight to the pane.

## What it does

- One persistent server per session (a Unix socket in
  `$XDG_RUNTIME_DIR/moerx-<uid>/` or `$TMPDIR/moerx-<uid>/`).
- Windows, panes, splits, resizing, renaming and a status line.
- Real ptys per pane, a built-in VT100/xterm-ish screen emulator (colors,
  256-color and truecolor SGR, alternate screen, scrolling, UTF-8, wide
  characters).
- Multiple clients can attach to the same session. The session is sized to the
  smallest attached client; a larger client shows the session centred in its
  terminal.
- Panes run `$SHELL` as a login shell with `TERM=screen-256color`.

## Keys

Prefix is `Ctrl-A`. Every shortcut below is `Ctrl-A`, then the key(s) shown.

| Chain | Action |
| --- | --- |
| `Ctrl-A a` | send a literal `Ctrl-A` to the pane |
| `Ctrl-A c`, `Ctrl-A Ctrl-C` | new window |
| `Ctrl-A n`, `Ctrl-A Space`, `Ctrl-A Ctrl-N`, `Ctrl-A Ctrl-@` | next window |
| `Ctrl-A p`, `Ctrl-A Ctrl-P`, `Ctrl-A Backspace` | previous window |
| `Ctrl-A Ctrl-A` | last window |
| `Ctrl-A w`, `Ctrl-A Ctrl-W`, `Ctrl-A "` | window menu (↑/↓, `j`/`k`, Enter) |
| `Ctrl-A A` | rename window (prompt) |
| `Ctrl-A 0` … `Ctrl-A 9` | select window by its number in the status line |
| `Ctrl-A -` | split top/bottom |
| `Ctrl-A \|`, `Ctrl-A %` | split left/right |
| `Ctrl-A h` `Ctrl-A j` `Ctrl-A k` `Ctrl-A l`, `Ctrl-A` ↑/↓/←/→ | select pane |
| `Ctrl-A Tab` / `Ctrl-A Shift-Tab` | next / previous pane |
| `Ctrl-A o` | next pane |
| `Ctrl-A Ctrl-H` `Ctrl-A Ctrl-J` `Ctrl-A Ctrl-K` `Ctrl-A Ctrl-L` | resize pane (8/5/5/8) |
| `Ctrl-A x`, `Ctrl-A Ctrl-X` | lock the client |
| `Ctrl-A K` | kill pane (confirm) |
| `Ctrl-A &` | kill window (confirm) |
| `Ctrl-A \` | kill server (confirm) |
| `Ctrl-A r` | show `Configuration Reloaded!` |
| `Ctrl-A *` | list attached clients |
| `Ctrl-A d`, `Ctrl-A Ctrl-D` | detach |

Two bindings worth noting: `Ctrl-A Ctrl-H`/`Ctrl-A Ctrl-L` resize the pane
(they do not switch windows), and `Ctrl-A k` selects the pane above while
`Ctrl-A K` kills it.

## Colors

Hard-coded:

- Status on top, default background, foreground `colour12`. The palette mirrors
  `~/.tmux.conf`.
- `[moerx]` on the left (`moerx` in `colour14`, brackets `colour12`).
- `[host] HH:MM:SS` on the right (`host` in `colour14`, brackets and clock
  `colour12`), refreshed every 2 seconds.
- Window list right-aligned: current name `colour11`, other names `colour12`,
  separators/flags `colour14`.
- Pane borders are thin box-drawing lines (`─`, `│`, `┬`, `┼`, ...) in
  `colour8`, drawn on the terminal's default background. The row above each
  pane doubles as its title bar. The `[pane: title]` label text uses the
  terminal's default background: brackets/numbers `colour7`, separator
  `colour11`, title `colour15` on the active pane; brackets/numbers `colour8`,
  separator `colour3`, title `colour12` on inactive panes.
- Messages use `fg=colour6,bg=colour8`.

Colours are written to the outer terminal in 256-colour form (`38;5;n` /
`48;5;n`). Truecolor from a pane is mapped to the nearest xterm-256 entry,
unless the outer terminal advertises `COLORTERM=truecolor` and is not
screen/tmux/linux.

The title is set to `[moerx] <window name>`.

## Deliberate limitations

This is "very basic" by design:

- No config file, no command prompt, no scripting, no plugins.
- No scrollback / copy mode (`[` does nothing), no zoom, no activity
  monitoring, no clock mode.
- The lock screen is a cosmetic client lock: press Enter to unlock. It is not a
  password lock.
- Mouse reporting is not negotiated with the outer terminal.
- Killing the server affects only the current session, since each named
  session has its own small server process.
- Key repeat is not implemented: one resize step per key press.
- New windows start in the directory moerx was started from, not in the
  current pane's directory.
- Resizing does not reflow existing text.

## Build notes

Requires a C99 compiler and POSIX. Tested on macOS with clang; the code also
builds with gcc on Linux.
