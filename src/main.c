#include "args.h"
#include "bbox.h"
#include "image.h"
#include "link-buffer.h"
#include "log.h"
#include "output-picker.h"
#include "paths.h"
#include "region-picker.h"
#include "wayland/globals.h"
#include "wayland/screenshot.h"
#include "wayland/seat.h"
#include <assert.h>
#include <config/config.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <threads.h>
#include <unistd.h>
#include <wayland-client.h>

typedef enum {
    PICKER_ENTRY_REGION,
    PICKER_ENTRY_OUTPUT,
} PickerListEntryType;

typedef struct {
    void *picker;
    PickerListEntryType type;
    Image *image;
    struct wl_list link;
} PickerListEntry;

// First, Wayland should be polled until an "active wait" flag is unset,
// then the process should detach as to not block when waiting for others to
// paste, then Wayland should be polled until a "clipboard wait" flag is unset.
static bool should_active_wait = true;
static bool should_clipboard_wait = false;
static bool correct_output_found = false;
// This flag causes an unsuccessful exit code to be returned from main.
static bool was_cancelled = false;
static Arguments *args;
static struct wl_list active_pickers;
static struct wl_display *display;

/**
 * Save an already-encoded image to disk.
 */
static void
save_screenshot(LinkBuffer *encoded_image, const char *output_filename) {
    FILE *out_file;
    if (strcmp(output_filename, "-") == 0) {
        out_file = fdopen(dup(STDOUT_FILENO), "wb");
    } else {
        out_file = fopen(output_filename, "wb");
    }
    assert(out_file);
    link_buffer_write(encoded_image, out_file);
    fclose(out_file);
}

static void send_notification(char *output_filename, bool did_copy) {
#ifdef SPACESHOT_NOTIFICATIONS
    if (config_get()->notify.enabled) {
        pid_t pid = fork();
        if (pid == 0) {
            // child
            char *notify_bin_path = getenv("SPACESHOT_NOTIFY_PATH");
            notify_bin_path =
                notify_bin_path ? notify_bin_path : "spaceshot-notify";
            if (did_copy) {
                execlp(
                    notify_bin_path,
                    "spaceshot-notify",
                    "-p",
                    output_filename,
                    "-c",
                    NULL
                );
            } else {
                execlp(
                    notify_bin_path,
                    "spaceshot-notify",
                    "-p",
                    output_filename,
                    NULL
                );
            }

            // if something has gone terribly wrong, exit
            // 104 is a random number that is used as a heuristic for when
            // exec() failed
            exit(104);
        } else if (pid == -1) {
            report_error("Couldn't spawn spaceshot-notify");
        } else {
            // parent
            // wait for the child to exit; usually this doesn't take too long
            // (and the layers are closed by this point)
            TIMING_START(exec_spaceshot_notify);
            int status;
            waitpid(pid, &status, WUNTRACED);
            if (WIFEXITED(status)) {
                int status_code = WEXITSTATUS(status);
                if (status_code != 0) {
                    if (status_code == 104) {
                        report_warning(
                            "Couldn't invoke spaceshot-notify; is it in "
                            "PATH?\ntip: notifications require installing the "
                            "spaceshot-notify binary and its D-Bus service "
                            "definition"
                        );
                    } else {
                        report_warning(
                            "spaceshot-notify exited with status code %d",
                            WEXITSTATUS(status)
                        );
                    }
                }
            } else {
                report_warning("spaceshot-notify didn't exit?");
            }
            TIMING_END(exec_spaceshot_notify);
        }
    }
#endif
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

static void finish_predefined_output_screenshot(
    WrappedOutput * /* output */, Image *image, void * /* data */
) {
    if (!image) {
        report_error("Capturing output failed");

        goto defer;
    }

    LinkBuffer *out_data = image_save_png(image);

    char *output_filename = get_output_filename();
    save_screenshot(out_data, output_filename);
    send_notification(output_filename, false);

    free(output_filename);
    link_buffer_destroy(out_data);

defer:
    image_destroy(image);
    should_active_wait = false;
}

// This function uses logical coordinates
static void finish_predefined_region_screenshot(
    WrappedOutput *output, Image *image, void *data
) {
    if (!is_output_valid(output)) {
        report_error("Output disappeared while screenshotting");
        goto defer;
    }
    if (!image) {
        report_error("Capturing output failed");
        goto defer;
    }

    // NOTE: this isn't dynamically allocated
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

    LinkBuffer *out_data = image_save_png(cropped);
    image_destroy(cropped);

    char *output_filename = get_output_filename();
    save_screenshot(out_data, output_filename);
    send_notification(output_filename, false);

    free(output_filename);
    link_buffer_destroy(out_data);

defer:
    image_destroy(image);
    should_active_wait = false;
}

static void picker_entry_destroy(PickerListEntry *entry) {
    switch (entry->type) {
    case PICKER_ENTRY_REGION:
        region_picker_destroy(entry->picker);
        break;
    case PICKER_ENTRY_OUTPUT:
        output_picker_destroy(entry->picker);
        break;
    }
    image_destroy(entry->image);
    wl_list_remove(&entry->link);
    free(entry);
}

static void picker_entry_destroy_all() {
    PickerListEntry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &active_pickers, link) {
        picker_entry_destroy(entry);
    }
}

