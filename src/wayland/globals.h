#pragma once

#include "ext-image-capture-source-client.h"
#include "ext-image-copy-capture-client.h"
#include "wayland/seat.h"
#include <cursor-shape-client.h>
#include <fractional-scale-client.h>
#include <viewporter-client.h>
#include <wayland-client.h>
#include <wlr-layer-shell-client.h>
#include <wlr-screencopy-client.h>
#include <xdg-output-client.h>

typedef void (*OutputCallback)(WrappedOutput *);

typedef struct {
    struct wl_compositor *compositor;
    struct wl_data_device_manager *data_device_manager;
    struct wl_shm *shm;
    struct wl_subcompositor *subcompositor;
    struct ext_image_copy_capture_manager_v1 *ext_image_copy_capture_manager;
    struct ext_output_image_capture_source_manager_v1
        *ext_output_capture_source_manager;
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wp_viewporter *viewporter;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_screencopy_manager_v1 *wlr_screencopy_manager;
    struct zxdg_output_manager_v1 *output_manager;
    SeatDispatcher *seat_dispatcher;
    struct wl_list outputs;
    OutputCallback handle_output_create;
} WaylandGlobals;
extern WaylandGlobals wayland_globals;

bool find_wayland_globals(
    struct wl_display *display, OutputCallback create_output_callback
);

void cleanup_wayland_globals();

/**
    Returns whether the specified output still exists.
*/
bool is_output_valid(WrappedOutput *output);
