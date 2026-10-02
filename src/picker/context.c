#include "context.h"
#include "image.h"
#include "link-buffer.h"
#include "log.h"
#include "wayland/globals.h"
#include "wayland/screen-capture.h"
#include "wayland/seat.h"
#include <xkbcommon/xkbcommon.h>

static RenderDisplayList picker_surface_draw(void *data) {
    PickerSurface *picker = data;

    link_buffer_reset(picker->command_arena);
    RenderDisplayList dl = {.arena = picker->command_arena};

    // Toplevel pickers do not take an output screenshot
    // to use as background
    if (picker->background) {
        RENDER_RECT(
            dl,
            .bounds =
                {0,
                 0,
                 picker->surface->device_width,
                 picker->surface->device_height},
            .color = RENDER_COLOR_DEFAULT,
            .texture = capture_frame_get_texture(picker->background),
            .uv = RENDER_UV_DEFAULT
        );
    } else {
        RenderColor empty = {0, 0, 0, 0};
        RENDER_CLEAR(dl, empty);
    }

    picker->vtable->draw(picker, &dl);

    return dl;
}

static void picker_surface_destroy(PickerSurface *picker);
static void picker_surface_close(void *data) {
    PickerSurface *picker = data;
    PickerContext *ctx = picker->ctx;
    // this also destroys the capture frame
    picker_surface_destroy(picker);
    // If this is the final picker, we should clean up.
    if (wl_list_empty(&ctx->pickers)) {
        capture_frame_destroy_list(ctx->captures);
        report_error_fatal("all pickers closed unexpectedly");
    }
}

static void picker_surface_scale(void *data, uint32_t scale) {
    PickerSurface *picker = data;

    // TODO: If switching is possible, we want to notify ALL the picker types
    if (picker->vtable->scale) {
        picker->vtable->scale(picker, scale);
    }
}

static void picker_surface_resize(void *data) {
    PickerSurface *picker = data;

    if (picker->vtable->resize) {
        picker->vtable->resize(picker);
    }
}

static void picker_surface_mouse(void *data, MouseEvent ev) {
    PickerSurface *picker = data;

    if (picker->vtable->mouse) {
        picker->vtable->mouse(picker, ev);
    }
}

static void picker_surface_keyboard(void *data, KeyboardEvent ev) {
    PickerSurface *picker = data;

    if (ev.keysym == XKB_KEY_Escape && ev.type == KEYBOARD_EVENT_PRESS &&
        picker->surface->wl_surface == ev.focus) {
        picker_context_finish(picker, PICKER_FINISH_REASON_CANCELLED, NULL);
        return;
    }

    if (picker->vtable->keyboard) {
        picker->vtable->keyboard(picker, ev);
    }
}

static SeatListener picker_surface_seat_listener = {
    .mouse = picker_surface_mouse,
    .keyboard = picker_surface_keyboard,
};

/**
 * A picker takes ownership of its captured output frame.
 * The output can be NULL, which means "default" (typically the focused output).
 * The frame can be NULL, which means "no background".
 */
static PickerSurface *picker_surface_new(
    PickerContext *ctx,
    const PickerVTable *vtable,
    WrappedOutput *output,
    CaptureFrame *frame
) {
    PickerSurface *picker = calloc(1, sizeof(PickerSurface));
    picker->ctx = ctx;
    picker->vtable = vtable;
    picker->surface = overlay_surface_new(
        output,
        frame ? frame->compatible_format : IMAGE_FORMAT_ARGB8888,
        (OverlaySurfaceHandlers){
            .draw = picker_surface_draw,
            .close = picker_surface_close,
            .scale = picker_surface_scale,
            .resize = picker_surface_resize,
        },
        picker
    );
    picker->background = frame;
    picker->command_arena = link_buffer_new(LINK_BUFFER_ARENA_SIZE);

    seat_dispatcher_add_listener(
        wayland_globals.seat_dispatcher,
        picker->surface,
        &picker_surface_seat_listener,
        picker
    );

    return picker;
}

static void picker_surface_destroy(PickerSurface *picker) {
    seat_dispatcher_remove_listener(
        wayland_globals.seat_dispatcher, picker->surface
    );

    // Destroy every picker in case switches happened.
    region_picker_destroy(picker);
    output_picker_destroy(picker);
    toplevel_picker_destroy(picker);

    link_buffer_destroy(picker->command_arena);
    overlay_surface_destroy(picker->surface);
    if (picker->background) {
        capture_frame_destroy(picker->background);
    }

    wl_list_remove(&picker->link);
    free(picker);
}

static void picker_surface_spawn(
    PickerContext *ctx,
    const PickerVTable *ops,
    WrappedOutput *output,
    CaptureFrame *frame
) {
    PickerSurface *picker = picker_surface_new(ctx, ops, output, frame);
    if (ops->init) {
        ops->init(picker);
    }
    if (ops->enter) {
        ops->enter(picker);
    }
    wl_list_insert(&ctx->pickers, &picker->link);
}

static const PickerVTable *picker_vtable_for_type(PickerType type) {
    switch (type) {
    case PICKER_TYPE_REGION:
        return &region_picker_vtable;
    case PICKER_TYPE_OUTPUT:
        return &output_picker_vtable;
    case PICKER_TYPE_TOPLEVEL:
        return &toplevel_picker_vtable;
    default:
        REPORT_UNHANDLED("picker type", "%d", type);
    }
}

void picker_context_init(
    PickerContext *ctx,
    PickerType type,
    struct wl_list *captures,
    const PickerHost *host
) {
    ctx->captures = captures;
    ctx->host = host;
    wl_list_init(&ctx->pickers);

    const PickerVTable *ops = picker_vtable_for_type(type);
    if (ops->per_output) {
        CaptureFrame *frame;
        wl_list_for_each(frame, captures, link) {
            if (frame->type == CAPTURE_FRAME_TYPE_OUTPUT) {
                picker_surface_spawn(ctx, ops, frame->output, frame);
            }
        }
    } else {
        picker_surface_spawn(ctx, ops, NULL, NULL);
    }

    if (wl_list_empty(&ctx->pickers)) {
        report_error_fatal("no captures to spawn pickers from");
    }
}

void picker_context_finish(
    PickerSurface *picker, PickerFinishReason reason, Image *result
) {
    PickerContext *ctx = picker->ctx;

    switch (reason) {
    case PICKER_FINISH_REASON_SELECTED: {
        PickerSurface *surface, *tmp;
        wl_list_for_each_safe(surface, tmp, &ctx->pickers, link) {
            picker_surface_destroy(surface);
        }
        // this takes ownership of result
        ctx->host->finalize(result);
        break;
    }
    case PICKER_FINISH_REASON_CANCELLED: {
        PickerSurface *surface, *tmp;
        wl_list_for_each_safe(surface, tmp, &ctx->pickers, link) {
            picker_surface_destroy(surface);
        }
        ctx->host->cancel();
        break;
    }
    default:
        REPORT_UNHANDLED("picker finish reason", "%d", reason);
    }

    // destroy any unused captures
    capture_frame_destroy_list(ctx->captures);
}
