#pragma once
#include "wayland/output.h"
#include "wayland/render.h"
#include <wayland-client.h>
#include <wlr-layer-shell-client.h>

constexpr size_t OVERLAY_SURFACE_BUFFER_COUNT = 2;

typedef struct {
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    uint32_t width;
    uint32_t height;
    RenderBuffer *buffers[OVERLAY_SURFACE_BUFFER_COUNT];
} OverlaySurface;

OverlaySurface *overlay_surface_new(WrappedOutput *output);
