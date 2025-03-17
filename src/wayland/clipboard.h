#pragma once
#include <wayland-client.h>

/** This callback receives the data passed into clipboard_copy. */
typedef void (*ClipboardFinishCallback)(void *copy_data);

/**
 * Copy something to the clipboard. Note that only one data buffer and MIME type
 * can be supplied. The supplied finish_callback is called when the underlying
 * data source is replaced and the program can exit.
 *
 * Note that the supplied data is owned by the user
 */
void clipboard_copy(
    void *data,
    size_t data_len,
    const char *mime_type,
    ClipboardFinishCallback finish_callback
);
