#include "args.h"
#include "bbox.h"
#include "debug.h"
#include "image.h"
#include "link-buffer.h"
#include "log.h"
#include "paths.h"
#include "picker/context.h"
#include "render/renderer.h"
#include "wayland/clipboard.h"
#include "wayland/globals.h"
#include "wayland/output.h"
#include "wayland/screen-capture.h"
#include "wayland/toplevel.h"
#include <assert.h>
#include <config/config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <threads.h>
#include <unistd.h>
#include <wayland-client.h>

// First, Wayland should be polled until an "active wait" flag is unset,
// then the process should detach as to not block when waiting for others to
// paste, then Wayland should be polled until a "clipboard wait" flag is unset.
static bool should_active_wait = true;
static bool should_clipboard_wait = false;
// This flag causes an unsuccessful exit code to be returned from main.
static bool was_cancelled = false;
static Arguments args;
// image data is obtained some time after publishing the copy,
// so these need to be kept at a higher level
ClipboardCopy *copy_source = NULL;
ClipboardCopyOffer *image_png_offer = NULL;
/** list of CaptureFrame */
static struct wl_list active_captures;
// Captures only appear in the list when ready.
// This counter helps with waiting until everything's captured.
static int pending_captures = 0;
static PickerContext pickers;
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

static void clipboard_copy_finish(ClipboardCopy *source) {
    clipboard_copy_destroy(source);
    should_clipboard_wait = false;
}

static void finish_noninteractive_screenshot(Image *image) {
    LinkBuffer *out_data = image_save_png(image);

    ClipboardCopy *copy_source = NULL;
    if (config_get()->copy_to_clipboard) {
        copy_source = clipboard_copy_setup(false);
    }
    // the copy may not be successful
    if (copy_source) {
        copy_source->finished = clipboard_copy_finish;
        ClipboardCopyOffer *offer =
            clipboard_copy_offer_mime(copy_source, "image/png");
        offer->buffer = out_data;
        clipboard_copy_activate(copy_source);
        clipboard_copy_run(copy_source);
        should_clipboard_wait = true;
    }

    char *output_filename = get_output_filename();
    save_screenshot(out_data, output_filename);
    send_notification(output_filename, copy_source != NULL);

    free(output_filename);
    // copies take ownership of link buffers
    // so only destroy now if NOT copied
    if (!copy_source) {
        link_buffer_destroy(out_data);
    }

    should_active_wait = false;
}

// This function uses logical coordinates
static void finish_predefined_region_screenshot(
    WrappedOutput *output, Image *image, BBox crop_bounds
) {
    if (!is_output_valid(output)) {
        report_error("output disappeared while screenshotting");
        should_active_wait = false;
        return;
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

    finish_noninteractive_screenshot(image);
    image_destroy(cropped);
}

// The finalize_? functions are not static,
// because they are called by the picker context.

void finalize_picker_setup() {
    if (config_get()->copy_to_clipboard) {
        // Set up the copy while the picker's still alive
        copy_source = clipboard_copy_setup(true);
        assert(copy_source);
        copy_source->finished = clipboard_copy_finish;
        image_png_offer = clipboard_copy_offer_mime(copy_source, "image/png");
        clipboard_copy_activate(copy_source);
    }
}

void finalize_picker_cancel() {
    printf("selection cancelled\n");
    was_cancelled = true;
    should_active_wait = false;
}

void finalize_picker_finish(Image *result) {
    // saving is an expensive operation - flush the display first so that
    // the pickers are properly closed before we block
    // TODO: properly poll the display fd if EAGAIN
    if (wl_display_flush(display) == -1) {
        report_warning("flushing Wayland display failed before encoding image");
    }

    LinkBuffer *out_data = image_save_png(result);
    image_destroy(result);
    if (image_png_offer) {
        image_png_offer->buffer = out_data;
        clipboard_copy_run(copy_source);
        should_clipboard_wait = true;
    }

    char *output_filename = get_output_filename();
    save_screenshot(out_data, output_filename);
    send_notification(output_filename, copy_source != NULL);

    free(output_filename);
    should_active_wait = false;
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
        // if we're not deferring outputs, we're not listening for them at all
        return true;
    }
    // in toplevel mode, we don't need outputs
    // (this can happen if we defer into toplevel, and an output appears after
    // the mode switch)

    return false;
}

