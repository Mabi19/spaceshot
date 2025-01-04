#pragma once

#include <wayland-client.h>
#include <wlr-screencopy-client.h>

typedef void (*OutputCallback)(struct wl_output *);

typedef struct {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    struct wl_output **outputs;
    OutputCallback handle_output_create;
} WaylandGlobals;
extern WaylandGlobals wayland_globals;

int find_wayland_globals(
    struct wl_display *display, OutputCallback output_callback
);
