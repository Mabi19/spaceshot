# Spaceshot
A batteries-included screenshot tool for wlroots-compatible Wayland compositors.

> [!WARNING]
> This is still a work-in-progress, but largely usable.

It takes a screenshot first and then allows selecting a region on it, which makes taking screenshots of moving things and popups (which would disappear when focus changes) possible.

## Features
- Proper (fractional) scaling support: snaps to device pixels and not logical pixels, which makes selections more precise
- 10-bit image format support (saved as 16-bit PNGs)
- Integrated copying to clipboard (only in interactive modes)
- Screenshots are only ever cropped (and never scaled)
- Selection border is drawn outside the selection (so it's clear which pixels will end up in the final screenshot)
- Sending notifications, with actions to open the result file and view it in a file manager

Note that spaceshot will not allow selecting a region which overlaps multiple monitors. This is because screenshotting multiple monitors at once requires scaling the screenshots, which is a non-goal.

Planned:
- An output picker
    - As a workaround, use `spaceshot output $(slurp -o -f "%o")`
- Snapping to predefined aspect ratios (Shift?)
- Hold a key (Ctrl?) when releasing mouse button to edit selection afterwards
- A better selection border that will be visible on both light and dark backgrounds
- More configuration options (mostly concerning appearance)
- Screenshotting individual windows
    - There isn't a widely-supported protocol for this yet
    - ext-image-capture-source-v1 is ideal, but support is limited
    - May be implemented using hyprland-toplevel-export-v1 first
- Edit action for notifications (to launch an external editor)

### Controls
- Click and drag to select a region
- Press Esc or click in place to cancel
- Hold Alt or Space to move the selection instead of resizing it

## Building
You will need a C23-capable compiler and [Meson](https://mesonbuild.com).
Dependencies:
- cairo
- iniparser
- libpng
- xkbcommon
- wayland (incl. wlr-protocols and wayland-protocols)

> [!NOTE]
> All Wayland protocols are currently sourced from the system; the Arch Linux repos have them, but I don't know about other Linux distributions / OSes.

If the `notifications` build option is enabled (it is on by default):
- the Vala toolchain is required to build,
- xdg-open and a file manager implementing org.freedesktop.FileManager1 need to be available at runtime.

Note that notifications depend on a D-Bus service, and the easiest way to make that available is via `meson install`. However, for testing, setting the environment variable `$SPACESHOT_NOTIFY_PATH` and running `./build/notify/spaceshot-notify -s` will also work.
```sh
meson setup build
meson compile -C build
# spaceshot now lives in ./build/src/spaceshot; to install, do
meson install -C build
```

## Running
Run `spaceshot --help` for the help text.

Example invocations:
```sh
# print version
spaceshot -v
# print help
spaceshot -h

# select a region and screenshot it
spaceshot region
# screenshot a predefined region
# note that the coordinates can be fractional, unlike slurp
spaceshot region '150,150 300x200'
# screenshot an output
# note that there is no output picker yet
spaceshot output DP-1
```

## Configuration
Configuration is initialized from the default options, then loaded from $XDG_CONFIG_DIRS/spaceshot/config.ini, then $XDG_CONFIG_HOME/spaceshot/config.ini, and finally from command-line arguments. All configuration files are optional.

Available configuration options:
- `copy-to-clipboard` (boolean, default: `true`): whether to copy to the clipboard after screenshotting. Note that copying is only available in interactive modes; otherwise this option is ignored. Also available via the `-c`/`--copy` option, or `--no-copy` to disable
- `move-to-background` (boolean, default: `false`): whether to move to the background after screenshotting. If set to false, the process will block until the clipboard is overridden and the notification is dismissed. Also available via the `-b`/`--background` option
- `output-file` (string, default: `~~/%Y-%m-%d-%H%M%S-spaceshot.png`): a template for the output filename. `~/` expands to `$HOME`, `~~/` expands to `$(xdg-user-dir PICTURES)`. Accepts `strftime` specifiers. Also available via the `-o`/`--output-file` option
- `png-compression-level` (integer in the range [0, 9], default: 4): the compression level to save PNGs at. Note that this is lowered by default to improve performance, at a small expense in file size
- `verbose` (boolean, default: `false`): whether to enable debug logging. Also available via the `--verbose` option
- In `[notify]` section:
    - `enabled` (boolean, default: `true`): whether to send a notification after screenshotting. Also available via the `-n`/`--notify` option, or `--no-notify` to disable
