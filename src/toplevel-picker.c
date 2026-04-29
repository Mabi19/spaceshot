#include "toplevel-picker.h"
#include "cursor-shape-client.h"
#include "image.h"
#include "log.h"
#include "picker-common.h"
#include "wayland/globals.h"
#include "wayland/overlay-surface.h"
#include "wayland/seat.h"
#include "wayland/shared-memory.h"
#include <xkbcommon/xkbcommon-keysyms.h>

// TODO: handle keyboard.
// TODO: subsurface all the things
// each entry is a subsurface for the highlight, with a thumbnail subsurface and
// label subsurface
// actually idk if cropping a supersurface also crops the subsurface.
// in that case, the search bar should just be a sticky overlay
//? How does moving subsurfaces out of the layer surface work?

static void toplevel_picker_render(void *data) {
    ToplevelPicker *picker = data;
    // The overlay surface automagically scales the 1x1 to the surface logical
    // size via wp_viewporter (because this is how fractional scaling happens).
    wl_surface_attach(
        picker->surface->wl_surface, picker->background_buf->wl_buffer, 0, 0
    );
    overlay_surface_damage(
        picker->surface, (BBox){.x = 0, .y = 0, .width = 1, .height = 1}
    );
}

static void toplevel_picker_handle_surface_close(void *data) {
    ToplevelPicker *picker = data;
    picker->finish_callback(picker, PICKER_FINISH_REASON_DESTROYED, NULL);
}

static void toplevel_picker_handle_mouse_background(void *data, MouseEvent ev) {
    ToplevelPicker *picker = data;
    if (ev.focus == picker->surface->wl_surface &&
        ev.buttons_released & POINTER_BUTTON_LEFT) {
        picker->finish_callback(picker, PICKER_FINISH_REASON_CANCELLED, NULL);
    }
}

static void toplevel_picker_handle_keyboard(void *data, KeyboardEvent ev) {
    ToplevelPicker *picker = data;
    if (ev.focus != picker->surface->wl_surface) {
        return;
    }
    switch (ev.keysym) {
    case XKB_KEY_Escape:
        if (ev.type == KEYBOARD_EVENT_RELEASE) {
            picker->finish_callback(
                picker, PICKER_FINISH_REASON_CANCELLED, NULL
            );
        }
        break;
    }
}

static SeatListener toplevel_picker_seat_listener = {
    .mouse = toplevel_picker_handle_mouse_background,
    .keyboard = toplevel_picker_handle_keyboard,
};

ToplevelPicker *
toplevel_picker_new(ToplevelPickerFinishCallback finish_callback) {
    ToplevelPicker *result = calloc(1, sizeof(ToplevelPicker));
    result->surface = overlay_surface_new(
        NULL,
        IMAGE_FORMAT_ARGB8888,
        (OverlaySurfaceHandlers){
            .draw = NULL,
            .manual_render = toplevel_picker_render,
            .close = toplevel_picker_handle_surface_close,
            .scale = NULL,
        },
        result
    );

    result->background_buf = shared_buffer_new(
        1,
        1,
        image_format_default_stride(IMAGE_FORMAT_ARGB8888, 1),
        image_format_to_wl(IMAGE_FORMAT_ARGB8888)
    );
    result->background_buf->data[0] = 0x10;
    result->background_buf->data[1] = 0x10;
    result->background_buf->data[2] = 0x10;
    result->background_buf->data[3] = 0x66;

    result->finish_callback = finish_callback;
    wl_list_init(&result->entries);

    seat_dispatcher_add_listener(
        wayland_globals.seat_dispatcher,
        result->surface,
        &toplevel_picker_seat_listener,
        result
    );
    seat_dispatcher_set_cursor_for_surface(
        wayland_globals.seat_dispatcher,
        result->surface,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT
    );

    return result;
}

void toplevel_picker_add(
    ToplevelPicker *picker, const char *title, Image *image
) {
    // TODO
}

void toplevel_picker_present(ToplevelPicker *picker) {
    // TODO
}

void toplevel_picker_destroy(ToplevelPicker *picker) {
    log_debug("destroying toplevel picker %p\n", (void *)picker);

    seat_dispatcher_remove_listener(
        wayland_globals.seat_dispatcher, picker->surface
    );

    shared_buffer_destroy(picker->background_buf);
    // TODO: destroy the entries

    overlay_surface_destroy(picker->surface);
    free(picker);
}
