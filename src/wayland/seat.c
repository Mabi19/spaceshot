#include "wayland/seat.h"
#include "cursor-shape-client.h"
#include "log.h"
#include "wayland/globals.h"
#include <assert.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
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

/** Assumes the passed-in wl_surface has pointer focus. */
static void
send_cursor_shape(SeatDispatcher *dispatcher, struct wl_surface *wl_surface) {
    SeatListenerListEntry *entry;
    wl_array_for_each(entry, &dispatcher->listeners) {
        if (!entry->surface)
            continue;

        if (entry->surface->wl_surface == wl_surface) {
            wp_cursor_shape_device_v1_set_shape(
                dispatcher->pointer_data.shape_device,
                dispatcher->pointer_data.last_enter_serial,
                entry->surface->cursor_shape
            );
        }
    }
}

static void pointer_handle_enter(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t serial,
    struct wl_surface *surface,
    wl_fixed_t x,
    wl_fixed_t y
) {
    SeatDispatcher *dispatcher = data;
    dispatcher->pointer_data.focus = surface;
    dispatcher->pointer_data.surface_x = wl_fixed_to_double(x);
    dispatcher->pointer_data.surface_y = wl_fixed_to_double(y);
    dispatcher->pointer_data.received_events |= POINTER_EVENT_MOTION;
    dispatcher->pointer_data.last_enter_serial = serial;

    send_cursor_shape(dispatcher, surface);
}

static void pointer_handle_leave(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t /* serial */,
    struct wl_surface * /* surface */
) {
    SeatDispatcher *dispatcher = data;
    dispatcher->pointer_data.focus = NULL;
}

static void pointer_handle_motion(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t /* time */,
    wl_fixed_t x,
    wl_fixed_t y
) {
    SeatDispatcher *dispatcher = data;
    dispatcher->pointer_data.surface_x = wl_fixed_to_double(x);
    dispatcher->pointer_data.surface_y = wl_fixed_to_double(y);
    dispatcher->pointer_data.received_events |= POINTER_EVENT_MOTION;
}

static void pointer_handle_button(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t serial,
    uint32_t /* time */,
    uint32_t button,
    enum wl_pointer_button_state state
) {
    SeatDispatcher *dispatcher = data;
    dispatcher->last_clipboard_serial = serial;

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
        dispatcher->pointer_data.pending_buttons |= button_mask;
    } else {
        dispatcher->pointer_data.pending_buttons &= ~button_mask;
    }
}

static void pointer_handle_axis(
    void * /* data */,
    struct wl_pointer * /* pointer */,
    uint32_t /* time */,
    enum wl_pointer_axis /* axis */,
    wl_fixed_t /* value */
) {
    // This space intentionally left blank
    // (but may not be later)
}

static void
pointer_handle_frame(void *data, struct wl_pointer * /* pointer */) {
    SeatDispatcher *dispatcher = data;
    auto ptr_data = &dispatcher->pointer_data;

    MouseEvent event = {
        .focus = ptr_data->focus,
        .surface_x = ptr_data->surface_x,
        .surface_y = ptr_data->surface_y,
        .buttons_pressed =
            ptr_data->pending_buttons & ~ptr_data->pressed_buttons,
        .buttons_held = ptr_data->pending_buttons,
        .buttons_released =
            ptr_data->pressed_buttons & ~ptr_data->pending_buttons,
    };

    // emit events both on mouse move and button change
    if (ptr_data->received_events & POINTER_EVENT_MOTION ||
        ptr_data->pressed_buttons != ptr_data->pending_buttons) {

        SeatListenerListEntry *entry;
        wl_array_for_each(entry, &dispatcher->listeners) {
            if (!entry->surface)
                continue;

            if (entry->listener->mouse) {
                entry->listener->mouse(entry->user_data, event);
            }
        }
    }
    ptr_data->pressed_buttons = ptr_data->pending_buttons;
    ptr_data->received_events = 0;
}

static void pointer_handle_axis_source(
    void * /* data */,
    struct wl_pointer * /* pointer */,
    enum wl_pointer_axis_source /* axis_source */
) {
    // This space intentionally left blank
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
    void * /* data */,
    struct wl_pointer * /* pointer */,
    enum wl_pointer_axis /* axis */,
    int32_t /* value120 */
) {
    // This space intentionally left blank
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
    SeatDispatcher *dispatcher = data;
    if (dispatcher->keyboard_data.keymap) {
        xkb_keymap_unref(dispatcher->keyboard_data.keymap);
        dispatcher->keyboard_data.keymap = NULL;
    }
    if (dispatcher->keyboard_data.state) {
        xkb_state_unref(dispatcher->keyboard_data.state);
        dispatcher->keyboard_data.state = NULL;
    }

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        report_error_fatal("unrecognized keyboard format %d", format);
    }

    char *map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_shm == MAP_FAILED) {
        report_error_fatal("couldn't mmap keymap");
    }
    dispatcher->keyboard_data.keymap = xkb_keymap_new_from_string(
        dispatcher->keyboard_data.context,
        map_shm,
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );
    munmap(map_shm, size);
    close(fd);

    dispatcher->keyboard_data.state =
        xkb_state_new(dispatcher->keyboard_data.keymap);
}

