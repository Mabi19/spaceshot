#include "clipboard.h"
#include "link-buffer.h"
#include "log.h"
#include "wayland/globals.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

static void clipboard_handle_target(
    void * /* data */,
    struct wl_data_source * /* data_source */,
    const char * /* mime_type */
) {
    // This space intentionally left blank
}

static void clipboard_handle_send(
    void *data,
    struct wl_data_source * /* wl_data_source */,
    const char *mime_type,
    int fd
) {
    ClipboardCopy *copy_source = data;
    ClipboardCopyOffer *offer;
    wl_list_for_each(offer, &copy_source->offers, link) {
        if (strcmp(mime_type, offer->mime) == 0) {
            FILE *wrapped_fd = fdopen(fd, "w");
            if (!wrapped_fd) {
                perror("fdopen");
                close(fd);
                report_error_fatal("couldn't open clipboard fd %d", fd);
            }
            if (offer->buffer) {
                link_buffer_write(offer->buffer, wrapped_fd);
            } else if (offer->data) {
                fwrite(offer->data, offer->length, 1, wrapped_fd);
            } else {
                report_error_fatal(
                    "tried to paste mime type %s, but no data was present\n",
                    offer->mime
                );
            }
            fclose(wrapped_fd);
            return;
        }
    }
    report_warning("no offer for mime type %s\n", mime_type);
}

static void
clipboard_handle_cancelled(void *data, struct wl_data_source *data_source) {
    ClipboardCopy *source = data;
    wl_data_source_destroy(data_source);
    source->finished(source);
}

// Most of these can be NULL because they're for DND data sources,
// and this is a clipboard source.
static struct wl_data_source_listener clipboard_source_listener = {
    .target = clipboard_handle_target,
    .send = clipboard_handle_send,
    .cancelled = clipboard_handle_cancelled,
    .dnd_drop_performed = NULL,
    .dnd_finished = NULL,
    .action = NULL
};

static struct wl_data_device *get_data_device() {
    if (!wayland_globals.data_device) {
        wayland_globals.data_device = wl_data_device_manager_get_data_device(
            wayland_globals.data_device_manager,
            wayland_globals.seat_dispatcher->seat
        );
    }
    return wayland_globals.data_device;
}

void clipboard_core_setup_impl(ClipboardCopy *source) {
    source->core_data_source = wl_data_device_manager_create_data_source(
        wayland_globals.data_device_manager
    );
}

void clipboard_core_activate_impl(ClipboardCopy *source) {
    struct wl_data_device *data_device = get_data_device();
    wl_data_device_set_selection(
        data_device,
        source->core_data_source,
        wayland_globals.seat_dispatcher->last_clipboard_serial
    );
}

void clipboard_core_run_impl(ClipboardCopy *source) {
    wl_data_source_add_listener(
        source->core_data_source, &clipboard_source_listener, source
    );
}

void clipboard_core_offer_impl(
    ClipboardCopy *source, ClipboardCopyOffer *offer
) {
    wl_data_source_offer(source->core_data_source, offer->mime);
}
