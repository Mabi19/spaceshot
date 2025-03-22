#include "clipboard.h"
#include "link-buffer.h"
#include "log.h"
#include "wayland/globals.h"
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>

typedef struct {
    LinkBuffer *data;
    ClipboardFinishCallback finish_cb;
} ClipboardDataSource;

static void data_source_handle_target(
    void * /* data */,
    struct wl_data_source * /* data_source */,
    const char * /* mime_type */
) {
    // This space intentionally left blank
}

static void data_source_handle_send(
    void *data,
    struct wl_data_source * /* wl_data_source */,
    const char * /* mime_type */,
    int fd
) {
    ClipboardDataSource *source = data;
    FILE *wrapped_fd = fdopen(fd, "w");
    if (!wrapped_fd) {
        perror("fdopen");
        close(fd);
        report_error_fatal("couldn't open clipboard fd %d", fd);
    }
    link_buffer_write(source->data, wrapped_fd);
    fclose(wrapped_fd);
}

static void data_source_handle_cancelled(
    void *data, struct wl_data_source *wl_data_source
) {
    ClipboardDataSource *source = data;
    wl_data_source_destroy(wl_data_source);

    auto finish_cb = source->finish_cb;
    finish_cb(source->data);

    free(source);
}

// Most of these can be NULL because they're for DND data sources,
// and this is a clipboard source.
static struct wl_data_source_listener data_source_listener = {
    .target = data_source_handle_target,
    .send = data_source_handle_send,
    .cancelled = data_source_handle_cancelled,
    .dnd_drop_performed = NULL,
    .dnd_finished = NULL,
    .action = NULL
};

void clipboard_copy_link_buffer(
    LinkBuffer *data, const char *mime_type, ClipboardFinishCallback finish_cb
) {
    struct wl_data_source *data_source =
        wl_data_device_manager_create_data_source(
            wayland_globals.data_device_manager
        );

    ClipboardDataSource *source = malloc(sizeof(ClipboardDataSource));
    source->data = data;
    source->finish_cb = finish_cb;

    wl_data_source_offer(data_source, mime_type);
    wl_data_source_add_listener(data_source, &data_source_listener, source);
    seat_dispatcher_set_selection(wayland_globals.seat_dispatcher, data_source);
}
