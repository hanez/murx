# moerx

A deliberately small terminal multiplexer. One C file, no config file, no
dependencies.

```sh
make -B CC=clang
./moerx                            # session "0"
./moerx work                       # separate named session
./moerx -p x work                  # use Ctrl-X as this client's prefix
./moerx --prefix x work            # same as -p x
./moerx -s bottom work             # show the session status line at the bottom
./moerx --status bottom work       # same as -s bottom
./moerx --no-cwd work              # show [moerx] instead of the directory
./moerx --no-cwd --no-moerx work   # do not show [moerx] instead of the directory
./moerx --no-clock work            # hide the status bar clock
./moerx --no-hostname work         # hide the hostname: prefix in window titles
./moerx --help                     # all shortcuts
```

Detach with `Ctrl-A d`. Run `moerx` again to reattach.

Every shortcut is a chain that starts with the prefix (by default `Ctrl-A`):
press it, release it, then press the rest of the chain.

Use `-p x` or `--prefix x` to choose Ctrl-X instead; `--prefix` is the long
form of `-p`. Letters `a` through `z` select Ctrl-A through Ctrl-Z,
case-insensitively. The choice applies to this attached client; use the option
again when reattaching, or set a shell
alias such as `alias moerx='moerx -p x'`. Clients attached to the same session
can choose different prefixes.

With `-p x`, detach with `Ctrl-X d`, send a literal Ctrl-X with `Ctrl-X a`,
and switch to the last window with `Ctrl-X Ctrl-X`. Doubling the prefix takes
priority over any other control-key binding; `Ctrl-X x` still locks the client.
Ctrl-A is sent straight to the pane. All tables below show the default prefix;
replace it with your chosen key. When upgrading from a version without this
option, restart the session server once so it understands the prefix setting.

Use `-s top` or `-s bottom` (also `--status top|bottom`) to choose the status
line position. New sessions default to top. An explicit choice also updates an
existing session for all attached clients; omitting the option preserves its
position. Prompts and messages follow the status line. Restart servers from
older versions once before using this option.

Use `--no-cwd` to show `[moerx]` on the left of the status bar instead of the
current directory, and `--no-clock` to hide the clock on the right. Use
`--no-moerx` to hide `[moerx]` when using `--no-clock` (and `--no-cwd`). Use
`--no-hostname` to hide the `hostname:` prefix in window names and the outer
terminal title. These options apply when starting a new session.

## What it does

- One persistent server per session (a Unix socket in
  `$XDG_RUNTIME_DIR/moerx-<uid>/` or `$TMPDIR/moerx-<uid>/`).
- Windows, panes, splits, resizing, renaming and a status line.
- Real ptys per pane, a built-in VT100/xterm-ish screen emulator (colors,
  256-color and truecolor SGR, alternate screen, scrolling, UTF-8, wide
  characters).
- Application cursor-key mode follows the active pane, so arrow navigation
  works in programs such as Midnight Commander. Menus and prompts use normal
  cursor-key mode; detaching restores normal cursor keys in the outer terminal.
- Multiple clients can attach to the same session. The most recently attached
  or resized client sets the session size; a larger client shows the session
  centred in its terminal, while a smaller client shows the top-left part.
  `Ctrl-A R` forces the session back to the current client's terminal size.
- Panes run `$SHELL` as a login shell with `TERM=screen-256color`.
  They inherit the launching process's umask, so builds keep their usual file
  permissions. After upgrading from a version that forced `077`, restart the
  session server to apply the fix. In an existing shell, `umask 022` restores
  typical public file permissions for subsequent commands, including builds
  published with `rsync -a`.
- Window names show `hostname:program` for the foreground program in their
  active pane (for example `laptop:zsh` or `laptop:vim`), refreshed
  every second, including in inactive windows. With `--no-hostname`, they show
  only `program`. When a program exits, the name returns to the shell. For
  pipelines, the foreground process group leader supplies the name. Linux uses
  `/proc`; other systems use `ps`.

## SSH window names and directories

While an SSH pane is active, the outer terminal's window title shows
`[moerx] remote-hostname:program`, for example `[moerx] server:vim`.
The moerx window list also shows `server:vim`. Starting another
remote program updates the title, and returning to the prompt shows the
remote shell name, for example `[moerx] server:zsh`.
The remote shell must send an OSC 0 or OSC 2 terminal title in
that format: SSH itself does not report the remote foreground program.
Without a matching title, the window name remains `local-hostname:ssh`.
Names refresh within one second; disconnecting restores the local hostname
and shell name in the terminal title and window list.

### Zsh

For **zsh**, add this to `~/.zshrc` on the **remote host**:

