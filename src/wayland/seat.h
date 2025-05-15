#pragma once
#include "wayland/overlay-surface.h"
#include <cursor-shape-client.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

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

typedef enum { KEYBOARD_EVENT_PRESS, KEYBOARD_EVENT_RELEASE } KeyboardEventType;

typedef struct {
    struct wl_surface *focus;
    KeyboardEventType type;
    xkb_keysym_t keysym;
} KeyboardEvent;

/**
 * A set of callbacks to receive seat events.
 */
typedef struct {
    void (*mouse)(void *data, MouseEvent event);
    void (*keyboard)(void *data, KeyboardEvent event);
} SeatListener;

typedef struct {
    struct wl_seat *seat;
    struct wl_pointer *pointer;
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
        uint32_t last_enter_serial;
    } pointer_data;

    struct wl_keyboard *keyboard;
    struct {
        struct xkb_context *context;
        struct xkb_keymap *keymap;
        struct xkb_state *state;
        struct wl_surface *focus;
    } keyboard_data;

    struct wl_data_device *data_device;
    // This is the last input event's serial.
    // Used to set the clipboard selection.
    uint32_t last_clipboard_serial;

    // This is an array because it needs to be safe against arbitrary removals
    // (it does mean O(n) insertions though)
    // of (private) struct SeatListenerListEntry
    struct wl_array listeners;
} SeatDispatcher;

/**
 * Create a SeatDispatcher for the specified seat and data device manager.
 * The data device manager may be NULL if it hasn't been received yet;
 * if so, then `seat_dispatcher_attach_data_device` should be called later.
 */
SeatDispatcher *seat_dispatcher_new(
    struct wl_seat *seat, struct wl_data_device_manager *data_device_manager
);

/**
 * Attach a data device to this SeatDispatcher, which allows clipboard
 * operations. This function will be called automatically if a
 * data_device_manager is passed into seat_dispatcher_new.
 */
void seat_dispatcher_attach_data_device(
    SeatDispatcher *dispatcher,
    struct wl_data_device_manager *data_device_manager
);

/**
 * Copy something to the clipboard.
 * This is here because it requires an event serial, which is tracked by the
 * seat.
 */
void seat_dispatcher_set_selection(
    SeatDispatcher *dispatcher, struct wl_data_source *data_source
);

/** Set the cursor shape for a specific OverlaySurface. */
void seat_dispatcher_set_cursor_for_surface(
    SeatDispatcher *dispatcher,
    OverlaySurface *surface,
    enum wp_cursor_shape_device_v1_shape shape
);

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
