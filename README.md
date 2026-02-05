# Spaceshot
A batteries-included screenshot tool for wlroots-compatible Wayland compositors.

> [!WARNING]
> This is still a work-in-progress, but largely usable.

It takes a screenshot first and then allows selecting a region on it, which makes taking screenshots of moving things and popups (which would disappear when focus changes) possible.

## Features
- Proper (fractional) scaling support: snaps to device pixels and not logical pixels, which makes selections more precise
- 10-bit image format support (saved as 16-bit PNGs) (note that this may require compositor configuration)
- Integrated copying to clipboard (only in interactive modes)
- Screenshots are only ever cropped (and never scaled)
- Selection border is drawn outside the selection (so it's clear which pixels will end up in the final screenshot)
- Sending notifications, with actions to open the result file, edit it, and view it in a file manager

Note that spaceshot will not allow selecting a region which overlaps multiple monitors. This is because screenshotting multiple monitors at once requires scaling the screenshots, which is a non-goal.

Planned:
- Snapping to predefined aspect ratios (Shift?)
- More configuration options (mostly concerning appearance)
- Screenshotting individual windows
    - There isn't a widely-supported protocol for this yet
    - ext-image-capture-source-v1 is ideal, but support is limited
    - May be implemented using hyprland-toplevel-export-v1 first

### Controls
- Click and drag to select a region
- Press Esc or click in place to cancel
- Hold Alt or Space to move the selection instead of resizing it
- Hold Ctrl while releasing the mouse button to edit the selection by dragging its edges or corners
    - In edit mode, press Enter to confirm

## Building
You will need a C23-capable compiler and [Meson](https://mesonbuild.com). GCC 15 or Clang 19 should work.
Dependencies:
- cairo
- libpng
- pango
- xkbcommon
- wayland (incl. wlr-protocols and wayland-protocols)

> [!NOTE]
> All Wayland protocols are currently sourced from the system; the Arch Linux repos have them, but I don't know about other Linux distributions / OSes.

If the `notifications` build option is enabled (it is on by default):
- the Vala toolchain is required to build,
- xdg-open and a file manager implementing org.freedesktop.FileManager1 need to be available at runtime,
- [satty](https://github.com/gabm/satty) is invoked by the edit button, though this is configurable

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
spaceshot output
# screenshot a predefined output
spaceshot output DP-1
```

### Deferred mode
Running `spaceshot defer` puts it in a special mode made for scripting, where screenshot parameters are passed in later.
In this mode, the program flow looks like this:
1. All the available outputs are captured and saved.
2. spaceshot outputs a line containing "ready" on stdout.
3. A new set of arguments is input over stdin (separated by NULs, and terminated by EOF)
4. Screenshotting continues as normal, using the images captured during step 1 and using the arguments from step 3

So, running `echo "region" | spaceshot defer` is equivalent to running `spaceshot region`.

This separation can, for example, be used to make "choose-how-to-screenshot" menus, while ensuring the screenshot does not contain the menu.

<details>
    <summary>Example script</summary>

This script implements a "choose-how-to-screenshot" menu. It uses [Walker](https://github.com/abenz1267/walker) in dmenu mode to show the menu. While this example only puts in a mode after deferring, all arguments are supported (and are treated as if they were passed in right after the "real" argv)

```sh
#!/bin/bash

coproc spaceshot defer
read -r READY <&"${COPROC[0]}"

if [ "$READY" != "ready" ]; then
    exit 1
fi

CHOICE=$(printf "Region\\nOutput" | walker -d)

case "$CHOICE" in
    "Region") ARGS=("region") ;;
    "Output") ARGS=("output") ;;
    *) exit 1 ;;
esac

for arg in "${ARGS[@]}"; do
    printf '%s\0' "$arg" >&"${COPROC[1]}"
done

# shellcheck disable=SC2093,SC1083
exec {COPROC[1]}>&-

wait "$COPROC_PID"
```

</details>

## Configuration
Configuration is initialized from the default options, then loaded from $XDG_CONFIG_DIRS/spaceshot/config.ini, then $XDG_CONFIG_HOME/spaceshot/config.ini, and finally from command-line arguments. All configuration files are optional, and don't need to specify every property - only the ones you want to override.

The format is INI-like; most notably values are interpreted based on the associated key's type information. This means that quotes do not mean the value will be a string, they just prevent parsing ; and # within them as comments.

See [config/defaults.ini](config/defaults.ini) for all available options and some descriptions. You can also look at [config/config.py](config/config.py) for the schema.
