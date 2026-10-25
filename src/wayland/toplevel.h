#pragma once
#include <ext-foreign-toplevel-list-client.h>

typedef enum {
    WRAPPED_TOPLEVEL_CREATE_WAS_CALLED = 1,
} WrappedToplevelFillState;

typedef struct {
    struct ext_foreign_toplevel_handle_v1 *handle;
    char *title;
    char *app_id;
    char *identifier;
    WrappedToplevelFillState fill_state;
    struct wl_list link;
} WrappedToplevel;