static bool is_toplevel_matching(WrappedToplevel *toplevel) {
    if (args.mode == CAPTURE_TOPLEVEL) {
        if (args.toplevel_params.toplevel_id) {
            if (strcmp(
                    toplevel->identifier, args.toplevel_params.toplevel_id
                ) == 0) {
                return true;
            }
        } else {
            return true;
        }
    } else if (args.mode == CAPTURE_DEFER) {
        // if we're not deferring toplevels, we're not listening for them at all
        return true;
    }
    // in non-toplevel modes, we don't need toplevels

    return false;
}

static void handle_captured_output(CaptureFrame *frame, void *data) {
    WrappedOutput *output = data;
    pending_captures--;

    if (!is_output_valid(output)) {
        report_error("output disappeared while screenshotting");
        if (frame) {
            capture_frame_destroy(frame);
        }
        return;
    }

    if (!frame) {
        report_error_fatal("capturing output %s failed", output->name);
    }

    wl_list_insert(&active_captures, &frame->link);
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

    const char *output_filter_name = getenv("SPACESHOT_OUTPUT_FILTER");
    if (output_filter_name) {
        bool invert = false;
        if (output_filter_name[0] == '!') {
            output_filter_name++;
            invert = true;
        }
        bool name_matches = strcmp(output->name, output_filter_name) == 0;
        if (name_matches == invert) {
            return;
        }
    }

    if (!is_output_valid(output)) {
        report_error("output disappeared while screenshotting");
        return;
    }

    // If we can rule out needing to capture any outputs right now, do that
    if (!is_output_matching(output)) {
        return;
    }

    // We use the output as user data here because if capturing fails,
    // we can use it to print out the error message without the frame object
    pending_captures++;
    capture_output(output, handle_captured_output, output);
}

static void handle_captured_toplevel(CaptureFrame *frame, void *data) {
    WrappedToplevel *toplevel = data;
    pending_captures--;

    if (!is_toplevel_valid(toplevel)) {
        report_error("toplevel disappeared while screenshotting");
        if (frame) {
            capture_frame_destroy(frame);
        }
        return;
    }

    if (!frame) {
        report_error_fatal(
            "capturing toplevel %s failed\n", toplevel->identifier
        );
    }

    wl_list_insert(&active_captures, &frame->link);
}

static void add_new_toplevel(WrappedToplevel *toplevel) {
    // See add_new_output for comments on this function.
    log_debug(
        "Got toplevel %p with id %s and title %s\n",
        (void *)toplevel->handle,
        toplevel->identifier,
        toplevel->title
    );

    if (!should_active_wait) {
        return;
    }

    if (!is_toplevel_matching(toplevel)) {
        return;
    }

    pending_captures++;
    capture_toplevel(toplevel, handle_captured_toplevel, toplevel);
}

static void get_required_capture_types(bool *output, bool *toplevel) {
    switch (args.mode) {
    case CAPTURE_REGION:
    case CAPTURE_OUTPUT:
        *output = true;
        *toplevel = false;
        break;
    case CAPTURE_TOPLEVEL:
        *output = false;
        *toplevel = true;
        break;
    case CAPTURE_DEFER:
        *output = args.defer_params.needs_output;
        *toplevel = args.defer_params.needs_toplevel;
        break;
    default:
        REPORT_UNHANDLED("capture mode", "%d", args.mode);
    }
}

