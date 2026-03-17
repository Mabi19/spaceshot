#pragma once
#include <ext-foreign-toplevel-list-client.h>

typedef struct {
    struct ext_foreign_toplevel_handle_v1 *handle;
    char *title;
    char *app_id;
    char *identifier;
    struct wl_list link;
} WrappedToplevel;
