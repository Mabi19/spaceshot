#include "context.h"
#include "link-buffer.h"
#include "log.h"
#include "picker/output.h"
#include "wayland/globals.h"
#include "wayland/screen-capture.h"
#include "wayland/seat.h"
#include <xkbcommon/xkbcommon.h>

static RenderDisplayList picker_surface_draw(void *data) {
    PickerSurface *picker = data;

    link_buffer_reset(picker->command_arena);
    RenderDisplayList dl = {.arena = picker->command_arena};

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

    switch (picker->type) {
    case PICKER_TYPE_REGION:
        region_picker_draw(picker, &dl);
        break;
    case PICKER_TYPE_OUTPUT:
        output_picker_draw(picker, &dl);
        break;
    default:
        REPORT_UNHANDLED("picker type", "%d", picker->type);
    }

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

    // TODO: if switching is possible, we want to notify ALL picker types
    // so they don't miss out on scale updates.
    switch (picker->type) {
    case PICKER_TYPE_REGION:
        region_picker_handle_scale(picker, scale);
        break;
    case PICKER_TYPE_OUTPUT:
        output_picker_handle_scale(picker, scale);
        break;
    default:
        REPORT_UNHANDLED("picker type", "%d", picker->type);
    }
}

static void picker_surface_mouse(void *data, MouseEvent ev) {
    PickerSurface *picker = data;

    switch (picker->type) {
    case PICKER_TYPE_REGION:
        region_picker_handle_mouse(picker, ev);
        break;
    case PICKER_TYPE_OUTPUT:
        output_picker_handle_mouse(picker, ev);
        break;
    default:
        REPORT_UNHANDLED("picker type", "%d", picker->type);
    }
}

static void picker_surface_keyboard(void *data, KeyboardEvent ev) {
    PickerSurface *picker = data;

    if (ev.keysym == XKB_KEY_Escape && ev.type == KEYBOARD_EVENT_PRESS &&
        picker->surface->wl_surface == ev.focus) {
        picker_context_finish(picker, PICKER_FINISH_REASON_CANCELLED, NULL);
        return;
    }

    switch (picker->type) {
    case PICKER_TYPE_REGION:
        region_picker_handle_keyboard(picker, ev);
        break;
    case PICKER_TYPE_OUTPUT:
        // no picker-specific keyboard actions
        break;
    default:
        REPORT_UNHANDLED("picker type", "%d", picker->type);
    }
}

static SeatListener picker_surface_seat_listener = {
    .mouse = picker_surface_mouse,
    .keyboard = picker_surface_keyboard,
};

/**
 * A picker takes ownership of its captured output frame.
 */
static PickerSurface *picker_surface_new(
    PickerContext *ctx, WrappedOutput *output, CaptureFrame *frame
) {
    PickerSurface *picker = calloc(1, sizeof(PickerSurface));
    picker->ctx = ctx;
    picker->surface = overlay_surface_new(
        output,
        frame->pixel_format,
        (OverlaySurfaceHandlers){
            .draw = picker_surface_draw,
            .close = picker_surface_close,
            .scale = picker_surface_scale,
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

    region_picker_destroy(picker);
    output_picker_destroy(picker);

    link_buffer_destroy(picker->command_arena);
    overlay_surface_destroy(picker->surface);
    capture_frame_destroy(picker->background);

    wl_list_remove(&picker->link);
    free(picker);
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

    CaptureFrame *frame;
    wl_list_for_each(frame, captures, link) {
        if (frame->type == CAPTURE_FRAME_TYPE_OUTPUT) {
            PickerSurface *picker =
                picker_surface_new(ctx, frame->output, frame);
            picker->type = type;
            switch (type) {
            case PICKER_TYPE_REGION:
                // region picker state (the smart border)
                // is initialized later, once the scale is available
                region_picker_enter(picker);
                break;
            case PICKER_TYPE_OUTPUT:
                output_picker_init(picker, frame->output);
                output_picker_enter(picker);
                break;
            default:
                REPORT_UNHANDLED("picker type", "%d", type);
            }

            wl_list_insert(&ctx->pickers, &picker->link);
        }
    }

    if (wl_list_empty(&ctx->pickers)) {
        report_error_fatal("no output captures to spawn pickers from");
    }
}

void picker_context_finish(
    PickerSurface *picker, PickerFinishReason reason, Image *result
) {
    PickerContext *ctx = picker->ctx;

    switch (reason) {
    case PICKER_FINISH_REASON_SELECTED: {
        ctx->host->finalize_prepare();
        PickerSurface *surface, *tmp;
        wl_list_for_each_safe(surface, tmp, &ctx->pickers, link) {
            picker_surface_destroy(surface);
        }
        // this takes ownership of result
        ctx->host->finalize_finish(result);
        break;
    }
    case PICKER_FINISH_REASON_CANCELLED: {
        PickerSurface *surface, *tmp;
        wl_list_for_each_safe(surface, tmp, &ctx->pickers, link) {
            picker_surface_destroy(surface);
        }
        ctx->host->finalize_cancel();
        break;
    }
    default:
        REPORT_UNHANDLED("picker finish reason", "%d", reason);
    }

    // destroy any unused captures
    capture_frame_destroy_list(ctx->captures);
}
