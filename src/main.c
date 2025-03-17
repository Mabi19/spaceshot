#include "args.h"
#include "bbox.h"
#include "image.h"
#include "log.h"
#include "paths.h"
#include "region-picker.h"
#include "wayland/clipboard.h"
#include "wayland/globals.h"
#include "wayland/screenshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

typedef struct {
    RegionPicker *picker;
    Image *image;
    struct wl_list link;
} RegionPickerListEntry;

// TODO: Rework how waiting works.
// First, Wayland should be polled until an "active wait" flag is unset,
// then the process should detach as to not block when waiting for others to
// paste, then Wayland should be polled until a "clipboard wait" flag is unset.
// If a second event loop is added later (such as for notifications),
// that can go in its own thread that is later join()ed.
static bool is_finished = false;
static bool correct_output_found = false;
static Arguments *args;
static struct wl_list active_pickers;

static void clipboard_copy_finish(void *) { is_finished = true; }

// Note that this function uses pixels (device coordinates).
static void
crop_and_save_image(Image *image, BBox crop_bounds, bool is_interactive) {
    Image *cropped = image_crop(
        image,
        crop_bounds.x,
        crop_bounds.y,
        crop_bounds.width,
        crop_bounds.height
    );

    char *output_filename = get_output_filename();
    image_save_png(cropped, output_filename);

    // copying to clipboard via the core protocol can only be done when focused
    // TODO: use wl-clipboard or wlr_data_control when non-interactive
    if (is_interactive) {
        clipboard_copy(
            "hello world",
            strlen("hello world"),
            "text/plain",
            clipboard_copy_finish
        );
    } else {
        // no need to wait for copy
        is_finished = true;
    }

    free(output_filename);
    image_destroy(cropped);
}

static void finish_output_screenshot(
    WrappedOutput * /* output */, Image *image, void * /* data */
) {
    image_save_png(image, "./screenshot.png");
    image_destroy(image);
    is_finished = true;
}

// This function uses logical coordinates
static void finish_predefined_region_screenshot(
    WrappedOutput *output, Image *image, void *data
) {
    BBox crop_bounds = *(BBox *)data;
    // move to output space
    crop_bounds = bbox_translate(
        crop_bounds, -output->logical_bounds.x, -output->logical_bounds.y
    );

    double scale_factor_x = image->width / output->logical_bounds.width;
    double scale_factor_y = image->height / output->logical_bounds.height;
    assert(fabs(scale_factor_x - scale_factor_y) < 0.01);
    // move to device space
    crop_bounds = bbox_scale(crop_bounds, scale_factor_x);
    // cropping takes place in pixels, which are whole, so round off any
    // potential inaccuracies
    crop_bounds = bbox_round(crop_bounds);

    crop_and_save_image(image, crop_bounds, false);
    image_destroy(image);
}

static void region_picker_finish(
    RegionPicker *picker, RegionPickerFinishReason reason, BBox result_region
) {
    RegionPickerListEntry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &active_pickers, link) {
        if (entry->picker != picker) {
            continue;
        }

        if (reason == REGION_PICKER_FINISH_REASON_SELECTED) {
            crop_and_save_image(entry->image, result_region, true);
        } else if (reason == REGION_PICKER_FINISH_REASON_CANCELLED) {
            printf("selection cancelled\n");
            is_finished = true;
        }

        region_picker_destroy(entry->picker);
        image_destroy(entry->image);
        wl_list_remove(&entry->link);
        free(entry);
    }

    // The DESTROYED reason is the only one that isn't user-initiated,
    // and shouldn't destroy all the others.
    if (reason != REGION_PICKER_FINISH_REASON_DESTROYED) {
        wl_list_for_each_safe(entry, tmp, &active_pickers, link) {
            region_picker_destroy(entry->picker);
            image_destroy(entry->image);
            wl_list_remove(&entry->link);
            free(entry);
        }
    }
}

static void create_region_picker_for_output(
    WrappedOutput *output, Image *image, void * /* data */
) {
    // Save it in a list so that it can be properly destroyed later
    RegionPickerListEntry *entry = calloc(1, sizeof(RegionPickerListEntry));
    entry->picker = region_picker_new(output, image, region_picker_finish);
    entry->image = image;
    wl_list_insert(&active_pickers, &entry->link);
}

static void add_new_output(WrappedOutput *output) {
    log_debug(
        "Got output %p with name %s\n",
        (void *)output->wl_output,
        output->name ? output->name : "NULL"
    );

    if (args->mode == CAPTURE_OUTPUT) {
        if (strcmp(output->name, args->output_params.output_name) == 0) {
            log_debug("...which is correct\n");
            correct_output_found = true;
            take_output_screenshot(output, finish_output_screenshot, NULL);
        }
    } else if (args->mode == CAPTURE_REGION) {
        if (args->region_params.has_region) {
            if (bbox_contains(
                    output->logical_bounds, args->region_params.region
                )) {
                log_debug("... which is correct\n");
                correct_output_found = true;
                take_output_screenshot(
                    output,
                    finish_predefined_region_screenshot,
                    &args->region_params.region
                );
            }
        } else {
            // This is for debugging so I don't lock myself out of my terminal
            const char *only_output_name = getenv("SPACESHOT_PICKER_ONLY");
            if (only_output_name &&
                strcmp(output->name, only_output_name) != 0) {
                return;
            }
            correct_output_found = true;

            take_output_screenshot(
                output, create_region_picker_for_output, NULL
            );
        }
    } else {
        REPORT_UNHANDLED("mode", "%x", args->mode);
    }
}

int main(int argc, char **argv) {
    wl_list_init(&active_pickers);

    set_program_name(argv[0]);
    args = parse_argv(argc, argv);

    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        report_error_fatal("failed to connect to Wayland display");
    }

    bool found_everything = find_wayland_globals(display, &add_new_output);
    if (!found_everything) {
        report_error_fatal("didn't find every required Wayland object");
    }

    wl_display_roundtrip(display);
    if (!correct_output_found) {
        const char *output_name = args->mode == CAPTURE_OUTPUT
                                      ? args->output_params.output_name
                                      : "[unspecified]";
        report_error_fatal("couldn't find output %s", output_name);
    }

    while (wl_display_dispatch(display) != -1) {
        if (is_finished)
            break;
    }

    wl_display_disconnect(display);
    return 0;
}
