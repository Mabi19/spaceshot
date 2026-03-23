#include "globals.h"
#include "ext-data-control-client.h"
#include "ext-foreign-toplevel-list-client.h"
#include "log.h"
#include "wayland/seat.h"
#include "wayland/toplevel.h"
#include <cursor-shape-client.h>
#include <ext-image-capture-source-client.h>
#include <ext-image-copy-capture-client.h>
#include <fractional-scale-client.h>
#include <stdlib.h>
#include <string.h>
#include <viewporter-client.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
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
    // if there was a previous name, free it properly
    free(output->name);
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

/** Doesn't remove from the list. */
static void wrapped_toplevel_destroy(WrappedToplevel *toplevel) {
    free(toplevel->identifier);
    free(toplevel->app_id);
    free(toplevel->title);
    ext_foreign_toplevel_handle_v1_destroy(toplevel->handle);
    free(toplevel);
}

static void toplevel_handle_handle_identifier(
    void *data,
    struct ext_foreign_toplevel_handle_v1 * /* toplevel */,
    const char *identifier
) {
    WrappedToplevel *toplevel = data;
    free(toplevel->identifier);
    toplevel->identifier = strdup(identifier);
}

static void toplevel_handle_handle_app_id(
    void *data,
    struct ext_foreign_toplevel_handle_v1 * /* toplevel */,
    const char *app_id
) {
    WrappedToplevel *toplevel = data;
    free(toplevel->app_id);
    toplevel->app_id = strdup(app_id);
}

static void toplevel_handle_handle_title(
    void *data,
    struct ext_foreign_toplevel_handle_v1 * /* toplevel */,
    const char *title
) {
    WrappedToplevel *toplevel = data;
    free(toplevel->title);
    toplevel->title = strdup(title);
}

static void toplevel_handle_handle_done(
    void *data, struct ext_foreign_toplevel_handle_v1 * /* toplevel */
) {
    WrappedToplevel *toplevel = data;
    wayland_globals.handle_toplevel_create(toplevel);
}

static void toplevel_handle_handle_closed(
    void *data, struct ext_foreign_toplevel_handle_v1 * /* toplevel */
) {
    WrappedToplevel *toplevel = data;
    wl_list_remove(&toplevel->link);
    wrapped_toplevel_destroy(toplevel);
}

static const struct ext_foreign_toplevel_handle_v1_listener
    toplevel_handle_listener = {
        .identifier = toplevel_handle_handle_identifier,
        .app_id = toplevel_handle_handle_app_id,
        .title = toplevel_handle_handle_title,
        .done = toplevel_handle_handle_done,
        .closed = toplevel_handle_handle_closed
};

static void toplevel_list_handle_toplevel(
    void *data,
    struct ext_foreign_toplevel_list_v1 * /* list */,
    struct ext_foreign_toplevel_handle_v1 *toplevel_handle
) {
    WaylandGlobals *globals = data;

    WrappedToplevel *toplevel = calloc(1, sizeof(WrappedToplevel));
    toplevel->handle = toplevel_handle;
    ext_foreign_toplevel_handle_v1_add_listener(
        toplevel_handle, &toplevel_handle_listener, toplevel
    );

    wl_list_insert(&globals->toplevels, &toplevel->link);
}

static void toplevel_list_handle_finished(
    void *data, struct ext_foreign_toplevel_list_v1 *list
) {
    WrappedToplevel *toplevel, *tmp;
    wl_list_for_each_safe(toplevel, tmp, &wayland_globals.toplevels, link) {
        wl_list_remove(&toplevel->link);
        wrapped_toplevel_destroy(toplevel);
    }
    ext_foreign_toplevel_list_v1_destroy(list);
    WaylandGlobals *globals = data;
    globals->ext_foreign_toplevel_list = NULL;
}

static const struct ext_foreign_toplevel_list_v1_listener
    toplevel_list_listener = {
        .toplevel = toplevel_list_handle_toplevel,
        .finished = toplevel_list_handle_finished,
};

