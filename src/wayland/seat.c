#include "wayland/seat.h"
#include "cursor-shape-client.h"
#include "log.h"
#include "wayland/globals.h"
#include <assert.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

typedef struct {
    // This will be NULL when empty
    OverlaySurface *surface;
    SeatListener *listener;
    void *user_data;
} SeatListenerListEntry;

// The amount of pixels one wheel scroll tick is worth.
// Compositors can't be trusted to give reasonable values via axis events,
// so convert ticks from axis_value120 with a multiplier.
// TODO: make this configurable once the UI is more settled
constexpr double SCROLL_TICK_PIXELS = 60.0;

static void pointer_handle_enter(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t serial,
    struct wl_surface *surface,
    wl_fixed_t x,
    wl_fixed_t y
) {
    Seat *seat = data;
    seat->pointer_data.focus = surface;
    seat->pointer_data.surface_x = wl_fixed_to_double(x);
    seat->pointer_data.surface_y = wl_fixed_to_double(y);
    seat->pointer_data.received_events |= POINTER_EVENT_MOTION;
    seat->pointer_data.last_enter_serial = serial;

    SeatListenerListEntry *entry;
    wl_array_for_each(entry, &seat->listeners) {
        if (!entry->surface)
            continue;

        if (entry->surface->wl_surface == surface) {
            wp_cursor_shape_device_v1_set_shape(
                seat->pointer_data.shape_device,
                seat->pointer_data.last_enter_serial,
                entry->surface->cursor_shape
            );
        }
    }
}

static void pointer_handle_leave(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t /* serial */,
    struct wl_surface * /* surface */
) {
    Seat *seat = data;
    seat->pointer_data.focus = NULL;
}

static void pointer_handle_motion(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t /* time */,
    wl_fixed_t x,
    wl_fixed_t y
) {
    Seat *seat = data;
    seat->pointer_data.surface_x = wl_fixed_to_double(x);
    seat->pointer_data.surface_y = wl_fixed_to_double(y);
    seat->pointer_data.received_events |= POINTER_EVENT_MOTION;
}

static void pointer_handle_button(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t serial,
    uint32_t /* time */,
    uint32_t button,
    enum wl_pointer_button_state state
) {
    Seat *seat = data;
    seat->last_clipboard_serial = serial;

    PointerButtons button_mask;
    switch (button) {
    case BTN_LEFT:
        button_mask = POINTER_BUTTON_LEFT;
        break;
    case BTN_RIGHT:
        button_mask = POINTER_BUTTON_RIGHT;
        break;
    case BTN_MIDDLE:
        button_mask = POINTER_BUTTON_MIDDLE;
        break;
    default:
        button_mask = 0;
        break;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        seat->pointer_data.pending_buttons |= button_mask;
    } else {
        seat->pointer_data.pending_buttons &= ~button_mask;
    }
}

static void pointer_handle_axis(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t /* time */,
    enum wl_pointer_axis axis,
    wl_fixed_t value
) {
    Seat *seat = data;
    double amount = wl_fixed_to_double(value);
    switch (axis) {
    case WL_POINTER_AXIS_VERTICAL_SCROLL:
        seat->pointer_data.pending_scroll_y += amount;
        break;
    case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
        seat->pointer_data.pending_scroll_x += amount;
        break;
    default:
        break;
    }
}

static void
pointer_handle_frame(void *data, struct wl_pointer * /* pointer */) {
    Seat *seat = data;
    auto ptr_data = &seat->pointer_data;

    double scroll_x = ptr_data->pending_scroll_x;
    double scroll_y = ptr_data->pending_scroll_y;
    // Wheel scrolling gets much better granularity from the value120 events,
    // so prefer them over the (tiny) axis values when available.
    if (ptr_data->has_axis_source &&
        ptr_data->axis_source == WL_POINTER_AXIS_SOURCE_WHEEL) {
        if (ptr_data->received_value120_x) {
            scroll_x =
                ptr_data->pending_value120_x / 120.0 * SCROLL_TICK_PIXELS;
        }
        if (ptr_data->received_value120_y) {
            scroll_y =
                ptr_data->pending_value120_y / 120.0 * SCROLL_TICK_PIXELS;
        }
    }

    MouseEvent event = {
        .focus = ptr_data->focus,
        .surface_x = ptr_data->surface_x,
        .surface_y = ptr_data->surface_y,
        .scroll_x = scroll_x,
        .scroll_y = scroll_y,
        .buttons_pressed =
            ptr_data->pending_buttons & ~ptr_data->pressed_buttons,
        .buttons_held = ptr_data->pending_buttons,
        .buttons_released =
            ptr_data->pressed_buttons & ~ptr_data->pending_buttons,
    };

    // emit events on mouse move, button change and scrolling
    if (ptr_data->received_events & POINTER_EVENT_MOTION ||
        ptr_data->pressed_buttons != ptr_data->pending_buttons ||
        scroll_x != 0 || scroll_y != 0) {

        SeatListenerListEntry *entry;
        wl_array_for_each(entry, &seat->listeners) {
            if (!entry->surface)
                continue;

            if (entry->listener->mouse) {
                entry->listener->mouse(entry->user_data, event);
            }
        }
    }
    ptr_data->pressed_buttons = ptr_data->pending_buttons;
    ptr_data->pending_scroll_x = 0;
    ptr_data->pending_scroll_y = 0;
    ptr_data->pending_value120_x = 0;
    ptr_data->pending_value120_y = 0;
    ptr_data->received_value120_x = false;
    ptr_data->received_value120_y = false;
    ptr_data->received_events = 0;
}

