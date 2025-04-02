# Spaceshot
A batteries-included screenshot tool for wlroots-compatible Wayland compositors.

It takes a screenshot first and then allows selecting a region on it, which makes taking screenshots of moving things and popups (which would disappear when focus changes) possible.

## Features
- Proper (fractional) scaling support: snaps to device pixels and not logical pixels, which makes selections more precise
- 10-bit image format support (saved as 16-bit PNGs)
- Integrated copying to clipboard (only in interactive modes)
- Screenshots are only ever cropped (and never scaled)
- Selection border is drawn outside the selection (so it's clear which pixels will end up in the final screenshot)

Note that spaceshot will not allow selecting a region which overlaps multiple monitors. This is because screenshotting multiple monitors at once requires scaling the screenshots, which is a non-goal.

Planned:
- Snapping to predefined aspect ratios (Shift?)
- Hold a key (Ctrl?) when releasing mouse button to edit selection afterwards
- A better selection border that will be visible on both light and dark backgrounds
- More configuration options
- Screenshotting individual windows
    - There isn't a widely-supported protocol for this yet
    - ext-image-capture-source-v1 is ideal, but support is limited
    - May be implemented using hyprland-toplevel-export-v1 first
- Sending notifications

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

```sh
meson setup build
meson compile -C build
# spaceshot now lives in ./build/src/spaceshot; to install, do
meson install -C build
```

## Running
Run `spaceshot --help` for the help text.
// TODO document options

## Configuration
Currently undocumented. // TODO fix
