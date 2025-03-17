#include "overlay-surface.h"
#include "log.h"
#include "wayland/globals.h"
#include "wayland/render.h"
#include <assert.h>
#include <cairo.h>
#include <fractional-scale-client.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <viewporter-client.h>
#include <wayland-client-protocol.h>
#include <wlr-layer-shell-client.h>

static RenderBuffer *get_unused_buffer(OverlaySurface *surface) {
    // first, try to get an existing buffer
    for (size_t i = 0; i < OVERLAY_SURFACE_BUFFER_COUNT; i++) {
        if (!surface->buffers[i]) {
            continue;
        }
        RenderBuffer *test_buf = surface->buffers[i];
        if (!test_buf->is_busy &&
            test_buf->shm->width == surface->device_width &&
            test_buf->shm->height == surface->device_height) {
            return test_buf;
        }
    }

    // second, try to create a new one in an empty spot
    // or overwrite one with the wrong size
    for (size_t i = 0; i < OVERLAY_SURFACE_BUFFER_COUNT; i++) {
        if (surface->buffers[i]) {
            if (surface->buffers[i]->shm->width == surface->device_width &&
                surface->buffers[i]->shm->height == surface->device_height) {
                continue;
            }
            log_debug("destroyed buffer #%zu\n", i);
            render_buffer_destroy(surface->buffers[i]);
        }
        surface->buffers[i] = render_buffer_new(
            surface->device_width, surface->device_height, surface->pixel_format
        );
        log_debug("created buffer #%zu\n", i);
        return surface->buffers[i];
    }

    // last resort: overwrite the first one
    if (surface->buffers[0]) {
        render_buffer_destroy(surface->buffers[0]);
    }
    surface->buffers[0] = render_buffer_new(
        surface->device_width, surface->device_height, surface->pixel_format
    );
    log_debug("overwrote buffer #0 (last resort)\n");

    return surface->buffers[0];
}

static void recompute_device_size(OverlaySurface *surface) {
    surface->device_width =
        round((surface->logical_width * surface->scale) / 120.0);
    surface->device_height =
        round((surface->logical_height * surface->scale) / 120.0);
}

/** Call the draw_callback immediately. Prefer using
 * overlay_surface_queue_draw() over this if possible. */
static void overlay_surface_draw_immediate(OverlaySurface *surface) {
    RenderBuffer *draw_buf = get_unused_buffer(surface);
    bool did_update = surface->draw_callback(surface->user_data, draw_buf->cr);
    if (!did_update) {
        return;
    }
    cairo_surface_flush(draw_buf->cairo_surface);
    render_buffer_attach_to_surface(draw_buf, surface->wl_surface);
    wl_surface_commit(surface->wl_surface);
}

static void overlay_surface_handle_configure(
    void *data,
    struct zwlr_layer_surface_v1 * /* layer_surface */,
    uint32_t serial,
    uint32_t width,
    uint32_t height
) {
    OverlaySurface *surface = data;
    log_debug(
        "Received configure for surface %p with w = %d, h = %d\n",
        data,
        width,
        height
    );

    zwlr_layer_surface_v1_ack_configure(surface->layer_surface, serial);

    surface->logical_width = width;
    surface->logical_height = height;
    wp_viewport_set_destination(
        surface->viewport, surface->logical_width, surface->logical_height
    );
    recompute_device_size(surface);

    overlay_surface_draw_immediate(surface);
}

static void overlay_surface_handle_closed(
    void *data, struct zwlr_layer_surface_v1 * /* layer_surface */
) {
    OverlaySurface *surface = data;
    surface->close_callback(surface->user_data);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = overlay_surface_handle_configure,
    .closed = overlay_surface_handle_closed,
};

static void preferred_scale_changed(
    void *data,
    struct wp_fractional_scale_v1 * /* fractional_scale */,
    uint32_t scale
) {
    OverlaySurface *surface = data;
    log_debug(
        "Received fractional scale for surface %p with scale = %d\n",
        data,
        scale
    );
    surface->scale = scale;
    recompute_device_size(surface);

    overlay_surface_draw_immediate(surface);
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener =
    {.preferred_scale = preferred_scale_changed};

OverlaySurface *overlay_surface_new(
    WrappedOutput *output,
    ImageFormat pixel_format,
    OverlaySurfaceDrawCallback draw_callback,
    OverlaySurfaceCloseCallback close_callback,
    void *user_data
) {
    OverlaySurface *result = calloc(1, sizeof(OverlaySurface));
    result->pixel_format = pixel_format;
    result->wl_surface =
        wl_compositor_create_surface(wayland_globals.compositor);
    result->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        wayland_globals.layer_shell,
        result->wl_surface,
        output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "spaceshot"
    );
    result->viewport = wp_viewporter_get_viewport(
        wayland_globals.viewporter, result->wl_surface
    );
    result->scale = 120;
    result->fractional_scale =
        wp_fractional_scale_manager_v1_get_fractional_scale(
            wayland_globals.fractional_scale_manager, result->wl_surface
        );
    result->draw_callback = draw_callback;
    result->close_callback = close_callback;
    result->user_data = user_data;

    wp_fractional_scale_v1_add_listener(
        result->fractional_scale, &fractional_scale_listener, result
    );
    zwlr_layer_surface_v1_add_listener(
        result->layer_surface, &layer_surface_listener, result
    );

    const enum zwlr_layer_surface_v1_anchor ANCHOR =
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(result->layer_surface, ANCHOR);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        result->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
    );
    // do not honor other surfaces' exclusive zones
    zwlr_layer_surface_v1_set_exclusive_zone(result->layer_surface, -1);
    wl_surface_commit(result->wl_surface);

    return result;
}

static void overlay_surface_handle_frame(
    void *data, struct wl_callback * /* callback */, uint32_t /* timestamp */
) {
    OverlaySurface *surface = data;
    overlay_surface_draw_immediate(surface);
    surface->has_requested_frame = false;
}

static struct wl_callback_listener frame_callback_listener = {
    .done = overlay_surface_handle_frame
};

void overlay_surface_queue_draw(OverlaySurface *surface) {
    if (surface->has_requested_frame) {
        return;
    }
    surface->has_requested_frame = true;
    struct wl_callback *callback = wl_surface_frame(surface->wl_surface);
    wl_callback_add_listener(callback, &frame_callback_listener, surface);
    wl_surface_commit(surface->wl_surface);
}

void overlay_surface_damage(OverlaySurface *surface, BBox damage_box) {
    wl_surface_damage_buffer(
        surface->wl_surface,
        damage_box.x,
        damage_box.y,
        damage_box.width,
        damage_box.height
    );
}

void overlay_surface_destroy(OverlaySurface *surface) {
    for (size_t i = 0; i < OVERLAY_SURFACE_BUFFER_COUNT; i++) {
        if (surface->buffers[i]) {
            render_buffer_destroy(surface->buffers[i]);
        }
    }

    wp_fractional_scale_v1_destroy(surface->fractional_scale);
    wp_viewport_destroy(surface->viewport);
    zwlr_layer_surface_v1_destroy(surface->layer_surface);
    wl_surface_destroy(surface->wl_surface);

    free(surface);
}