static void picker_finish_generic(
    void *picker,
    PickerFinishReason reason,
    Image *(*result_image_callback)(PickerListEntry *entry, void *data),
    void *data
) {
    PickerListEntry *entry, *tmp;
    Image *to_save = NULL;
    struct wl_data_source *data_source = NULL;
    bool should_copy = config_get()->copy_to_clipboard;
    wl_list_for_each_safe(entry, tmp, &active_pickers, link) {
        if (entry->picker != picker) {
            continue;
        }

        if (reason == PICKER_FINISH_REASON_SELECTED) {
            if (config_get()->copy_to_clipboard) {
                // Set up the copy while the picker's still alive
                data_source = wl_data_device_manager_create_data_source(
                    wayland_globals.data_device_manager
                );
                wl_data_source_offer(data_source, "image/png");
                seat_dispatcher_set_selection(
                    wayland_globals.seat_dispatcher, data_source
                );
            }

            to_save = result_image_callback(entry, data);
            // should_active_wait is unset later
        } else if (reason == PICKER_FINISH_REASON_CANCELLED) {
            printf("selection cancelled\n");
            was_cancelled = true;
            should_active_wait = false;
        }

        picker_entry_destroy(entry);
    }

    // The DESTROYED reason is the only one that isn't user-initiated,
    // and shouldn't destroy all the others.
    if (reason != PICKER_FINISH_REASON_DESTROYED) {
        picker_entry_destroy_all();
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
        if (should_copy) {
            wl_data_source_add_listener(
                data_source, &clipboard_source_listener, out_data
            );
            should_clipboard_wait = true;
        }

        char *output_filename = get_output_filename();
        save_screenshot(out_data, output_filename);
        send_notification(output_filename, should_copy);

        free(output_filename);
        should_active_wait = false;
    }
}

static Image *
region_picker_finish_get_image(PickerListEntry *entry, void *data) {
    BBox result_region = *(BBox *)data;
    return image_crop(
        entry->image,
        result_region.x,
        result_region.y,
        result_region.width,
        result_region.height
    );
}

static void region_picker_finish(
    RegionPicker *picker, PickerFinishReason reason, BBox result_region
) {
    picker_finish_generic(
        picker, reason, region_picker_finish_get_image, &result_region
    );
}

static Image *output_picker_finish_get_image(PickerListEntry *entry, void *) {
    // picker_finish_generic frees this separately from the source image
    return image_copy(entry->image);
}

static void
output_picker_finish(OutputPicker *picker, PickerFinishReason reason) {
    picker_finish_generic(picker, reason, output_picker_finish_get_image, NULL);
}

static PickerListEntry *make_picker_entry(WrappedOutput *output, Image *image) {
    if (!is_output_valid(output)) {
        // Theoretically, if all the outputs disappear (and no new ones appear),
        // there will be no region pickers, so should_active_wait will never be
        // unset, so the process will remain forever. But I don't think that
        // should ever happen.
        report_error("Output disappeared while screenshotting");
        return NULL;
    }

    if (!image) {
        // Similarly, I don't really have a good way of handling all the
        // screenshots failing.
        report_error("Output capture failed");
        return NULL;
    }
    PickerListEntry *entry = calloc(1, sizeof(PickerListEntry));
    entry->image = image;
    wl_list_insert(&active_pickers, &entry->link);

    return entry;
}

static void create_region_picker_for_output(
    WrappedOutput *output, Image *image, void * /* data */
) {
    PickerListEntry *entry = make_picker_entry(output, image);
    if (entry) {
        entry->picker = region_picker_new(output, image, region_picker_finish);
        entry->type = PICKER_ENTRY_REGION;
    }
}

static void create_output_picker_for_output(
    WrappedOutput *output, Image *image, void * /* data */
) {
    PickerListEntry *entry = make_picker_entry(output, image);
    if (entry) {
        entry->picker = output_picker_new(output, image, output_picker_finish);
        entry->type = PICKER_ENTRY_OUTPUT;
    }
}

static void add_new_output(WrappedOutput *output) {
    log_debug(
        "Got output %p with name %s\n",
        (void *)output->wl_output,
        output->name ? output->name : "NULL"
    );

    // New outputs shouldn't be accepted if spaceshot is in the background
    if (!should_active_wait) {
        return;
    }

    if (args->mode == CAPTURE_OUTPUT) {
        if (args->output_params.output_name) {
            // output specified in command line
            if (strcmp(output->name, args->output_params.output_name) == 0) {
                log_debug("...which is correct\n");
                correct_output_found = true;
                capture_output(
                    output, finish_predefined_output_screenshot, NULL
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

            capture_output(output, create_output_picker_for_output, NULL);
        }
    } else if (args->mode == CAPTURE_REGION) {
        if (args->region_params.has_region) {
            if (bbox_contains(
                    output->logical_bounds, args->region_params.region
                )) {
                log_debug("... which is correct\n");
                correct_output_found = true;
                capture_output(
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

            capture_output(output, create_region_picker_for_output, NULL);
        }
    } else {
        REPORT_UNHANDLED("mode", "%x", args->mode);
    }
}

int main(int argc, char **argv) {
    wl_list_init(&active_pickers);

    TIMING_START(config_load);
    config_load();
    TIMING_END(config_load);
    set_program_name(argv[0]);
    args = parse_argv(argc, argv);

    display = wl_display_connect(NULL);
    if (!display) {
        report_error_fatal("failed to connect to Wayland display");
    }

    // Note that there's no need for a destroy callback here: the layer surfaces
    // should get closed on their own (they don't care about the output after
    // creation either), and ongoing screenshot operations can't really receive
    // a destroyed callback (so they will check is_output_valid)
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
        signal(SIGPIPE, SIG_IGN);
        if (config_get()->move_to_background) {
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
                    _exit(0);
                }
            } else {
                // parent
                _exit(0);
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

    cleanup_wayland_globals();
    wl_display_disconnect(display);
    int exit_code = was_cancelled ? 1 : 0;
    return exit_code;
}
