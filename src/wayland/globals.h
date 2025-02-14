#pragma once

#include "bbox.h"
#include <wayland-client.h>
#include <wlr-screencopy-client.h>
#include <xdg-output-client.h>

typedef enum {
    WRAPPED_OUTPUT_HAS_NAME = 1,
    WRAPPED_OUTPUT_HAS_LOGICAL_POSITION = 2,
    WRAPPED_OUTPUT_HAS_LOGICAL_SIZE = 4,
    WRAPPED_OUTPUT_CREATE_WAS_CALLED = 8,
    WRAPPED_OUTPUT_HAS_ALL = 1 | 2 | 4,
} WrappedOutputFillState;

typedef struct {
    struct wl_output *wl_output;
    struct zxdg_output_v1 *xdg_output;
    const char *name;
    BBox logical_bounds;
    WrappedOutputFillState fill_state;
} WrappedOutput;

typedef void (*OutputCallback)(WrappedOutput *);

typedef struct {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    struct zxdg_output_manager_v1 *output_manager;
    OutputCallback handle_output_create;
} WaylandGlobals;
extern WaylandGlobals wayland_globals;

bool find_wayland_globals(
    struct wl_display *display, OutputCallback output_callback
);
