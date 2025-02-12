#pragma once

#include <wayland-client.h>
#include <wlr-screencopy-client.h>

typedef struct {
    struct wl_output *wl_output;
    const char *name;
    struct wl_list link;
} WrappedOutput;

typedef void (*OutputCallback)(WrappedOutput *);

typedef struct {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    OutputCallback handle_output_create;
} WaylandGlobals;
extern WaylandGlobals wayland_globals;

bool find_wayland_globals(
    struct wl_display *display, OutputCallback output_callback
);
