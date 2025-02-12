#include "globals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-util.h>
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

        printf("Adding output %p\n", (void *)output);
        OutputListElement *element = malloc(sizeof(OutputListElement));
        element->output = output;
        wl_list_insert(&globals->outputs, &element->link);

        if (globals->handle_output_create) {
            globals->handle_output_create(output);
        }
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
    wl_list_init(&wayland_globals.outputs);

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &wayland_globals);
    wl_display_roundtrip(display);

    if (wayland_globals.compositor == NULL ||
        wayland_globals.screencopy_manager == NULL) {
        return false;
    }

    // call the callback when everything's obtained
    OutputListElement *output_element;
    wl_list_for_each(output_element, &wayland_globals.outputs, link) {
        output_callback(output_element->output);
    }

    // if any new outputs are connected after this, handle them
    wayland_globals.handle_output_create = output_callback;

    return true;
}