```zsh
if [[ -n $SSH_CONNECTION ]]; then
    autoload -Uz add-zsh-hook
    moerx_ssh_title() {
        local program=${1:t}
        printf '\033]2;%s:%s\007' "${HOST%%.*}" "$program"
        local directory=$PWD
        if [[ $directory == "$HOME" ]]; then
            directory='~'
        elif [[ $directory == "$HOME/"* ]]; then
            directory="~/${directory#"$HOME/"}"
        fi
        printf '\033]777;moerx-cwd;%s\007' "$directory"
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
The hook also reports the remote working directory before each command and at
each prompt, so the status bar follows remote `cd` commands and shows `~` for
the remote home directory. Disconnecting restores the local path. Restart
existing moerx servers and reload the remote shell configuration after upgrading.

### Bash

For **Bash**, add this to `~/.bashrc` on the **remote host**:

```bash
if [[ $- == *i* && -n $SSH_CONNECTION && -z ${moerx_ssh_installed-} ]]; then
    moerx_ssh_title() {
        local program=$1 directory=$PWD
        program=${program##*/}
        printf '\033]2;%s:%s\007' "${HOSTNAME%%.*}" "$program"
        if [[ $directory == "$HOME" ]]; then
            directory='~'
        elif [[ $directory == "$HOME/"* ]]; then
            directory="~/${directory#"$HOME/"}"
        fi
        printf '\033]777;moerx-cwd;%s\007' "$directory"
    }
    moerx_ssh_preexec() {
        if [[ ${moerx_ssh_ready-0} == 1 && $1 != moerx_ssh_precmd ]]; then
            moerx_ssh_ready=0
            local program rest
            read -r program rest <<< "$1"
            # Strip quotes around a simple command name without evaluating it.
            program=${program#\"}; program=${program%\"}
            program=${program#\'}; program=${program%\'}
            moerx_ssh_title "$program"
        fi
        return 0
    }
    moerx_ssh_precmd() {
        local last_status=$?
        moerx_ssh_title bash
        moerx_ssh_ready=1
        return "$last_status"
    }
    # Keep existing prompt commands, and run our prompt hook last.
    if [[ $(declare -p PROMPT_COMMAND 2>/dev/null) == 'declare -a '* ]]; then
        PROMPT_COMMAND+=(moerx_ssh_precmd)
    else
        PROMPT_COMMAND=("${PROMPT_COMMAND-}" moerx_ssh_precmd)
    fi
    trap 'moerx_ssh_preexec "$BASH_COMMAND"' DEBUG
    moerx_ssh_installed=1
fi
```

This updates `hostname:program` before a command, restores `hostname:bash`
at the prompt, and reports the remote directory with `$HOME` shortened to
`~`. The hooks print only invisible terminal updates. Source `~/.bashrc` or
open a new SSH shell after installing them.

The Bash snippet preserves string and array forms of `PROMPT_COMMAND` and
avoids duplicate hooks when sourced again. It installs a `DEBUG` trap; if your
shell configuration already uses one (for example through bash-preexec),
integrate these functions with that framework instead of replacing its trap.
Command names come from the first whitespace-delimited word of Bash's first
simple command; aliases, compound commands, assignments and quoted executable
paths containing spaces may display differently from the Zsh hook. The command
text is never evaluated by the hook. Do not enable function tracing (`set -T`)
for these hooks.

moerx accepts standard OSC 7 reports (`ESC ] 7 ; file://host/path BEL`) too,
including percent-encoded paths. These display the reported absolute path;
`OSC 777;moerx-cwd;path` accepts either an absolute path or a path starting with
`~` and preserves that abbreviation. Reports belong to the foreground job
that sent them, so an old SSH connection cannot override the local shell's path.

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
| `Ctrl-A Page Up`, `Ctrl-A [` | enter scrollback (Page Up also scrolls one page) |
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

## Scrollback

Press `Ctrl-A`, then `Page Up` to scroll back in the active pane. Use
`Page Up` and `Page Down` without the prefix to move through history.
`Ctrl-A [` opens scrollback at the current screen. Press `q`, Escape,
Enter or Ctrl-C to return to live output.

Each pane keeps up to 2,000 lines of normal screen history. The view is frozen
while browsing; programs keep running and new output continues to be saved.
Scrollback is private to each client. Changing the active pane or window closes
it. Alternate-screen programs such as editors do not add history; opening
scrollback there shows only their current screen. History is kept in memory,
retains colors, and is clipped or padded to the pane width without reflow.
Restart an existing session server after upgrading to enable this feature.

## Colors

Hard-coded:

- Status on top by default (`-s bottom` moves it), default background, foreground `colour12`. The palette mirrors
  `~/.tmux.conf`.
- `[/path/to/directory]` on the left (directory in `colour14`, brackets
  `colour12`), refreshed every second and when switching panes. On Linux this
  follows the active pane's foreground process, falling back to its shell's
  directory; on other systems it shows the session's starting directory.
  Paths in `$HOME` use `~`, for example `[~/code/moerx]`; `$HOME` itself
  appears as `[~]`. Long paths end in `...` to leave room for the window list and clock; the
  directory label is hidden when fewer than four columns remain. Remote SSH
  directories require a shell report; see [SSH window names and directories](#ssh-window-names-and-directories).
- `HH:MM:SS` on the right in `colour12`, refreshed every second.
- Window list right-aligned: current name `colour11`, other names `colour12`,
  separators/flags `colour14`.
- Pane borders are thin box-drawing lines (`─`, `│`, `┬`, `┼`, ...) in
  `colour8`, drawn on the terminal's default background. Only splits have
  borders; panes have no title row, leaving more space for terminal content.
- Messages use `fg=colour6,bg=colour8`.

Colours are written to the outer terminal in 256-colour form (`38;5;n` /
`48;5;n`). Truecolor from a pane is mapped to the nearest xterm-256 entry,
unless the outer terminal advertises `COLORTERM=truecolor` and is not
screen/tmux/linux.

The terminal window title is set to `[moerx] hostname:program`, following the
active moerx window (or `[moerx] program` with `--no-hostname`). Hostnames use
their short form (before the first dot).

## Deliberate limitations

This is "very basic" by design:

- No config file, no command prompt, no scripting, no plugins.
- No text selection or copying in scrollback, no zoom, no activity monitoring,
  no clock mode.
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

## Authors

- Armin Jenewein - <a@bsd.pm> - Founder
- Johannes Findeisen - <you@hanez.org> - Contributor

## License

Copyright 2026 Armin Jenewein

The code of moerx is licensed under the terms aof the Apache-2.0 license. See 
LICENSE for details.
