#pragma once

#include <build-config.h>
#include <threads.h>

#ifdef SPACESHOT_NOTIFICATIONS

/**
 * Responding to notification actions requires an event loop,
 * which is easier to put in its own thread.
 * This function takes ownership of @p filepath.
 */
bool notify_for_file(thrd_t *thread, char *filepath);

#endif
