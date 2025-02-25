#include "globals.h"
#include "wayland/seat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wlr-screencopy-client.h>
#include <xdg-output-client.h>

WaylandGlobals wayland_globals;

static void output_handle_geometry(
    void * /* data */,
    struct wl_output * /* output */,
    int32_t /* x */,
    int32_t /* y */,
    int32_t /* physical_width */,
    int32_t /* physical_height */,
    int32_t /* subpixel */,
    const char * /* make */,
    const char * /* model */,
    int32_t /* transform */
) {
    // this space intentionally left blank
}

static void output_handle_mode(
    void * /* data */,
    struct wl_output * /* output */,
    uint32_t /* flags */,
    int32_t /* width */,
    int32_t /* height */,
    int32_t /* refresh */
) {
    // this space intentionally left blank
}

static void output_handle_scale(
    void * /* data */, struct wl_output * /* output */, int32_t /* factor */
) {
    // this space intentionally left blank
}

static void output_handle_name(
    void *data, struct wl_output * /* output */, const char *name
) {
    WrappedOutput *output = data;
    output->name = strdup(name);
    output->fill_state |= WRAPPED_OUTPUT_HAS_NAME;
}

static void output_handle_description(
    void * /* data */,
    struct wl_output * /* output */,
    const char * /* description */
) {
    // this space intentionally left blank
}

static void output_handle_done(void *data, struct wl_output * /* output */) {
    WrappedOutput *output = data;
    if (output->fill_state & WRAPPED_OUTPUT_CREATE_WAS_CALLED) {
        // this is an update.
        // TODO: handle updates
        // TODO: this should probably use a pending state mechanism anyway
    } else {
        if ((output->fill_state & WRAPPED_OUTPUT_HAS_ALL) ==
            WRAPPED_OUTPUT_HAS_ALL) {
            // just created
            output->fill_state |= WRAPPED_OUTPUT_CREATE_WAS_CALLED;
            wayland_globals.handle_output_create(output);
        }
    }
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .scale = output_handle_scale,
    .name = output_handle_name,
    .description = output_handle_description,
    .done = output_handle_done,
};

static void xdg_output_handle_name(
    void * /* data */,
    struct zxdg_output_v1 * /* output */,
    const char * /* name */
) {
    // this space intentionally left blank
}

static void xdg_output_handle_description(
    void * /* data */,
    struct zxdg_output_v1 * /* output */,
    const char * /* name */
) {
    // this space intentionally left blank
}

static void xdg_output_handle_logical_position(
    void *data, struct zxdg_output_v1 * /* output */, int32_t x, int32_t y
) {
    WrappedOutput *output = data;
    output->logical_bounds.x = x;
    output->logical_bounds.y = y;
    output->fill_state |= WRAPPED_OUTPUT_HAS_LOGICAL_POSITION;
}

static void xdg_output_handle_logical_size(
    void *data,
    struct zxdg_output_v1 * /* output */,
    int32_t width,
    int32_t height
) {
    WrappedOutput *output = data;
    output->logical_bounds.width = width;
    output->logical_bounds.height = height;
    output->fill_state |= WRAPPED_OUTPUT_HAS_LOGICAL_SIZE;
}

static void xdg_output_handle_done(
    void * /* data */, struct zxdg_output_v1 * /* output */
) {
    // This space intentionally left blank
    // note that wl_output_done replaces this event
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done
};

static void registry_handle_global(
    void *data,
    struct wl_registry *registry,
    uint32_t object_id,
    const char *interface,
    uint32_t /* version */
) {
    WaylandGlobals *globals = data;

    // printf(
    //     "Interface '%s' version %d with object ID %d\n",
    //     interface,
    //     version,
    //     object_id
    // );

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        globals->compositor =
            wl_registry_bind(registry, object_id, &wl_compositor_interface, 6);
    }

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        globals->shm =
            wl_registry_bind(registry, object_id, &wl_shm_interface, 1);
    }

    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        globals->layer_shell = wl_registry_bind(
            registry, object_id, &zwlr_layer_shell_v1_interface, 4
        );
    }

    if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        globals->screencopy_manager = wl_registry_bind(
            registry, object_id, &zwlr_screencopy_manager_v1_interface, 3
        );
    }

    if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        globals->output_manager = wl_registry_bind(
            registry, object_id, &zxdg_output_manager_v1_interface, 3
        );
    }

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        if (globals->seat_dispatcher) {
            fprintf(
                stderr,
                "warning: Multiple seats present. Handling this is "
                "unimplemented, only one will work\n"
            );
        } else {
            struct wl_seat *seat =
                wl_registry_bind(registry, object_id, &wl_seat_interface, 9);
            globals->seat_dispatcher = seat_dispatcher_new(seat);
        }
    }

    if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *wl_output =
            wl_registry_bind(registry, object_id, &wl_output_interface, 4);

        WrappedOutput *output = calloc(1, sizeof(WrappedOutput));
        output->wl_output = wl_output;
        wl_output_add_listener(wl_output, &output_listener, output);

        output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
            globals->output_manager, wl_output
        );
        zxdg_output_v1_add_listener(
            output->xdg_output, &xdg_output_listener, output
        );
    }
}

static void registry_handle_global_remove(
    void * /* data */, struct wl_registry * /* registry */, uint32_t object_id
) {
    // TODO: Is this an output?
    printf("Global object ID %d just got removed\n", object_id);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};

bool find_wayland_globals(
    struct wl_display *display, OutputCallback output_callback
) {
    // initialize wayland_globals object
    memset(&wayland_globals, 0, sizeof(WaylandGlobals));
    wayland_globals.handle_output_create = output_callback;

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &wayland_globals);
    wl_display_roundtrip(display);

    if (wayland_globals.compositor == NULL || wayland_globals.shm == NULL ||
        wayland_globals.layer_shell == NULL ||
        wayland_globals.screencopy_manager == NULL ||
        wayland_globals.seat_dispatcher == NULL ||
        wayland_globals.output_manager == NULL) {
        return false;
    }

    return true;
}
