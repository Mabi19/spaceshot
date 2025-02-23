#pragma once

#include "wayland/seat.h"
#include "wlr-layer-shell-client.h"
#include <wayland-client.h>
#include <wlr-screencopy-client.h>
#include <xdg-output-client.h>

typedef void (*OutputCallback)(WrappedOutput *);

typedef struct {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    struct zxdg_output_manager_v1 *output_manager;
    SeatDispatcher *seat_dispatcher;
    OutputCallback handle_output_create;
} WaylandGlobals;
extern WaylandGlobals wayland_globals;

bool find_wayland_globals(
    struct wl_display *display, OutputCallback output_callback
);
