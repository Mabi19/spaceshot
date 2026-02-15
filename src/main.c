#include "args.h"
#include "bbox.h"
#include "image.h"
#include "link-buffer.h"
#include "log.h"
#include "output-picker.h"
#include "paths.h"
#include "region-picker.h"
#include "wayland/globals.h"
#include "wayland/output.h"
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
#include <wayland-util.h>

typedef enum {
    /* Hasn't yet received its image. */
    CAPTURE_ENTRY_EMPTY = 0,
    /* Has a region picker active. */
    CAPTURE_ENTRY_REGION_PICKER,
    /* Has an output picker active. */
    CAPTURE_ENTRY_OUTPUT_PICKER,
    /* Has an image saved, and will eventually be transformed into one of the
       other types. */
    CAPTURE_ENTRY_DEFER,
} CaptureEntryType;

typedef struct {
    void *picker;
    CaptureEntryType type;
    WrappedOutput *output;
    Image *image;
    struct wl_list link;
} CaptureEntry;

// First, Wayland should be polled until an "active wait" flag is unset,
// then the process should detach as to not block when waiting for others to
// paste, then Wayland should be polled until a "clipboard wait" flag is unset.
static bool should_active_wait = true;
static bool should_clipboard_wait = false;
static bool correct_output_found = false;
// This flag causes an unsuccessful exit code to be returned from main.
static bool was_cancelled = false;
static Arguments args;
static struct wl_list active_captures;
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
                log_debug(
                    "invoking 'spaceshot-notify -p %s -c\n", output_filename
                );
                execlp(
                    notify_bin_path,
                    "spaceshot-notify",
                    "-p",
                    output_filename,
                    "-c",
                    NULL
                );
            } else {
                log_debug(
                    "invoking 'spaceshot-notify -p %s\n", output_filename
                );
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
            report_error("couldn't spawn spaceshot-notify");
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

static void finish_predefined_output_screenshot(Image *image) {
    if (!image) {
        report_error("capturing output failed");
        goto defer;
    }

    LinkBuffer *out_data = image_save_png(image);

    char *output_filename = get_output_filename();
    save_screenshot(out_data, output_filename);
    send_notification(output_filename, false);

    free(output_filename);
    link_buffer_destroy(out_data);

defer:
    should_active_wait = false;
}

// This function uses logical coordinates
static void finish_predefined_region_screenshot(
    WrappedOutput *output, Image *image, BBox crop_bounds
) {
    if (!is_output_valid(output)) {
        report_error("output disappeared while screenshotting");
        goto defer;
    }
    if (!image) {
        report_error("capturing output failed");
        goto defer;
    }

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
    should_active_wait = false;
}

static void capture_entry_destroy(CaptureEntry *entry) {
    if (entry->picker) {
        switch (entry->type) {
        case CAPTURE_ENTRY_REGION_PICKER:
            region_picker_destroy(entry->picker);
            break;
        case CAPTURE_ENTRY_OUTPUT_PICKER:
            output_picker_destroy(entry->picker);
            break;
        default:
            REPORT_UNHANDLED("picker entry type", "%d", entry->type);
        }
    }
    image_destroy(entry->image);
    wl_list_remove(&entry->link);
    free(entry);
}

static void picker_entry_destroy_all() {
    CaptureEntry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &active_captures, link) {
        capture_entry_destroy(entry);
    }
}

