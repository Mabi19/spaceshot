#include "context.h"
#include "link-buffer.h"
#include "log.h"
#include "picker/common.h"
#include "wayland/globals.h"
#include "wayland/screen-capture.h"
#include "wayland/seat.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

static RenderDisplayList picker_surface_draw(void *data) {
    PickerSurface *picker = data;
    switch (picker->type) {
    case PICKER_TYPE_REGION:
        return region_picker_draw(picker);
    // TODO
    default:
        REPORT_UNHANDLED("picker type", "%d", picker->type);
    }
}

static void picker_surface_close(void *data) {
    PickerSurface *picker = data;
    picker_context_finish(picker, PICKER_FINISH_REASON_DESTROYED, (BBox){});
}

static void picker_surface_scale(void *data, uint32_t scale) {
    PickerSurface *picker = data;
    switch (picker->type) {
    case PICKER_TYPE_REGION:
        if (!picker->smart_border &&
            config_get()->region.selection_border_color.type ==
                CONFIG_REGION_SELECTION_BORDER_COLOR_SMART) {
            picker->smart_border =
                smart_border_context_start(picker->background, scale);
        }
        break;
    // TODO
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
    // TODO
    default:
        REPORT_UNHANDLED("picker type", "%d", picker->type);
    }
}

static void picker_surface_keyboard(void *data, KeyboardEvent ev) {
    PickerSurface *picker = data;

    if (xkb_state_mod_name_is_active(
            wayland_globals.seat_dispatcher->keyboard_data.state,
            XKB_MOD_NAME_CTRL,
            XKB_STATE_MODS_EFFECTIVE
        ) > 0 &&
        ev.keysym == XKB_KEY_Escape) {
        // TODO: remove this once I can be reasonably sure that exiting works
        // TODO: also ew we need an easier way to check mods
        exit(0);
    }

    switch (picker->type) {
    case PICKER_TYPE_REGION:
        region_picker_handle_keyboard(picker, ev);
        break;
    // TODO
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

    if (picker->smart_border) {
        if (picker->smart_border->result_texture) {
            picker->surface->renderer->texture_destroy(
                picker->smart_border->result_texture
            );
        }
        smart_border_context_unref(picker->smart_border);
    }
    link_buffer_destroy(picker->command_arena);
    overlay_surface_destroy(picker->surface);
    capture_frame_destroy(picker->background);

    wl_list_remove(&picker->link);
    free(picker);
}

void picker_context_init(
    PickerContext *ctx, PickerType type, struct wl_list *captures
) {
    ctx->captures = captures;
    wl_list_init(&ctx->pickers);

    CaptureFrame *frame;
    wl_list_for_each(frame, captures, link) {
        if (frame->type == CAPTURE_FRAME_TYPE_OUTPUT) {
            PickerSurface *picker =
                picker_surface_new(ctx, frame->output, frame);
            switch (type) {
            case PICKER_TYPE_REGION:
                region_picker_init(picker);
                break;
            // TODO
            default:
                REPORT_UNHANDLED("picker type", "%d", type);
            }

            wl_list_insert(&ctx->pickers, &picker->link);
        }
    }
}

void finalize_picker_setup();
void finalize_picker_cancel();
void finalize_picker_finish(Image *result);

void picker_context_finish(
    PickerSurface *picker, PickerFinishReason reason, BBox crop_box
) {
    PickerContext *ctx = picker->ctx;

    if (reason == PICKER_FINISH_REASON_DESTROYED) {
        // this also destroys the entry
        picker_surface_destroy(picker);
        // If this is the final picker, we should clean up.
        if (wl_list_empty(&ctx->pickers)) {
            capture_frame_destroy_list(ctx->captures);
            report_error_fatal("all pickers closed unexpectedly");
        }
    } else {
        switch (reason) {
        case PICKER_FINISH_REASON_SELECTED: {
            finalize_picker_setup();
            Image *img = capture_frame_get_image(picker->background);
            // with a full-image BBox, this is essentially just a copy
            Image *cropped = image_crop(
                img, crop_box.x, crop_box.y, crop_box.width, crop_box.height
            );
            // img is destroyed here and cropped is destroyed by the finalize
            picker_surface_destroy(picker);
            finalize_picker_finish(cropped);
            break;
        }
        case PICKER_FINISH_REASON_CANCELLED:
            picker_surface_destroy(picker);
            finalize_picker_cancel();
            break;
        default:
            REPORT_UNHANDLED("picker finish reason", "%d", reason);
        }

        // destroy the rest
        PickerSurface *picker, *tmp;
        wl_list_for_each_safe(picker, tmp, &ctx->pickers, link) {
            picker_surface_destroy(picker);
        }
        capture_frame_destroy_list(ctx->captures);
    }
}
