#pragma once
#include "link-buffer.h"

/**
 * Copy some data to the clipboard.
 * @param mime The MIME type of the data (should be static).
 * @param buf The data to copy (ownership is transferred to the copy process).
 * @param path The path of the data saved to the file (may be NULL).
 * @param active Set to true while the copy is still active, and false.
 * afterwards.
 * @param async Whether to spawn a subprocess for the copy server.
 */
void clipboard_copy(
    const char *mime,
    LinkBuffer *buf,
    const char *path,
    bool *active,
    bool async
);
