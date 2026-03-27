#pragma once
#include "ext-data-control-client.h"
#include "link-buffer.h"
#include <wayland-client.h>

typedef enum {
    CLIPBOARD_COPY_SOURCE_CORE,
    CLIPBOARD_COPY_SOURCE_EXT,
} ClipboardCopyType;

typedef struct {
    const char *mime;
    // At least one of buffer or (data, length) must be set.
    LinkBuffer *buffer;
    uint8_t *data;
    size_t length;

    struct wl_list link;
} ClipboardCopyOffer;

typedef struct ClipboardCopy {
    ClipboardCopyType type;
    struct wl_data_source *core_data_source;
    struct ext_data_control_source_v1 *ext_data_source;
    /**
     * The function to call when no more copying will occur from this source,
     * usually due to it being replaced with another.
     * If you don't care about doing any cleanup,
     * just set this to clipboard_copy_destroy.
     */
    void (*finished)(struct ClipboardCopy *source);
    struct wl_list offers;
} ClipboardCopy;

/**
 * Create a new clipboard copy source.
 * @param has_surface_serial Whether at least one layer surface is currently
 * active, which allows using the core protocol for copying.
 */
ClipboardCopy *clipboard_copy_setup(bool has_surface_serial);
/**
 * Create a new offer object. The data can be filled in later.
 * Note that the copy offers take ownership of their data and will destroy it
 * upon calling clipboard_copy_destroy.
 */
ClipboardCopyOffer *
clipboard_copy_offer_mime(ClipboardCopy *source, const char *mime);
/**
 * Tell the compositor to set the clipboard.
 * This does not actually start waiting for pastes, see @c clipboard_copy_run
 * for that. Don't call wl_display_dispatch between this and @c
 * clipboard_copy_run, or you can miss pastes.
 */
void clipboard_copy_activate(ClipboardCopy *source);
/**
 * Activate the copy. All offers added by this point need to have at least one
 * data pointer filled out.
 */
void clipboard_copy_run(ClipboardCopy *source);
void clipboard_copy_destroy(ClipboardCopy *source);
