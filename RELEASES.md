# murx 1.0.1 released

**November 23, 2026**

- Open the main menu with `Ctrl-A m` to manage windows, panes and sessions
  or change display settings live. Use `Ctrl-A o` to cycle through panes.
- Hide pane titles with `--hide-pane-titles` or toggle them in the
  Configuration menu. Visible titles stay anchored to their pane's top row.
- murx detects when it is launched from one of its own panes and prevents
  starting or attaching to a nested session.
- Full-screen redraws avoid corruption at the last column by disabling
  terminal auto-wrap while attached. Screen-style window title sequences
  are consumed without printing their contents into the pane.
- Fixed the FreeBSD build by including the header that declares `openpty`.

# murx 1.0 released

**November 21, 2026** — murx has reached 1.0. It is a deliberately small
terminal multiplexer: one C99 source file, no config file, no dependencies, and
no scripting layer. Build it with `make`, run `./murx`, and detach with
`Ctrl-A d`.

## What you get

- Persistent named sessions, each with its own small server and Unix socket.
  Programs keep running when you detach.
- Windows, split panes, resizing, renaming, scrollback and a configurable status
  line.
- Multiple clients attached to one session, each with its own prefix, scrollback
  position and lock state.
- Real ptys with a built-in terminal emulator: colors, 256-color and truecolor,
  UTF-8, wide characters, alternate screen and 2,000 lines of history per pane.
- Optional per-client prefix (`-p x`), status line position (`-s top|bottom`),
  status bar background (`-c RRGGBB`), clock, hostname and current-directory
  display.
- Optional SSH shell hooks for remote window names, directory reporting and
  root-shell highlighting.
- Linux, macOS, FreeBSD and OpenBSD support, plus an `APKBUILD` and `PKGBUILD`
  for packaging.

## Highlights since the early releases

The project was renamed from moerx to murx, gained a manual page and
`Apache-2.0` license, and now ships a manpage, package recipes and
install/uninstall targets. Session listing via `murx -l`, status bar colors,
scrollback-mode window switching, fixed Home/End handling and `murx -v` /
`murx --version` are all part of this release.

Navigation was also tightened: `Ctrl-A h/j/k/l` and the arrow keys now pick the
pane truly in the requested direction, preferring overlapping panes, and
terminal resizing is reconciled every 250 ms in addition to `SIGWINCH`.

## Upgrading

If you are coming from moerx, update aliases and scripts to use `murx`, finish
old sessions before removing the old executable, and replace the remote shell
hooks with the `murx-cwd` and `murx-root` variants. Display options are now
opt-in: use `murx --cwd --clock --hostname SESSION` for the older busy status
bar. Restart long-running session servers once after upgrading.

## Build and install

```sh
make
sudo make install
```

Set `PREFIX`, `BINDIR`, `MANDIR` or `DESTDIR` as needed. Read `murx.1` for full
option and shortcut documentation, or run `murx --help`.

Thanks to Armin Jenewein (founder) and Johannes Findeisen (contributor).
Licensed under Apache-2.0.
