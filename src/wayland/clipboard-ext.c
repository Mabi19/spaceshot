#include "clipboard.h"
#include "ext-data-control-client.h"
#include "link-buffer.h"
#include "log.h"
#include "wayland/globals.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

static void clipboard_handle_send(
    void *data,
    struct ext_data_control_source_v1 * /* ext_data_source */,
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

static void clipboard_handle_cancelled(
    void *data, struct ext_data_control_source_v1 *data_source
) {
    ClipboardCopy *source = data;
    ext_data_control_source_v1_destroy(data_source);
    source->finished(source);
}

static struct ext_data_control_source_v1_listener clipboard_source_listener = {
    .send = clipboard_handle_send,
    .cancelled = clipboard_handle_cancelled,
};

static struct ext_data_control_device_v1 *get_data_device() {
    if (!wayland_globals.ext_data_control_device) {
        wayland_globals.ext_data_control_device =
            ext_data_control_manager_v1_get_data_device(
                wayland_globals.ext_data_control_manager,
                wayland_globals.seat_dispatcher->seat
            );
    }
    return wayland_globals.ext_data_control_device;
}

void clipboard_ext_setup_impl(ClipboardCopy *source) {
    struct ext_data_control_device_v1 *data_device = get_data_device();
    source->ext_data_source = ext_data_control_manager_v1_create_data_source(
        wayland_globals.ext_data_control_manager
    );

    ext_data_control_device_v1_set_selection(
        data_device, source->ext_data_source
    );
}

void clipboard_ext_run_impl(ClipboardCopy *source) {
    ext_data_control_source_v1_add_listener(
        source->ext_data_source, &clipboard_source_listener, source
    );
}

void clipboard_ext_offer_impl(
    ClipboardCopy *source, ClipboardCopyOffer *offer
) {
    ext_data_control_source_v1_offer(source->ext_data_source, offer->mime);
}