static void picker_finish_generic(
    void *picker,
    PickerFinishReason reason,
    Image *(*result_image_callback)(CaptureEntry *entry, void *data),
    void *data
) {
    CaptureEntry *entry, *tmp;
    Image *to_save = NULL;
    struct wl_data_source *data_source = NULL;
    bool should_copy = config_get()->copy_to_clipboard;
    wl_list_for_each_safe(entry, tmp, &active_captures, link) {
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

        capture_entry_destroy(entry);
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

static Image *region_picker_finish_get_image(CaptureEntry *entry, void *data) {
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

static Image *output_picker_finish_get_image(CaptureEntry *entry, void *) {
    // picker_finish_generic frees this separately from the source image
    return image_copy(entry->image);
}

static void
output_picker_finish(OutputPicker *picker, PickerFinishReason reason) {
    picker_finish_generic(picker, reason, output_picker_finish_get_image, NULL);
}

static CaptureEntry *make_capture_entry(WrappedOutput *output) {
    if (!is_output_valid(output)) {
        // Theoretically, if all the outputs disappear (and no new ones appear),
        // there will be no region pickers, so should_active_wait will never be
        // unset, so the process will remain forever. But I don't think that
        // should ever happen.
        report_error("output disappeared while screenshotting");
        return NULL;
    }
    CaptureEntry *entry = calloc(1, sizeof(CaptureEntry));
    entry->output = output;
    wl_list_insert(&active_captures, &entry->link);

    return entry;
}

static bool is_output_matching(WrappedOutput *output) {
    if (args.mode == CAPTURE_OUTPUT) {
        if (args.output_params.output_name) {
            // output specified in command line
            if (strcmp(output->name, args.output_params.output_name) == 0) {
                return true;
            }
        } else {
            return true;
        }
    } else if (args.mode == CAPTURE_REGION) {
        if (args.region_params.has_region) {
            if (bbox_contains(
                    output->logical_bounds, args.region_params.region
                )) {
                return true;
            }
        } else {
            return true;
        }
    } else if (args.mode == CAPTURE_DEFER) {
        return true;
    } else {
        REPORT_UNHANDLED("mode", "%x", args.mode);
    }

    return false;
}

static void handle_captured_output(Image *image, void *data) {
    CaptureEntry *entry = data;

    if (!is_output_valid(entry->output)) {
        report_error("output disappeared while screenshotting");
        capture_entry_destroy(entry);
        return;
    }

    // If these entries are getting reactivated after being in defer mode,
    // the image's already set.
    if (image) {
        entry->image = image;
    }

    // This can't be checked early if mode selection was deferred,
    // so also do it now. Returning if this is false also means
    // the following logic doesn't have to check this
    if (is_output_matching(entry->output)) {
        correct_output_found = true;
    } else {
        capture_entry_destroy(entry);
        return;
    }

    if (args.mode == CAPTURE_OUTPUT) {
        if (args.output_params.output_name) {
            finish_predefined_output_screenshot(entry->image);
            capture_entry_destroy(entry);
        } else {
            entry->picker = output_picker_new(
                entry->output, entry->image, output_picker_finish
            );
            entry->type = CAPTURE_ENTRY_OUTPUT_PICKER;
        }
    } else if (args.mode == CAPTURE_REGION) {
        if (args.region_params.has_region) {
            finish_predefined_region_screenshot(
                entry->output, entry->image, args.region_params.region
            );
            capture_entry_destroy(entry);
        } else {
            entry->picker = region_picker_new(
                entry->output, entry->image, region_picker_finish
            );
            entry->type = CAPTURE_ENTRY_REGION_PICKER;
        }
    } else if (args.mode == CAPTURE_DEFER) {
        entry->type = CAPTURE_ENTRY_DEFER;
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

    const char *only_output_name = getenv("SPACESHOT_PICKER_ONLY");
    if (only_output_name && strcmp(output->name, only_output_name) != 0) {
        return;
    }

    if (is_output_matching(output)) {
        correct_output_found = true;
    } else {
        return;
    }

    CaptureEntry *entry = make_capture_entry(output);
    if (entry) {
        capture_output(output, handle_captured_output, entry);
    }
}

int main(int argc, char **argv) {
    wl_list_init(&active_captures);

    TIMING_START(config_load);
    config_load();
    TIMING_END(config_load);
    set_program_name(argv[0]);
    args.executable_name = argv[0];
    parse_argv(&args, argc - 1, argv + 1);

    display = wl_display_connect(NULL);
    if (!display) {
        report_error_fatal("failed to connect to Wayland display");
    }

    // Note that there's no need for a destroy callback here: the layer
    // surfaces should get closed on their own (they don't care about the
    // output after creation either), and ongoing screenshot operations
    // can't really receive a destroyed callback (so they will check
    // is_output_valid)
    bool found_everything = find_wayland_globals(display, &add_new_output);
    if (!found_everything) {
        report_error_fatal("didn't find every required Wayland object");
    }

    wl_display_roundtrip(display);
    if (!correct_output_found) {
        report_error_fatal("couldn't find matching output");
    }

    if (args.mode == CAPTURE_DEFER) {
        // Wait for all the captured outputs to be ready.
        while (true) {
            CaptureEntry *entry;
            bool is_waiting = false;
            wl_list_for_each(entry, &active_captures, link) {
                if (entry->type == CAPTURE_ENTRY_EMPTY) {
                    log_debug("waiting for picker entry %p\n", (void *)entry);
                    is_waiting = true;
                    break;
                }
            }
            if (is_waiting) {
                wl_display_dispatch(display);
            } else {
                break;
            }
        }

        printf("ready\n");
        fflush(stdout);
        // Read stdin until EOF and use that as the actual arguments.
        char **new_argv = NULL;
        int new_argc = 0;
        int capacity = 16;
        new_argv = malloc(capacity * sizeof(char *));

        char *line = NULL;
        size_t line_len = 0;
        ssize_t nread;
        while ((nread = getdelim(&line, &line_len, '\0', stdin)) != -1) {
            if (new_argc >= capacity) {
                capacity *= 2;
                char **new_ptr = realloc(new_argv, capacity * sizeof(char *));
                new_argv = new_ptr;
            }
            new_argv[new_argc++] = line;
            line = NULL;
            line_len = 0;
        }
        if (ferror(stdin)) {
            report_error_fatal("couldn't read deferred arguments");
        }
        free(line);

        // It's really easy to accidentally put a newline at the end of the last
        // argument, so trim it.
        if (argc > 0) {
            char *last_arg = new_argv[new_argc - 1];
            size_t len = strlen(last_arg);
            if (len > 0 && last_arg[len - 1] == '\n') {
                last_arg[len - 1] = '\0';
            }
        }

        args.captured_mode_params = 0;
        parse_argv(&args, new_argc, new_argv);

        for (int i = 0; i < new_argc; i++) {
            log_debug("new arg: '%s'\n", new_argv[i]);
            free(new_argv[i]);
        }

        if (args.mode == CAPTURE_DEFER) {
            report_error_fatal("mode selection already deferred");
        }

        correct_output_found = false;
        CaptureEntry *entry, *tmp;
        wl_list_for_each_safe(entry, tmp, &active_captures, link) {
            handle_captured_output(NULL, entry);
        }

        if (!correct_output_found) {
            report_error_fatal("couldn't find matching output");
        }
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

            int ret = chdir("/");
            if (ret != 0) {
                // If this happens, something really weird is going on,
                // but it technically doesn't break anything for us
                report_error("chdir failed: %s", strerror(errno));
            }
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
