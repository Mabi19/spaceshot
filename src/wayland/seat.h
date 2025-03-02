#pragma once
#include "wayland/overlay-surface.h"
#include <cursor-shape-client.h>
#include <wayland-client.h>

typedef enum {
    POINTER_BUTTON_LEFT = 1,
    POINTER_BUTTON_RIGHT = 2,
    POINTER_BUTTON_MIDDLE = 4,
} PointerButtons;

typedef struct {
    struct wl_surface *focus;
    double surface_x;
    double surface_y;
    PointerButtons buttons_pressed;
    PointerButtons buttons_held;
    PointerButtons buttons_released;
} MouseEvent;

/**
 * A set of callbacks to receive seat events.
 */
typedef struct {
    void (*mouse)(void *data, MouseEvent event);
} SeatListener;

typedef struct {
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_touch *touch;
    struct {
        struct wp_cursor_shape_device_v1 *shape_device;
        struct wl_surface *focus;
        double surface_x;
        double surface_y;
        PointerButtons pressed_buttons;
        /** Used internally to decide when pressed/released events should be
         * sent. */
        PointerButtons pending_buttons;
        /** Used internally to decide what events should be sent. */
        enum {
            POINTER_EVENT_MOTION = 1,
        } received_events;
    } pointer_data;
    // This is an array because it needs to be safe against arbitrary removals
    // (it does mean O(n) insertions though)
    // of (private) struct SeatListenerListEntry
    struct wl_array listeners;
} SeatDispatcher;

SeatDispatcher *seat_dispatcher_new(struct wl_seat *seat);
/**
 * Add a listener for the specified OverlaySurface.
 * Note that events will not be filtered depending on pointer/keyboard focus;
 * this is so that selections can work nicer when moving the mouse outside the
 * output.
 */
void seat_dispatcher_add_listener(
    SeatDispatcher *dispatcher,
    OverlaySurface *surface,
    SeatListener *listener,
    void *user_data
);
void seat_dispatcher_remove_listener(
    SeatDispatcher *dispatcher, OverlaySurface *surface
);
