#include "args.h"
#include "bbox.h"
#include "config.h"
#include "image.h"
#include "link-buffer.h"
#include "log.h"
#include "notifications.h"
#include "paths.h"
#include "region-picker.h"
#include "wayland/globals.h"
#include "wayland/screenshot.h"
#include "wayland/seat.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <wayland-client.h>

typedef struct {
    RegionPicker *picker;
    Image *image;
    struct wl_list link;
} RegionPickerListEntry;

// First, Wayland should be polled until an "active wait" flag is unset,
// then the process should detach as to not block when waiting for others to
// paste, then Wayland should be polled until a "clipboard wait" flag is unset.
// If a second event loop is added later (such as for notifications),
// that can go in its own thread that is later join()ed.
static bool should_active_wait = true;
static bool should_clipboard_wait = false;
static bool correct_output_found = false;
static Arguments *args;
static struct wl_list active_pickers;
static struct wl_display *display;
#ifdef SPACESHOT_NOTIFICATIONS
static bool has_notify_thread = false;
static thrd_t notify_thread;
#endif

/**
 * Save an already-encoded image to disk.
 */
static void
save_screenshot(LinkBuffer *encoded_image, const char *output_filename) {
    FILE *out_file = fopen(output_filename, "wb");
    assert(out_file);
    link_buffer_write(encoded_image, out_file);
    fclose(out_file);
}

static void finish_output_screenshot(
    WrappedOutput * /* output */, Image *image, void * /* data */
) {
    LinkBuffer *out_data = image_save_png(image);
    image_destroy(image);

    char *output_filename = get_output_filename();
    save_screenshot(out_data, output_filename);
    // TODO: consider sending a notification here
    free(output_filename);
    link_buffer_destroy(out_data);

    should_active_wait = false;
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

    Image *cropped = image_crop(
        image,
        crop_bounds.x,
        crop_bounds.y,
        crop_bounds.width,
        crop_bounds.height
    );
    image_destroy(image);

    LinkBuffer *out_data = image_save_png(cropped);
    image_destroy(cropped);

    char *output_filename = get_output_filename();
    save_screenshot(out_data, output_filename);
    // TODO: consider sending a notification here
    free(output_filename);
    link_buffer_destroy(out_data);

    should_active_wait = false;
}

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
    const char * /* mime_type */,
    int fd
) {
    LinkBuffer *data_to_send = data;
    FILE *wrapped_fd = fdopen(fd, "w");
    if (!wrapped_fd) {
        perror("fdopen");
        close(fd);
        report_error_fatal("couldn't open clipboard fd %d", fd);
    }
    link_buffer_write(data_to_send, wrapped_fd);
    fclose(wrapped_fd);
}

static void
clipboard_handle_cancelled(void *data, struct wl_data_source *data_source) {
    LinkBuffer *stale_clipboard_data = data;
    link_buffer_destroy(stale_clipboard_data);
    wl_data_source_destroy(data_source);
    should_clipboard_wait = false;
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

static void region_picker_finish(
    RegionPicker *picker, RegionPickerFinishReason reason, BBox result_region
) {
    RegionPickerListEntry *entry, *tmp;
    Image *to_save = NULL;
    struct wl_data_source *data_source;
    wl_list_for_each_safe(entry, tmp, &active_pickers, link) {
        if (entry->picker != picker) {
            continue;
        }

        if (reason == REGION_PICKER_FINISH_REASON_SELECTED) {
            if (get_config()->should_copy_to_clipboard) {
                // Set up the copy while the picker's still alive
                data_source = wl_data_device_manager_create_data_source(
                    wayland_globals.data_device_manager
                );
                wl_data_source_offer(data_source, "image/png");
                seat_dispatcher_set_selection(
                    wayland_globals.seat_dispatcher, data_source
                );
            }

            to_save = image_crop(
                entry->image,
                result_region.x,
                result_region.y,
                result_region.width,
                result_region.height
            );
            // should_active_wait is unset later
        } else if (reason == REGION_PICKER_FINISH_REASON_CANCELLED) {
            printf("selection cancelled\n");
            should_active_wait = false;
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

    if (to_save) {
        // saving is an expensive operation - flush the display first so that
        // the region picker is properly closed before we block
        // TODO: properly poll the display fd if EAGAIN
        if (wl_display_flush(display) == -1) {
            report_warning(
                "flushing Wayland display failed before encoding image"
            );
        }

        LinkBuffer *out_data = image_save_png(to_save);
        image_destroy(to_save);
        if (get_config()->should_copy_to_clipboard) {
            wl_data_source_add_listener(
                data_source, &clipboard_source_listener, out_data
            );
            should_clipboard_wait = true;
        }

        char *output_filename = get_output_filename();
        save_screenshot(out_data, output_filename);

#ifdef SPACESHOT_NOTIFICATIONS
        if (get_config()->should_notify) {
            if (!notify_for_file(&notify_thread, strdup(output_filename))) {
                report_error("Couldn't spawn notification thread");
            } else {
                has_notify_thread = true;
            }
        }
#endif
        free(output_filename);
        should_active_wait = false;
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

    load_config();
    set_program_name(argv[0]);
    args = parse_argv(argc, argv);

    display = wl_display_connect(NULL);
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
        if (!should_active_wait) {
            break;
        }
    }

    if (should_clipboard_wait) {
        if (get_config()->move_to_background) {
            // double-fork
            // I'm not quite sure why this works, but according to daemon(7)
            // it should prevent the process from re-acquiring terminals
            pid_t pid = fork();
            if (pid == 0) {
                // child
                setsid();
                pid_t pid = fork();
                if (pid != 0) {
                    // parent
                    return 0;
                }
            } else {
                // parent
                return 0;
            }

            int dev_null = open("/dev/null", O_RDWR);
            if (dev_null >= 0) {
                dup2(dev_null, STDOUT_FILENO);
                dup2(dev_null, STDIN_FILENO);
                dup2(dev_null, STDERR_FILENO);
            } else {
                close(STDOUT_FILENO);
                close(STDIN_FILENO);
                close(STDERR_FILENO);
            }

            chdir("/");
        }

        while (wl_display_dispatch(display) != -1) {
            if (!should_clipboard_wait) {
                break;
            }
        }
    }

#ifdef SPACESHOT_NOTIFICATIONS
    if (has_notify_thread) {
        thrd_join(notify_thread, NULL);
    }
#endif

    // TODO: clean up wayland_globals

    wl_display_disconnect(display);
    return 0;
}