static void pointer_handle_axis_source(
    void *data,
    struct wl_pointer * /* pointer */,
    enum wl_pointer_axis_source source
) {
    Seat *seat = data;
    seat->pointer_data.axis_source = source;
    seat->pointer_data.has_axis_source = true;
}

static void pointer_handle_axis_stop(
    void * /* data */,
    struct wl_pointer * /* pointer */,
    uint32_t /* time */,
    enum wl_pointer_axis /* axis */
) {
    // This space intentionally left blank
}

static void pointer_handle_axis_value120(
    void *data,
    struct wl_pointer * /* pointer */,
    enum wl_pointer_axis axis,
    int32_t value120
) {
    Seat *seat = data;
    switch (axis) {
    case WL_POINTER_AXIS_VERTICAL_SCROLL:
        seat->pointer_data.pending_value120_y += value120;
        seat->pointer_data.received_value120_y = true;
        break;
    case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
        seat->pointer_data.pending_value120_x += value120;
        seat->pointer_data.received_value120_x = true;
        break;
    default:
        break;
    }
}

static void pointer_handle_axis_relative_direction(
    void * /* data */,
    struct wl_pointer * /* pointer */,
    enum wl_pointer_axis /* axis */,
    enum wl_pointer_axis_relative_direction /* direction */
) {
    // This space intentionally left blank
}

static struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = NULL, // deprecated
    .axis_value120 = pointer_handle_axis_value120,
    .axis_relative_direction = pointer_handle_axis_relative_direction,
};

static void keyboard_handle_keymap(
    void *data,
    struct wl_keyboard * /* keyboard */,
    enum wl_keyboard_keymap_format format,
    int fd,
    uint32_t size
) {
    Seat *seat = data;
    if (seat->keyboard_data.keymap) {
        xkb_keymap_unref(seat->keyboard_data.keymap);
        seat->keyboard_data.keymap = NULL;
    }
    if (seat->keyboard_data.state) {
        xkb_state_unref(seat->keyboard_data.state);
        seat->keyboard_data.state = NULL;
    }

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        report_error_fatal("unrecognized keyboard format %d", format);
    }

    char *map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_shm == MAP_FAILED) {
        report_error_fatal("couldn't mmap keymap");
    }
    seat->keyboard_data.keymap = xkb_keymap_new_from_string(
        seat->keyboard_data.context,
        map_shm,
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );
    munmap(map_shm, size);
    close(fd);

    seat->keyboard_data.state = xkb_state_new(seat->keyboard_data.keymap);
}

static void keyboard_handle_enter(
    void *data,
    struct wl_keyboard * /* keyboard */,
    uint32_t /* serial */,
    struct wl_surface *surface,
    struct wl_array * /* keys */
) {
    Seat *seat = data;
    seat->keyboard_data.focus = surface;
    // TODO: handle initial keys
    // fire a different event with initial key presses?
}

static void keyboard_handle_leave(
    void *data,
    struct wl_keyboard * /* keyboard */,
    uint32_t /* serial */,
    struct wl_surface * /* surface */
) {
    Seat *seat = data;
    seat->keyboard_data.focus = NULL;
}

static void keyboard_handle_key(
    void *data,
    struct wl_keyboard * /* keyboard */,
    uint32_t serial,
    uint32_t /* time */,
    uint32_t keycode,
    enum wl_keyboard_key_state key_state
) {
    Seat *seat = data;
    seat->last_clipboard_serial = serial;

    KeyboardEventType type;
    switch (key_state) {
    case WL_KEYBOARD_KEY_STATE_PRESSED:
        type = KEYBOARD_EVENT_PRESS;
        break;
    case WL_KEYBOARD_KEY_STATE_RELEASED:
        type = KEYBOARD_EVENT_RELEASE;
        break;
    default:
        // As of writing, there's a new state (REPEATED) merged into wayland but
        // not emitted by compositors yet. So explicitly do nothing here;
        // TODO: when we need key repeat, use the REPEATED key state
        return;
    }

    KeyboardEvent event = {
        .focus = seat->keyboard_data.focus,
        .keysym =
            xkb_state_key_get_one_sym(seat->keyboard_data.state, keycode + 8),
        .type = type,
        .state = seat->keyboard_data.state,
    };

    SeatListenerListEntry *entry;
    wl_array_for_each(entry, &seat->listeners) {
        if (!entry->surface)
            continue;

        if (entry->listener->keyboard) {
            entry->listener->keyboard(entry->user_data, event);
        }
    }
}

