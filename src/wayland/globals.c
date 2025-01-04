#include "globals.h"
#include "stb_ds.h"
#include <stdio.h>
#include <string.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wlr-screencopy-client.h>

WaylandGlobals wayland_globals;

static void registry_handle_global(
    void *data,
    struct wl_registry *registry,
    uint32_t object_id,
    const char *interface,
    uint32_t /* version */
) {
    WaylandGlobals *globals = (WaylandGlobals *)data;

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

    if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        globals->screencopy_manager = wl_registry_bind(
            registry, object_id, &zwlr_screencopy_manager_v1_interface, 3
        );
    }

    if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *output =
            wl_registry_bind(registry, object_id, &wl_output_interface, 4);

        arrput(globals->outputs, output);

        if (globals->handle_output_create) {
            globals->handle_output_create(output);
        }
    }
}

static void registry_handle_global_remove(
    void * /* data */, struct wl_registry * /* registry */, uint32_t object_id
) {
    // TODO: Is this a monitor?
    printf("Global object ID %d just got removed\n", object_id);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};

int find_wayland_globals(
    struct wl_display *display, OutputCallback output_callback
) {
    // initialize wayland_globals object
    memset(&wayland_globals, 0, sizeof(WaylandGlobals));

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &wayland_globals);
    wl_display_roundtrip(display);

    // call the callback when everything's obtained
    int output_count = arrlen(wayland_globals.outputs);
    for (int i = 0; i < output_count; i++) {
        output_callback(wayland_globals.outputs[i]);
    }

    // if any new outputs are connected after this, handle them
    wayland_globals.handle_output_create = output_callback;

    if (wayland_globals.compositor == NULL ||
        wayland_globals.screencopy_manager == NULL) {
        return 0;
    }

    return 1;
}
