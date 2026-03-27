#include "clipboard.h"
#include "link-buffer.h"
#include "log.h"
#include "wayland/globals.h"
#include <wayland-util.h>

extern void clipboard_core_setup_impl(ClipboardCopy *source);
extern void
clipboard_core_offer_impl(ClipboardCopy *source, ClipboardCopyOffer *offer);
extern void clipboard_core_activate_impl(ClipboardCopy *source);
extern void clipboard_core_run_impl(ClipboardCopy *source);

extern void clipboard_ext_setup_impl(ClipboardCopy *source);
extern void
clipboard_ext_offer_impl(ClipboardCopy *source, ClipboardCopyOffer *offer);
extern void clipboard_ext_activate_impl(ClipboardCopy *source);
extern void clipboard_ext_run_impl(ClipboardCopy *source);

ClipboardCopy *clipboard_copy_setup(bool has_surface_serial) {
    ClipboardCopy *copy_source = calloc(1, sizeof(ClipboardCopy));
    wl_list_init(&copy_source->offers);

    if (has_surface_serial &&
        wayland_globals.seat_dispatcher->last_clipboard_serial) {
        copy_source->type = CLIPBOARD_COPY_SOURCE_CORE;
        clipboard_core_setup_impl(copy_source);
        return copy_source;
    } else if (wayland_globals.ext_data_control_manager) {
        copy_source->type = CLIPBOARD_COPY_SOURCE_EXT;
        clipboard_ext_setup_impl(copy_source);
        return copy_source;
    }
    free(copy_source);
    return NULL;
}

ClipboardCopyOffer *
clipboard_copy_offer_mime(ClipboardCopy *source, const char *mime) {
    ClipboardCopyOffer *offer = calloc(1, sizeof(ClipboardCopyOffer));
    offer->mime = mime;
    if (source->type == CLIPBOARD_COPY_SOURCE_CORE) {
        clipboard_core_offer_impl(source, offer);
    } else if (source->type == CLIPBOARD_COPY_SOURCE_EXT) {
        clipboard_ext_offer_impl(source, offer);
    } else {
        REPORT_UNHANDLED("clipboard copy source type", "%d", source->type);
    }
    wl_list_insert(&source->offers, &offer->link);
    return offer;
}

void clipboard_copy_activate(ClipboardCopy *source) {
    if (source->type == CLIPBOARD_COPY_SOURCE_CORE) {
        clipboard_core_activate_impl(source);
    } else if (source->type == CLIPBOARD_COPY_SOURCE_EXT) {
        clipboard_ext_activate_impl(source);
    } else {
        REPORT_UNHANDLED("clipboard copy source type", "%d", source->type);
    }
}

void clipboard_copy_run(ClipboardCopy *source) {
    if (source->type == CLIPBOARD_COPY_SOURCE_CORE) {
        clipboard_core_run_impl(source);
    } else if (source->type == CLIPBOARD_COPY_SOURCE_EXT) {
        clipboard_ext_run_impl(source);
    } else {
        REPORT_UNHANDLED("clipboard copy source type", "%d", source->type);
    }
}

void clipboard_copy_destroy(ClipboardCopy *source) {
    ClipboardCopyOffer *offer, *tmp;
    wl_list_for_each_safe(offer, tmp, &source->offers, link) {
        if (offer->buffer) {
            link_buffer_destroy(offer->buffer);
        }
        if (offer->data) {
            free(offer->data);
        }
        wl_list_remove(&offer->link);
        free(offer);
    }
    free(source);
}
