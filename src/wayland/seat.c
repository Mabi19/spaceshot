#include "wayland/seat.h"
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <wayland-util.h>

typedef struct {
    // This will be NULL when empty
    OverlaySurface *surface;
    SeatListener *listener;
    void *user_data;
} SeatListenerListEntry;

static void pointer_handle_enter(
    void *data,
    struct wl_pointer * /* pointer */,
    uint32_t /* serial */,
    struct wl_surface *surface,
    wl_fixed_t x,
    wl_fixed_t y
) {
    SeatDispatcher *dispatcher = data;
    dispatcher->pointer_data.focus = surface;
    dispatcher->pointer_data.surface_x = wl_fixed_to_double(x);
    dispatcher->pointer_data.surface_y = wl_fixed_to_double(y);
    dispatcher->pointer_data.received_events |= POINTER_EVENT_MOTION;
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
    uint32_t /* serial */,
    uint32_t /* time */,
    uint32_t button,
    enum wl_pointer_button_state state
) {
    SeatDispatcher *dispatcher = data;
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

static void seat_handle_capabilities(
    void *data,
    struct wl_seat * /* seat */,
    enum wl_seat_capability capabilities
) {
    SeatDispatcher *dispatcher = data;
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!dispatcher->pointer) {
            dispatcher->pointer = wl_seat_get_pointer(dispatcher->seat);
            wl_pointer_add_listener(
                dispatcher->pointer, &pointer_listener, dispatcher
            );
        }
    } else {
        if (dispatcher->pointer) {
            wl_pointer_destroy(dispatcher->pointer);
        }
    }

    // TODO: other types of input
}

static void seat_handle_name(void *, struct wl_seat *, const char *) {
    // this space intentionally left blank
}

static struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

SeatDispatcher *seat_dispatcher_new(struct wl_seat *seat) {
    SeatDispatcher *result = calloc(1, sizeof(SeatDispatcher));
    result->seat = seat;
    wl_seat_add_listener(seat, &seat_listener, result);
    wl_array_init(&result->listeners);
    return result;
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
