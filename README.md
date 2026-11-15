# moerx

A deliberately small terminal multiplexer. One C file, no config file, no
dependencies.

```sh
make -B CC=clang
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
- Multiple clients can attach to the same session. The most recently attached
  or resized client sets the session size; a larger client shows the session
  centred in its terminal, while a smaller client shows the top-left part.
  `Ctrl-A R` forces the session back to the current client's terminal size.
- Panes run `$SHELL` as a login shell with `TERM=screen-256color`.
- Window names show `hostname:program` for the foreground program in their
  active pane (for example `laptop:zsh` or `laptop:vim`), refreshed
  every second, including in inactive windows. When a program exits, the name
  returns to the shell. For pipelines, the foreground process group leader
  supplies the name. Linux uses `/proc`; other systems use `ps`.

## SSH window names

While an SSH pane is active, the outer terminal's window title shows
`[moerx] remote-hostname:program`, for example `[moerx] server:vim`.
The moerx window list and pane title also show `server:vim`. Starting another
remote program updates the title, and returning to the prompt shows the
remote shell name, for example `[moerx] server:zsh`.
The remote shell must send an OSC 0 or OSC 2 terminal title in
that format: SSH itself does not report the remote foreground program.
Without a matching title, the window name remains `local-hostname:ssh`.
Names refresh within one second; disconnecting restores the local hostname
and shell name in the terminal title and window list.

For **zsh**, add this to `~/.zshrc` on the **remote host**:

```zsh
if [[ -n $SSH_CONNECTION ]]; then
    autoload -Uz add-zsh-hook
    moerx_ssh_title() {
        local program=${1:t}
        printf '\033]2;%s:%s\007' "${HOST%%.*}" "$program"
    }
    moerx_ssh_preexec() {
        local -a words
        words=(${(z)1})
        moerx_ssh_title "${(Q)words[1]}"
    }
    moerx_ssh_precmd() { moerx_ssh_title zsh; }
    add-zsh-hook preexec moerx_ssh_preexec
    add-zsh-hook precmd moerx_ssh_precmd
fi
```

This reports the command launched by the shell (the first command for a
pipeline), then `hostname:zsh` at the prompt. moerx retains the latest
`hostname:program` for that SSH job when programs send other terminal titles,
so these cannot reset the window name to `local-hostname:ssh`.
Other shells can use equivalent
hooks sending `printf '\033]2;server:vim\007'`; use a hostname containing only
letters, digits, dots, underscores or hyphens, and a program basename without
spaces or colons. With splits, the window follows its active pane.

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
| `Ctrl-A A` | temporarily rename window (until the next automatic name refresh) |
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
| `Ctrl-A R` | force the session to this client's terminal size |
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

The terminal window title is set to `[moerx] hostname:program`, following the
active moerx window. Hostnames use their short form (before the first dot).

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
builds with gcc on Linux and with clang on FreeBSD.