static void keyboard_handle_enter(
    void *data,
    struct wl_keyboard * /* keyboard */,
    uint32_t /* serial */,
    struct wl_surface *surface,
    struct wl_array * /* keys */
) {
    SeatDispatcher *dispatcher = data;
    dispatcher->keyboard_data.focus = surface;
    // TODO: handle initial keys
    // fire a different event with initial key presses?
}

static void keyboard_handle_leave(
    void *data,
    struct wl_keyboard * /* keyboard */,
    uint32_t /* serial */,
    struct wl_surface * /* surface */
) {
    SeatDispatcher *dispatcher = data;
    dispatcher->keyboard_data.focus = NULL;
}

static void keyboard_handle_key(
    void *data,
    struct wl_keyboard * /* keyboard */,
    uint32_t serial,
    uint32_t /* time */,
    uint32_t keycode,
    enum wl_keyboard_key_state key_state
) {
    SeatDispatcher *dispatcher = data;
    dispatcher->last_clipboard_serial = serial;

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
        // not in a tagged version. So explicitly do nothing here.
        return;
    }

    KeyboardEvent event = {
        .focus = dispatcher->keyboard_data.focus,
        .keysym = xkb_state_key_get_one_sym(
            dispatcher->keyboard_data.state, keycode + 8
        ),
        .type = type
    };

    SeatListenerListEntry *entry;
    wl_array_for_each(entry, &dispatcher->listeners) {
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
    SeatDispatcher *dispatcher = data;
    xkb_state_update_mask(
        dispatcher->keyboard_data.state, depressed, latched, locked, 0, 0, group
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
    SeatDispatcher *dispatcher = data;
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!dispatcher->pointer) {
            dispatcher->pointer = wl_seat_get_pointer(dispatcher->seat);
            dispatcher->pointer_data.shape_device =
                wp_cursor_shape_manager_v1_get_pointer(
                    wayland_globals.cursor_shape_manager, dispatcher->pointer
                );
            wl_pointer_add_listener(
                dispatcher->pointer, &pointer_listener, dispatcher
            );
        }
    } else {
        if (dispatcher->pointer) {
            wp_cursor_shape_device_v1_destroy(
                dispatcher->pointer_data.shape_device
            );
            wl_pointer_release(dispatcher->pointer);
            dispatcher->pointer = NULL;
        }
    }

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (!dispatcher->keyboard) {
            dispatcher->keyboard = wl_seat_get_keyboard(dispatcher->seat);
            wl_keyboard_add_listener(
                dispatcher->keyboard, &keyboard_listener, dispatcher
            );
        }
    } else {
        if (dispatcher->keyboard) {
            wl_keyboard_release(dispatcher->keyboard);
            dispatcher->keyboard = NULL;
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

SeatDispatcher *seat_dispatcher_new(
    struct wl_seat *seat, struct wl_data_device_manager *data_device_manager
) {
    SeatDispatcher *result = calloc(1, sizeof(SeatDispatcher));
    result->seat = seat;

    if (data_device_manager) {
        seat_dispatcher_attach_data_device(result, data_device_manager);
    }

    wl_seat_add_listener(seat, &seat_listener, result);
    wl_array_init(&result->listeners);

    result->keyboard_data.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    assert(result->keyboard_data.context);

    return result;
}

void seat_dispatcher_attach_data_device(
    SeatDispatcher *dispatcher,
    struct wl_data_device_manager *data_device_manager
) {
    if (dispatcher->data_device) {
        report_warning(
            "Tried to attach data device to seat dispatcher twice, skipping"
        );
        return;
    }

    dispatcher->data_device = wl_data_device_manager_get_data_device(
        data_device_manager, dispatcher->seat
    );
}

void seat_dispatcher_set_selection(
    SeatDispatcher *dispatcher, struct wl_data_source *data_source
) {
    wl_data_device_set_selection(
        dispatcher->data_device, data_source, dispatcher->last_clipboard_serial
    );
}

void seat_dispatcher_set_cursor_for_surface(
    SeatDispatcher *dispatcher,
    OverlaySurface *surface,
    enum wp_cursor_shape_device_v1_shape shape
) {
    surface->cursor_shape = shape;
    if (dispatcher->pointer_data.focus == surface->wl_surface) {
        send_cursor_shape(dispatcher, surface->wl_surface);
    }
    // If not focused right now, cursor shape will be set later when that
    // happens.
}

void seat_dispatcher_add_listener(
    SeatDispatcher *dispatcher,
    OverlaySurface *surface,
    SeatListener *listener,
    void *user_data
) {
    SeatListenerListEntry *entry;
    // try to find an empty slot first
    wl_array_for_each(entry, &dispatcher->listeners) {
        if (entry->surface == NULL) {
            entry->surface = surface;
            entry->listener = listener;
            entry->user_data = user_data;
            return;
        }
    }
    // push to the end
    entry = wl_array_add(&dispatcher->listeners, sizeof(SeatListenerListEntry));
    entry->surface = surface;
    entry->listener = listener;
    entry->user_data = user_data;
}

void seat_dispatcher_remove_listener(
    SeatDispatcher *dispatcher, OverlaySurface *surface
) {
    SeatListenerListEntry *entry;
    wl_array_for_each(entry, &dispatcher->listeners) {
        if (entry->surface == surface) {
            // clear everything for safety
            entry->surface = NULL;
            entry->listener = NULL;
            entry->user_data = NULL;
        }
    }
}