static void keyboard_handle_modifiers(
    void *data,
    struct wl_keyboard * /* keyboard */,
    uint32_t /* serial */,
    uint32_t depressed,
    uint32_t latched,
    uint32_t locked,
    uint32_t group
) {
    Seat *seat = data;
    xkb_state_update_mask(
        seat->keyboard_data.state, depressed, latched, locked, 0, 0, group
    );
}

static void keyboard_handle_repeat_info(
    void * /* data */,
    struct wl_keyboard * /* keyboard */,
    int32_t /* rate */,
    int32_t /* delay */
) {
    // There is no typing needed, so an implementation here isn't necessary.
}

static struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

static void seat_handle_capabilities(
    void *data,
    struct wl_seat * /* seat */,
    enum wl_seat_capability capabilities
) {
    Seat *seat = data;
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!seat->pointer) {
            seat->pointer = wl_seat_get_pointer(seat->seat);
            seat->pointer_data.shape_device =
                wp_cursor_shape_manager_v1_get_pointer(
                    wayland_globals.cursor_shape_manager, seat->pointer
                );
            wl_pointer_add_listener(seat->pointer, &pointer_listener, seat);
        }
    } else {
        if (seat->pointer) {
            wp_cursor_shape_device_v1_destroy(seat->pointer_data.shape_device);
            wl_pointer_release(seat->pointer);
            seat->pointer = NULL;
        }
    }

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (!seat->keyboard) {
            seat->keyboard = wl_seat_get_keyboard(seat->seat);
            wl_keyboard_add_listener(seat->keyboard, &keyboard_listener, seat);
        }
    } else {
        if (seat->keyboard) {
            wl_keyboard_release(seat->keyboard);
            seat->keyboard = NULL;
        }
    }

    // TODO: other types of input (touch?)
}

static void seat_handle_name(void *, struct wl_seat *, const char *) {
    // this space intentionally left blank
}

static struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

Seat *seat_new(struct wl_seat *seat) {
    Seat *result = calloc(1, sizeof(Seat));
    result->seat = seat;

    wl_seat_add_listener(seat, &seat_listener, result);
    wl_array_init(&result->listeners);

    result->keyboard_data.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    assert(result->keyboard_data.context);

    return result;
}

void seat_try_send_cursor_for_surface(
    Seat *seat,
    OverlaySurface *surface,
    enum wp_cursor_shape_device_v1_shape shape
) {
    if (seat->pointer_data.focus == surface->wl_surface) {
        wp_cursor_shape_device_v1_set_shape(
            seat->pointer_data.shape_device,
            seat->pointer_data.last_enter_serial,
            shape
        );
    }
    // If not focused right now, cursor shape will be set later when that
    // happens in the enter handler.
}

void seat_add_listener(
    Seat *seat, OverlaySurface *surface, SeatListener *listener, void *user_data
) {
    SeatListenerListEntry *entry;
    // try to find an empty slot first
    wl_array_for_each(entry, &seat->listeners) {
        if (entry->surface == NULL) {
            entry->surface = surface;
            entry->listener = listener;
            entry->user_data = user_data;
            return;
        }
    }
    // push to the end
    entry = wl_array_add(&seat->listeners, sizeof(SeatListenerListEntry));
    entry->surface = surface;
    entry->listener = listener;
    entry->user_data = user_data;
}

void seat_remove_listener(Seat *seat, OverlaySurface *surface) {
    SeatListenerListEntry *entry;
    wl_array_for_each(entry, &seat->listeners) {
        if (entry->surface == surface) {
            // clear everything for safety
            entry->surface = NULL;
            entry->listener = NULL;
            entry->user_data = NULL;
        }
    }
}

void seat_destroy(Seat *seat) {
    wl_seat_release(seat->seat);
    if (seat->pointer) {
        wl_pointer_release(seat->pointer);
        wp_cursor_shape_device_v1_destroy(seat->pointer_data.shape_device);
    }
    if (seat->keyboard) {
        wl_keyboard_release(seat->keyboard);
    }

    xkb_context_unref(seat->keyboard_data.context);
    xkb_keymap_unref(seat->keyboard_data.keymap);
    xkb_state_unref(seat->keyboard_data.state);

    wl_array_release(&seat->listeners);

    free(seat);
}
