# WinScreen

A terminal multiplexer for Windows, inspired by [GNU Screen](https://www.gnu.org/software/screen/) — the classic Unix terminal multiplexer — and tmux. Written in ANSI C using the Windows Console API and Windows Pseudo Console (ConPTY).

GNU Screen is developed by the GNU Project; its source code is available at <https://savannah.gnu.org/projects/screen/>.

**Author:** Igor Brzezek  
**Version:** 0.1  
**License:** GNU GPL  
**Repository:** https://github.com/IgorBrzezek/WinScreen

---

## Overview

WinScreen allows you to manage multiple terminal sessions (windows) within a single Windows console window. It uses a client–server architecture:

- A **headless server process** manages the terminal windows (each backed by a Windows Pseudo Console running a shell like `cmd.exe`).
- A **client process** connects to the server over a local TCP connection and provides the interactive terminal UI (keyboard input, screen rendering).
- The server and client can run in separate console windows. The client can detach and reattach to the same session, preserving the running shells.

### Architecture

```
┌──────────────────────────────────────────────────┐
│               Client Process                     │
│  ┌──────────────┐          ┌──────────────────┐  │
│  │   Renderer   │◄──TCP──► │   TCP Socket     │  │
│  │  (ANSI/VT100)│          │  (CMD_SCR)       │  │
│  └──────────────┘          └────────┬─────────┘  │
│  ┌──────────────┐                   │            │
│  │  Input       │━━━━━━━━━━━━━━━━━▶│ TCP Socket  │
│  │  (keyboard)  │  (CMD_KBD/ACT)  └─────────────┘│
│  └──────────────┘                                │
└──────────────────────────────────────────────────┘
                        │
                   TCP/IP loopback
                        │
┌───────────────────────┴─────────────────────────┐
│              Server Process (headless)          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│  │ Window 0 │  │ Window 1 │  │ Window 2 │  ...  │
│  │  cmd.exe │  │  cmd.exe │  │  cmd.exe │       │
│  │ ConPTY   │  │ ConPTY   │  │ ConPTY   │       │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘       │
│       │             │             │             │
│  ┌────┴─────────────┴─────────────┴────┐        │
│  │           Manager + Session         │        │
│  │   (window mgmt, TCP listener)       │        │
│  └─────────────────────────────────────┘        │
└─────────────────────────────────────────────────┘
```

## Features

- **Multiple terminal windows** – up to 10 simultaneous shell sessions
- **Detach and reattach** – disconnect the client without killing the server; reconnect later from any console
- **Session persistence** – sessions survive client disconnection; shells keep running
- **Status bar** – configurable status bar with hostname, clock, and window list
- **256-color ANSI support** – full terminal emulation with VT100/ANSI escape sequence parsing
- **Window renaming** – rename windows interactively
- **Configurable key bindings** – change the prefix key from the default `Ctrl+A`
- **Configuration file** – INI-style config for status bar colors, shell command, and more
- **Clipboard paste** – right-click pastes clipboard content into the terminal (client-side)
- **Scrollback buffer** – browse previous terminal output with keyboard or mouse wheel
- **Window list / help / info overlays** – in-terminal overlay dialogs with DOS-style box drawing
- **Language files** – translatable UI strings via `.lng` files

## Command-Line Options

| Option | Description |
|--------|-------------|
| `-h` | Show help message |
| `-c FILE` | Path to configuration file (default: `winscreen.cfg`) |
| `-S NAME` | Session name (default: auto-generated `WinScr_YYYYMMDD_HHMMSS`) |
| `-r NAME` | Reattach to an existing session by name |
| `-e CMD` | Shell command for the first window (e.g. `powershell.exe`) |
| `-n NAME` | Window name for the first window |
| `--encoding ENC` | Terminal encoding (currently `utf-8` default; reserved) |
| `--linebuf N` | Scrollback buffer lines (default: 256; reserved) |
| `-ls` | List all active sessions |
| `--version` | Show version number |
| `--lang FILE` | Load a language/translation file |
| `--CLEAN_ALL_SESSIONS` | Kill all WinScreen processes and clear session files |

## Quick Start

```batch
:: Start a new session (spawns server + attaches client)
winscreen.exe

:: Start with a specific session name
winscreen.exe -S myproject

:: Reattach to an existing session
winscreen.exe -r myproject

:: Start with PowerShell instead of cmd.exe
winscreen.exe -e powershell.exe

:: Start with a specific config file
winscreen.exe -c myconfig.cfg

:: List active sessions
winscreen.exe -ls
```

## Key Bindings

All commands are issued by pressing the **prefix key** followed by a command key. The default prefix is `Ctrl+A`.

| Sequence | Action |
|----------|--------|
| `Ctrl+A` `c` | Create a new window |
| `Ctrl+A` `n` | Switch to next window |
| `Ctrl+A` `p` | Switch to previous window |
| `Ctrl+A` `k` | Close (kill) the current window |
| `Ctrl+A` `d` | Detach from the session |
| `Ctrl+A` `w` | Show window list |
| `Ctrl+A` `h` / `?` / `/` | Show help overlay |
| `Ctrl+A` `i` | Show session info overlay |
| `Ctrl+A` `a` | Rename the current window |
| `Ctrl+A` `r` | Redraw the screen |
| `Ctrl+A` `0`–`9` | Switch directly to window by index |
| `Ctrl+A` `Ctrl+A` | Switch to the previously active window (last window toggle) |
| `Ctrl+A` `↑` / `↓` | Scroll up/down by one line |
| `Ctrl+A` `PgUp` / `PgDn` | Scroll up/down by one full page |
| Mouse wheel | Scroll up/down by one line |

### Scroll Mode

Pressing `↑`/`↓` or `PgUp`/`PgDn` after the prefix key enters scroll mode. The terminal view shifts to show previous output from the scrollback buffer (up to 1024 lines).

- Any new terminal output or keyboard input automatically returns the view to the bottom (normal mode).
- The mouse wheel also scrolls line by line when hovered over the console window.

### Rename Mode

After pressing `Ctrl+A` `a`, the status bar shows `Rename: _`. Type the new name and press `Enter` to confirm, or `Escape` to cancel.

## Configuration File

WinScreen reads an INI-style configuration file. The default path is `winscreen.cfg` in the working directory. Pass a different file with `-c`.

### `[statusbar]` Section

| Key | Description | Default |
|-----|-------------|---------|
| `position` | Status bar position: `top` or `bottom` | `bottom` |
| `background_color` | ANSI 256-color index for the status bar background | `4` (blue) |
| `foreground_color` | ANSI 256-color index for the status bar text | `15` (bright white) |
| `active_window_fg` | Active window name foreground color | `14` (bright cyan) |
| `active_window_bg` | Active window name background color | `4` (blue) |
| `inactive_window_fg` | Inactive window name foreground color | `7` (white) |
| `inactive_window_bg` | Inactive window name background color | `4` (blue) |
| `active_bracket_fg` | Active window bracket foreground color | `9` |
| `active_bracket_bg` | Active window bracket background color | `4` |
| `inactive_bracket_fg` | Inactive window bracket foreground color | `7` |
| `inactive_bracket_bg` | Inactive window bracket background color | `4` |
| `window_brackets` | Bracket style: `brackets`, `parens`, `angles`, `braces` | `parens` |
| `active_symbol` | Symbol marking the active window | `*` |
| `show_clock` | Show clock on status bar: `yes`/`no`/`true`/`false`/`1`/`0` | `no` |
| `clock_format` | Clock format: `HH:MM` or `HH:MM:SS` | `HH:MM` |
| `clock_position` | Clock position: `left` or `right` | `right` |
| `hostname` | Show hostname on status bar: `ON`/`OFF`/`yes`/`no` | `OFF` |
| `hostname_fg` | Hostname foreground color | `11` (bright yellow) |
| `clock_fg` | Clock foreground color | `14` (bright cyan) |
| `separator_fg` | Status bar separator `|` color | `240` (gray) |
| `rename_fg` | Rename mode prompt foreground color | `15` |
| `rename_bg` | Rename mode prompt background color | `236` |

### `[shell]` Section

| Key | Description | Default |
|-----|-------------|---------|
| `command` | Shell executable path | `cmd.exe` |
| `default_name` | Default name for new windows | `shell` |

### `[keybindings]` Section

| Key | Description | Default |
|-----|-------------|---------|
| `prefix` | Prefix key combintion (last character is the virtual key) | `Ctrl+A` |

### Example Configuration

```ini
[statusbar]
position = bottom
background_color = 236
foreground_color = 15
show_clock = yes
clock_format = HH:MM:SS
clock_position = right
hostname = ON
window_brackets = braces

[shell]
command = powershell.exe
default_name = PS

[keybindings]
prefix = Ctrl+B
```

## Session Files

Session metadata is stored in `%APPDATA%\WinScreen\sessions\` as `.txt` files. Each file contains:

```
name=WinScr_20260101_120000
pid=12345
port=54321
windows=3
created=1767225600
encoding=utf-8
```

Use `winscreen.exe -ls` to list all active sessions. Use `--CLEAN_ALL_SESSIONS` to terminate all server processes and remove all session files.

## Language / Translation

WinScreen supports on-the-fly UI translation via `.lng` files. Provided translations:

- `winscreen.lng` – English
- `winscreen_pl.lng` – Polish
- `winscreen_de.lng` – German
- `winscreen_cz.lng` – Czech
- `winscreen_sk.lng` – Slovak

Usage:

```batch
winscreen.exe --lang winscreen_pl.lng
```

Language files use a simple `key=value` format (lines starting with `#` are comments).

## Building

### Requirements

- MinGW-w64 cross-compiler (or MSYS2/MinGW on Windows)
- Windows SDK headers (included with MinGW)

### Cross-compile from Linux

```bash
make -f Makefile.mingw
```

### Build on Windows (MSYS2/MinGW)

```bash
mingw32-make -f Makefile.mingw
# or edit Makefile.mingw to use your local compiler, e.g. CC = gcc
```

The build produces `winscreen.exe` with no external runtime dependencies (statically linked).

### Source Files

| File | Purpose |
|------|---------|
| `main.c` | Entry point, CLI parsing, server/client main loops |
| `manager.c` | Window lifecycle, overlay dialogs, action dispatch |
| `window.c` | TerminalBuffer (VT100 emulation), VtWindow (ConPTY wrapper), reader thread |
| `renderer.c` | Content rendering and status bar rendering to ANSI strings |
| `input.c` | Keyboard input processing, prefix detection, rename mode |
| `session.c` | TCP session management, Winsock wrappers, session file I/O |
| `config.c` | INI configuration parser |
| `lang.c` | Language/translation file loader |
| `inih/ini.c` | Third-party INI parser (inih) |

## Protocol

Communication between client and server uses a simple message-based TCP protocol over loopback:

```
[4-byte length: big-endian uint32][1-byte command][payload]
```

### Commands

| Byte | Name | Direction | Payload |
|------|------|-----------|---------|
| `0x01` | `CMD_KBD` | Client → Server | UTF-8 keyboard input |
| `0x02` | `CMD_SCR` | Server → Client | ANSI/VT100 screen content |
| `0x03` | `CMD_DET` | Either | Detach request |
| `0x04` | `CMD_QUIT` | Client → Server | Quit server |
| `0x08` | `CMD_ACT` | Either | Action (see below) |

### Actions (inside `CMD_ACT`)

| Byte | Name |
|------|------|
| `1` | Passthrough |
| `2` | New window |
| `3` | Next window |
| `4` | Previous window |
| `5` | Switch to window N |
| `6` | Last window |
| `7` | Start rename |
| `8` | Rename character |
| `9` | Confirm rename |
| `10` | Cancel rename |
| `11` | Kill window |
| `14` | Show help overlay |
| `15` | Show window list |
| `16` | Detach |
| `17` | Resize terminal |
| `18` | Show info overlay |

## Limitations

- Windows-only (uses Win32 API, ConPTY, and Winsock)
- Maximum of 10 windows (`MAX_WINDOWS`)
- Mouse wheel scrolling only works when the client console window has focus
- UTF-8 only (encoding parameter is reserved for future use)