static void read_deferred_args() {
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
    if (new_argc > 0) {
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
    free(new_argv);
}

/**
 * Act on the captured frames,
 * based on the selected mode and its parameters.
 */
static void dispatch_captures() {
    // Get rid of any potential invalidated entries first
    // so we don't have to check everywhere
    {
        CaptureFrame *frame, *tmp;
        wl_list_for_each_safe(frame, tmp, &active_captures, link) {
            switch (frame->type) {
            case CAPTURE_FRAME_TYPE_OUTPUT:
                if (!is_output_valid(frame->output)) {
                    capture_frame_destroy(frame);
                }
                break;
            case CAPTURE_FRAME_TYPE_TOPLEVEL:
                if (!is_toplevel_valid(frame->toplevel)) {
                    capture_frame_destroy(frame);
                }
                break;
            default:
                REPORT_UNHANDLED("capture entry type", "%d", frame->type);
            }
        }
    }

    if (args.mode == CAPTURE_REGION) {
        if (args.region_params.has_region) {
            CaptureFrame *frame;
            bool found = false;
            wl_list_for_each(frame, &active_captures, link) {
                if (frame->type == CAPTURE_FRAME_TYPE_OUTPUT &&
                    is_output_matching(frame->output)) {
                    finish_predefined_region_screenshot(
                        frame->output,
                        capture_frame_get_image(frame),
                        args.region_params.region
                    );
                    found = true;
                    break;
                }
            }
            if (found) {
                capture_frame_destroy_list(&active_captures);
            } else {
                report_error_fatal("couldn't find matching output");
            }
        } else {
            picker_context_init(&pickers, PICKER_TYPE_REGION, &active_captures);
        }
    } else if (args.mode == CAPTURE_OUTPUT) {
        if (args.output_params.output_name) {
            CaptureFrame *frame;
            bool found = false;
            wl_list_for_each(frame, &active_captures, link) {
                if (frame->type == CAPTURE_FRAME_TYPE_OUTPUT &&
                    is_output_matching(frame->output)) {
                    finish_noninteractive_screenshot(
                        capture_frame_get_image(frame)
                    );
                    found = true;
                    break;
                }
            }
            if (found) {
                capture_frame_destroy_list(&active_captures);
            } else {
                report_error_fatal("couldn't find matching output");
            }
        } else {
            int output_count = 0;
            CaptureFrame *frame;
            wl_list_for_each(frame, &active_captures, link) {
                if (frame->type == CAPTURE_FRAME_TYPE_OUTPUT) {
                    output_count++;
                }
            }
            if (output_count == 1) {
                wl_list_for_each(frame, &active_captures, link) {
                    if (frame->type == CAPTURE_FRAME_TYPE_OUTPUT) {
                        finish_noninteractive_screenshot(
                            capture_frame_get_image(frame)
                        );
                        capture_frame_destroy(frame);
                        break;
                    }
                }
            } else if (output_count > 1) {
                picker_context_init(
                    &pickers, PICKER_TYPE_OUTPUT, &active_captures
                );
            } else {
                report_error_fatal("no outputs captured");
            }
        }
    } else if (args.mode == CAPTURE_TOPLEVEL) {
        if (args.toplevel_params.toplevel_id) {
            CaptureFrame *frame;
            bool found = false;
            wl_list_for_each(frame, &active_captures, link) {
                if (frame->type == CAPTURE_FRAME_TYPE_TOPLEVEL &&
                    is_toplevel_matching(frame->toplevel)) {
                    finish_noninteractive_screenshot(
                        capture_frame_get_image(frame)
                    );
                    found = true;
                    break;
                }
            }
            if (found) {
                capture_frame_destroy_list(&active_captures);
            } else {
                report_error_fatal("couldn't find matching toplevel");
            }
        } else {
            report_error_fatal("there is no toplevel picker");
        }
    } else if (args.mode == CAPTURE_DEFER) {
        printf("ready\n");
        fflush(stdout);
        read_deferred_args();

        if (args.mode == CAPTURE_DEFER) {
            report_error_fatal("mode selection already deferred");
        }

        // This function will error out and exit the program if it can't find
        // matching capture targets
        dispatch_captures();
    }
}

// defined in debug.c
extern void init_debug_mode();

int main(int argc, char **argv) {
    wl_list_init(&active_captures);

    TIMING_START(config_load);
    config_load();
    TIMING_END(config_load);
    set_program_name(argv[0]);
    init_debug_mode();
    args.executable_name = argv[0];
    parse_argv(&args, argc - 1, argv + 1);

    display = wl_display_connect(NULL);
    if (!display) {
        report_error_fatal("failed to connect to Wayland display");
    }

    bool needs_output, needs_toplevel;
    get_required_capture_types(&needs_output, &needs_toplevel);

    bool found_everything = find_wayland_globals(
        display,
        needs_output ? add_new_output : NULL,
        needs_toplevel ? add_new_toplevel : NULL
    );
    if (!found_everything) {
        report_error_fatal("didn't find every required Wayland object");
    }

    wl_display_roundtrip(display);
    // Non-matching outputs/toplevels are gonna be excluded from this count.
    if (pending_captures == 0) {
        report_error_fatal("couldn't find matching capture target");
    }

    // Wait for all the captured outputs to be ready.
    TIMING_START(capture);
    while (pending_captures > 0) {
        log_debug("waiting for %d pending captures\n", pending_captures);
        wl_display_dispatch(display);
    }
    TIMING_END(capture);

    dispatch_captures();

    while (wl_display_dispatch(display) != -1) {
        if (!should_active_wait) {
            break;
        }
    }

    // By this point all of the pickers are done.
    renderer_cleanup();

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
    // destroying some objects is async, so wait a bit
    wl_display_roundtrip(display);
    wl_display_disconnect(display);
    int exit_code = was_cancelled ? 1 : 0;
    return exit_code;
}
