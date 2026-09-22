#include "context.h"
#include "link-buffer.h"
#include "log.h"
#include "picker/common.h"
#include "wayland/clipboard.h"
#include "wayland/globals.h"
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
    // TODO: figure out how to make closing work or sth
    // both region and output just called the finish_callback with reason
    // DESTROYED
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

static PickerSurface *
picker_surface_new(WrappedOutput *output, CaptureEntry *entry) {
    PickerSurface *picker = calloc(1, sizeof(PickerSurface));
    picker->surface = overlay_surface_new(
        output,
        entry->frame->pixel_format,
        (OverlaySurfaceHandlers){
            .draw = picker_surface_draw,
            .close = picker_surface_close,
            .scale = picker_surface_scale,
        },
        picker
    );
    picker->background = entry->frame;
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

    free(picker);
}

void capture_entry_destroy(CaptureEntry *entry) {
    capture_frame_destroy(entry->frame);
    wl_list_remove(&entry->link);
    free(entry);
}

void capture_entry_destroy_all(struct wl_list *captures) {
    CaptureEntry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, captures, link) {
        capture_entry_destroy(entry);
    }
}

void picker_context_init(
    PickerContext *ctx, PickerType type, struct wl_list *captures
) {
    ctx->captures = captures;
    wl_list_init(&ctx->surfaces);

    CaptureEntry *entry;
    wl_list_for_each(entry, captures, link) {
        if (entry->frame_type == CAPTURE_ENTRY_TYPE_OUTPUT) {
            PickerSurface *picker = picker_surface_new(entry->output, entry);
            switch (type) {
            case PICKER_TYPE_REGION:
                region_picker_init(picker);
                break;
            // TODO
            default:
                REPORT_UNHANDLED("picker type", "%d", type);
            }

            wl_list_insert(&ctx->surfaces, &picker->link);
        }
    }
}

ClipboardCopyOffer *finalize_picker_setup();
void finalize_picker_cancel();
void finalize_picker_finish(Image *result, ClipboardCopyOffer *copy_offer);

void picker_context_finish(PickerSurface *picker, PickerFinishReason reason) {
    switch (reason) {
    case PICKER_FINISH_REASON_DESTROYED:
        picker_surface_destroy(picker);
        // TODO: clean up the entry
        // that's it
        return;
    case PICKER_FINISH_REASON_SELECTED: {
        ClipboardCopyOffer *clipboard_data = finalize_picker_setup();
        // TODO: destroy the picker and capture entry
        capture_entry_destroy(entry);
        finalize_picker_finish(NULL, clipboard_data);
        capture_entry_destroy_all(ctx->captures);
        //
        break;
    }
    case PICKER_FINISH_REASON_CANCELLED:
        finalize_picker_cancel();
        // TODO: destroy stuff
        break;
    }

    // TODO: destroy the picker context as well?
}
