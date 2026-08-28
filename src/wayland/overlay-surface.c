#include "overlay-surface.h"
#include "log.h"
#include "render/renderer.h"
#include "wayland/globals.h"
#include <assert.h>
#include <cursor-shape-client.h>
#include <fractional-scale-client.h>
#include <memory.h>
#include <stdlib.h>
#include <viewporter-client.h>
#include <wayland-client-protocol.h>
#include <wlr-layer-shell-client.h>

static void recompute_device_size(OverlaySurface *surface) {
    surface->device_width =
        round((surface->logical_width * surface->scale) / 120.0);
    surface->device_height =
        round((surface->logical_height * surface->scale) / 120.0);
}

/**
 * Call the draw_callback immediately. Prefer using
 * overlay_surface_queue_draw() over this if possible.
 * This function calls wl_surface_commit.
 */
static void overlay_surface_draw_immediate(OverlaySurface *surface) {
    if (surface->handlers.draw) {
        RenderDisplayList dl = surface->handlers.draw(surface->user_data);
        surface->renderer->draw(surface->canvas, dl);
    } else {
        surface->handlers.manual_render(surface->user_data);
        wl_surface_commit(surface->wl_surface);
    }
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
    surface->has_configured = true;

    surface->logical_width = width;
    surface->logical_height = height;
    wp_viewport_set_destination(
        surface->viewport, surface->logical_width, surface->logical_height
    );
    recompute_device_size(surface);
    // TODO: stop checking when manual_render is gone
    if (surface->handlers.draw) {
        if (!surface->canvas) {
            surface->canvas = surface->renderer->canvas_new(
                surface->wl_surface,
                surface->device_width,
                surface->device_height,
                surface->pixel_format
            );
        } else {
            surface->renderer->canvas_resize(
                surface->canvas, surface->device_width, surface->device_height
            );
        }
    }

    overlay_surface_draw_immediate(surface);
}

static void overlay_surface_handle_closed(
    void *data, struct zwlr_layer_surface_v1 * /* layer_surface */
) {
    OverlaySurface *surface = data;
    surface->handlers.close(surface->user_data);
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
    if (surface->canvas) {
        surface->renderer->canvas_resize(
            surface->canvas, surface->device_width, surface->device_height
        );
    }
    if (surface->handlers.scale) {
        surface->handlers.scale(surface->user_data, scale);
    }
    if (surface->has_configured) {
        overlay_surface_draw_immediate(surface);
    }
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener =
    {.preferred_scale = preferred_scale_changed};

OverlaySurface *overlay_surface_new(
    WrappedOutput *output,
    ImageFormat pixel_format,
    OverlaySurfaceHandlers handlers,
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
    result->cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    result->renderer = renderer_get_default();
    result->handlers = handlers;
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

static void draw_immediate_and_request_frame(OverlaySurface *);

static void overlay_surface_handle_frame(
    void *data, struct wl_callback *callback, uint32_t /* timestamp */
) {
    wl_callback_destroy(callback);

    OverlaySurface *surface = data;
    surface->frame_callback = NULL;
    if (surface->has_queued_render) {
        // request a frame callback again, if another render is queued
        draw_immediate_and_request_frame(surface);
        surface->has_queued_render = false;
    } else {
        // no frame queued and we've waited, so next time we can draw
        // immediately
        surface->has_requested_frame = false;
    }
}

static struct wl_callback_listener frame_callback_listener = {
    .done = overlay_surface_handle_frame
};

static void draw_immediate_and_request_frame(OverlaySurface *surface) {
    surface->has_requested_frame = true;
    assert(surface->frame_callback == NULL);
    surface->frame_callback = wl_surface_frame(surface->wl_surface);
    wl_callback_add_listener(
        surface->frame_callback, &frame_callback_listener, surface
    );
    overlay_surface_draw_immediate(surface);
}

void overlay_surface_queue_draw(OverlaySurface *surface) {
    if (!surface->has_configured) {
        return;
    }

    // if we're not waiting on anything, draw right now
    if (!surface->has_requested_frame) {
        draw_immediate_and_request_frame(surface);
    } else {
        // render at the frame() callback
        surface->has_queued_render = true;
    }
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
    // TODO: can be removed once manual_render is gone
    if (surface->canvas) {
        surface->renderer->canvas_destroy(surface->canvas);
    }

    wp_fractional_scale_v1_destroy(surface->fractional_scale);
    wp_viewport_destroy(surface->viewport);
    zwlr_layer_surface_v1_destroy(surface->layer_surface);
    wl_surface_destroy(surface->wl_surface);

    // If a frame is pending, cancel the callback so that it doesn't try to draw
    // after the surface was destroyed
    if (surface->frame_callback) {
        wl_callback_destroy(surface->frame_callback);
    }

    free(surface);
}
