# shot

A screenshot utility for Hyprland, with three capture modes, save and clipboard
options, and an optional graphical capture bar.

It shells out to `grim` for the actual capture, `slurp` for interactive region
and window selection, `hyprctl` to find windows, `wl-copy` for the clipboard,
and `notify-send`/`xdg-open` for the "screenshot saved" notification.

## Requirements

| Purpose | Program |
| --- | --- |
| Capture (always) | `grim` |
| Window mode | `hyprctl` (Hyprland) |
| Window / section modes | `slurp` |
| `--copy` / `--copy-only` | `wl-copy` |
| Saved-screenshot notification | `notify-send`, `xdg-open` (optional) |
| The graphical bar | `hyprtoolkit`, `hyprutils` (build-time) |

On Arch:

```sh
sudo pacman -S grim slurp wl-clipboard libnotify xdg-utils hyprtoolkit
```

`shot` tells you which one is missing and exits with code 3 rather than failing
obscurely.

## Build

```sh
make            # -> build/shot
make test       # unit tests, then the end-to-end CLI tests
make clean
```

Only `pkg-config` is needed besides a C++23 compiler; hyprtoolkit ships no
CMake package, so `pkg_check_modules`/`pkg-config` is the supported route.

## Usage

```
shot <screen|window|section> [options]
```

| Mode | Captures |
| --- | --- |
| `screen` | the whole screen (or one output with `--monitor`) |
| `window` | the window you click |
| `section` | the region you drag |

| Option | Meaning |
| --- | --- |
| `-c`, `--copy` | save to disk **and** copy to the clipboard |
| `--copy-only` | copy to the clipboard, saving nothing (streams `grim` into `wl-copy`; no temporary file) |
| `-d`, `--dir PATH` | save directory (default `~/Pictures/Screenshots`) |
| `-f`, `--file NAME` | output filename (default a timestamp; `~` and nested paths are handled) |
| `--delay N` | wait N seconds before capturing |
| `-o`, `--open` | open the saved file with `xdg-open` |
| `-m`, `--monitor OUT` | capture one output, e.g. `eDP-1` (screen mode only) |
| `--gui` | open the graphical capture bar (same as no arguments; takes no other arguments) |
| `-h`, `--help` | usage |
| `--version` | version |

Running `shot` with no arguments opens the graphical bar.

The saved path is printed to stdout, so it can be piped:

```sh
file="$(shot section)" && echo "saved to $file"
```

### Exit codes

| Code | Meaning |
| --- | --- |
| 0 | success, or the capture bar was dismissed |
| 1 | nothing captured (selection cancelled) or the GUI is unavailable |
| 2 | bad command line |
| 3 | a required program is missing |
| 4 | the capture or the clipboard copy failed |

### Notification opt-out

Set `SHOT_NO_NOTIFY=1` to suppress the "screenshot saved" notification. The test
suite uses this.

## The graphical capture bar

With no arguments (or `--gui`) `shot` shows a bar pinned to the bottom of the
screen:

- **Screen**, **Window** and **Section** are buttons, not a selection: clicking
  one starts that capture immediately. There is no confirm step.
- The result menu sits on the right and is read when you click a mode, so set it
  first. It offers two choices: **Save to <folder>** — naming the actual
  destination, with `~` for your home directory — which writes the file *and*
  copies it, and **Save to clipboard**, which writes nothing.
- `Escape` (once the bar has keyboard focus) or a right-click anywhere on the
  bar dismisses it without capturing.

The bar is a `wlr-layer-shell` surface on the top layer, anchored to the bottom
edge only and centred horizontally at a fixed width (660px — see `kBarWidth` in
`src/gui.cpp`). Anchoring the left and right edges as well is what would stretch
it across the entire screen. It uses no exclusive zone, so it floats over other
windows instead of resizing them. It asks for the keyboard *on demand*: `Escape`
works once the compositor focuses the bar, which is why a right-click anywhere
on the bar is the always-available way to dismiss it.

### Theming

The bar is styled entirely from the hyprtoolkit palette
(`~/.config/hypr/hyprtoolkit.conf`): its surface uses the palette background,
`rounding_large` and a 1px `accent` border, the same way hyprlauncher draws
itself. The mode buttons and the combobox are drawn by the toolkit from the
same palette.

It registers the layer-shell namespace `shot`. A translucent palette therefore
makes the bar translucent as well; pair it with a blur rule if you want one,
for example:

```lua
hl.layer_rule({ name = "shot-blur", match = { namespace = "shot" }, blur = true })
```

## Layout

```
src/
  main.cpp        entry point: dispatch to the GUI or the CLI, then capture
  args.{hpp,cpp}  argument parsing and the help text
  capture.{hpp,cpp} grim invocations, clipboard, destination paths
  hyprland.{hpp,cpp} hyprctl queries and slurp selection
  json.{hpp,cpp}  small strict JSON reader for `hyprctl -j`
  process.{hpp,cpp} running subprocesses safely
  notify.{hpp,cpp} the saved-screenshot notification
  gui.{hpp,cpp}   the hyprtoolkit capture bar
  gui_state.{hpp,cpp} controls -> capture arguments (no hyprtoolkit, so testable)
  version.hpp
tests/
  json_test.cpp      unit tests for the JSON reader
  args_test.cpp      unit tests for argument parsing
  gui_state_test.cpp unit tests for the capture bar's selection mapping
  cli_test.sh        end-to-end tests against the built binary
```

Three implementation notes worth knowing:

- **`process.cpp` multiplexes stdin, stdout and stderr with `poll()`.** The
  obvious "write everything to the child, then read it" order deadlocks as soon
  as the child fills a pipe buffer, and writing to a child that has already
  exited raises `SIGPIPE`. It also cannot wait for end-of-file: a command that
  forks (`wl-copy`) hands the write end of its pipe to a grandchild that
  outlives it, so the read end never closes. The loop therefore stops as soon as
  the child has been reaped, and the pipes are non-blocking so a read never
  blocks after the last byte available.
- **Copy-only mode never touches the filesystem.** `grim -` writes the PNG to
  stdout and it is piped straight into `wl-copy`.
- **The capture bar's controls feed a pure function** (`gui_state.cpp`), so the
  behaviour the GUI produces is unit tested without needing a compositor.

## Tests

`make test` runs:

- `json_test` — parsing and rejecting JSON, including escapes, surrogate pairs
  and malformed input.
- `args_test` — every option, `--opt=value` and separate-value forms, `--help`
  and `--version`, and each invalid combination.
- `gui_state_test` — every mode/behaviour combination the capture bar can
  produce, and that it never yields a combination the CLI rejects.
- `cli_test.sh` — runs the real binary: `--help`, every error path, real screen
  captures, filename uniqueness, `--file` handling, `--copy`, and `--copy-only`
  leaving no file behind. The capture tests need a live Hyprland session and
  skip themselves otherwise.

The capture bar itself needs a live Wayland session with a working GPU, so it is
verified by running `./build/shot --gui` rather than by an automated test; the
selection logic it drives is covered by `gui_state_test`.

## Licence

MIT — see [LICENSE](LICENSE).