static void registry_handle_global(
    void *data,
    struct wl_registry *registry,
    uint32_t object_id,
    const char *interface,
    uint32_t version
) {
    WaylandGlobals *globals = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        globals->compositor =
            wl_registry_bind(registry, object_id, &wl_compositor_interface, 6);
    }

    if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        globals->data_device_manager = wl_registry_bind(
            registry, object_id, &wl_data_device_manager_interface, 3
        );
    }

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        // wl_shm_release is only available from version 2 onwards.
        // But not all compositors support version 2, so bind 1 if 2 isn't
        // available.
        globals->shm = wl_registry_bind(
            registry, object_id, &wl_shm_interface, version > 1 ? 2 : 1
        );
    }

    if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        globals->subcompositor = wl_registry_bind(
            registry, object_id, &wl_subcompositor_interface, 1
        );
    }

    if (strcmp(interface, ext_data_control_manager_v1_interface.name) == 0) {
        globals->ext_data_control_manager = wl_registry_bind(
            registry, object_id, &ext_data_control_manager_v1_interface, 1
        );
    }

    if (strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0 &&
        globals->handle_toplevel_create) {
        globals->ext_foreign_toplevel_list = wl_registry_bind(
            registry, object_id, &ext_foreign_toplevel_list_v1_interface, 1
        );
        ext_foreign_toplevel_list_v1_add_listener(
            globals->ext_foreign_toplevel_list, &toplevel_list_listener, globals
        );
    }

    if (strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) ==
        0) {
        globals->ext_image_copy_capture_manager = wl_registry_bind(
            registry, object_id, &ext_image_copy_capture_manager_v1_interface, 1
        );
    }

    if (strcmp(
            interface,
            ext_foreign_toplevel_image_capture_source_manager_v1_interface.name
        ) == 0) {
        globals->ext_toplevel_capture_source_manager = wl_registry_bind(
            registry,
            object_id,
            &ext_foreign_toplevel_image_capture_source_manager_v1_interface,
            1
        );
    }

    if (strcmp(
            interface, ext_output_image_capture_source_manager_v1_interface.name
        ) == 0) {
        globals->ext_output_capture_source_manager = wl_registry_bind(
            registry,
            object_id,
            &ext_output_image_capture_source_manager_v1_interface,
            1
        );
    }

    if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        globals->cursor_shape_manager = wl_registry_bind(
            registry, object_id, &wp_cursor_shape_manager_v1_interface, 1
        );
    }

    if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        globals->fractional_scale_manager = wl_registry_bind(
            registry, object_id, &wp_fractional_scale_manager_v1_interface, 1
        );
    }

    if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        globals->viewporter =
            wl_registry_bind(registry, object_id, &wp_viewporter_interface, 1);
    }

    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        globals->layer_shell = wl_registry_bind(
            registry, object_id, &zwlr_layer_shell_v1_interface, 4
        );
    }

    if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        globals->wlr_screencopy_manager = wl_registry_bind(
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
            report_warning(
                "Multiple seats present. Handling this is "
                "unimplemented, only one will work"
            );
        } else {
            struct wl_seat *seat =
                wl_registry_bind(registry, object_id, &wl_seat_interface, 9);
            globals->seat_dispatcher = seat_dispatcher_new(seat);
        }
    }

    if (strcmp(interface, wl_output_interface.name) == 0 &&
        globals->handle_output_create) {
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

        wl_list_insert(&globals->outputs, &output->link);
    }
}

static void registry_handle_global_remove(
    void *data, struct wl_registry * /* registry */, uint32_t object_id
) {
    WaylandGlobals *globals = data;
    log_debug("Global object ID %d just got removed\n", object_id);
    WrappedOutput *output, *tmp;
    wl_list_for_each_safe(output, tmp, &globals->outputs, link) {
        if (output->object_id == object_id) {
            log_debug("... and it's an output (%s)\n", output->name);
            // It turns out that all the things that could benefit from a
            // destroy callback can't really receive it.
            wl_list_remove(&output->link);

            zxdg_output_v1_destroy(output->xdg_output);
            // I'm not sure this is necessary, but why not do it anyway
            wl_output_release(output->wl_output);
            free(output->name);
            free(output);
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};

bool find_wayland_globals(
    struct wl_display *display,
    OutputCallback create_output_callback,
    ToplevelCallback create_toplevel_callback
) {
    // initialize wayland_globals object
    memset(&wayland_globals, 0, sizeof(WaylandGlobals));
    wl_list_init(&wayland_globals.outputs);
    wl_list_init(&wayland_globals.toplevels);
    wayland_globals.handle_output_create = create_output_callback;
    wayland_globals.handle_toplevel_create = create_toplevel_callback;

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &wayland_globals);
    wl_display_roundtrip(display);

    if (wayland_globals.compositor == NULL || wayland_globals.shm == NULL ||
        wayland_globals.fractional_scale_manager == NULL ||
        wayland_globals.viewporter == NULL ||
        wayland_globals.layer_shell == NULL ||
        wayland_globals.seat_dispatcher == NULL) {
        return false;
    }

    if (create_output_callback && wayland_globals.output_manager == NULL) {
        return false;
    }

    if (create_toplevel_callback &&
        wayland_globals.ext_foreign_toplevel_list == NULL) {
        return false;
    }

    return true;
}

void cleanup_wayland_globals() {
    WrappedOutput *output, *tmp;
    wl_list_for_each_safe(output, tmp, &wayland_globals.outputs, link) {
        free(output->name);
        zxdg_output_v1_destroy(output->xdg_output);
        wl_output_release(output->wl_output);
        wl_list_remove(&output->link);
        free(output);
    }

    seat_dispatcher_destroy(wayland_globals.seat_dispatcher);

    // A couple of the built-in singleton globals do not have destructors:
    // - wl_compositor
    // - wl_data_device_manager

    if (wayland_globals.data_device) {
        wl_data_device_release(wayland_globals.data_device);
    }
    if (wl_shm_get_version(wayland_globals.shm) >= 2) {
        wl_shm_release(wayland_globals.shm);
    }
    wl_subcompositor_destroy(wayland_globals.subcompositor);
    if (wayland_globals.ext_foreign_toplevel_list) {
        ext_foreign_toplevel_list_v1_stop(
            wayland_globals.ext_foreign_toplevel_list
        );
    }
    if (wayland_globals.ext_image_copy_capture_manager) {
        ext_image_copy_capture_manager_v1_destroy(
            wayland_globals.ext_image_copy_capture_manager
        );
    }
    if (wayland_globals.ext_output_capture_source_manager) {
        ext_output_image_capture_source_manager_v1_destroy(
            wayland_globals.ext_output_capture_source_manager
        );
    }
    wp_cursor_shape_manager_v1_destroy(wayland_globals.cursor_shape_manager);
    wp_fractional_scale_manager_v1_destroy(
        wayland_globals.fractional_scale_manager
    );
    wp_viewporter_destroy(wayland_globals.viewporter);
    zwlr_layer_shell_v1_destroy(wayland_globals.layer_shell);
    if (wayland_globals.wlr_screencopy_manager) {
        zwlr_screencopy_manager_v1_destroy(
            wayland_globals.wlr_screencopy_manager
        );
    }
    if (wayland_globals.output_manager) {
        zxdg_output_manager_v1_destroy(wayland_globals.output_manager);
    }
}

bool is_output_valid(WrappedOutput *test_output) {
    WrappedOutput *it;
    wl_list_for_each(it, &wayland_globals.outputs, link) {
        if (it == test_output) {
            return true;
        }
    }
    return false;
}

bool is_toplevel_valid(WrappedToplevel *test_toplevel) {
    WrappedToplevel *it;
    wl_list_for_each(it, &wayland_globals.toplevels, link) {
        if (it == test_toplevel) {
            return true;
        }
    }
    return false;
}
