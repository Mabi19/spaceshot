#include "args.h"
#include "image.h"
#include "wayland/globals.h"
#include "wayland/screenshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

static bool is_finished = false;
static bool correct_output_found = false;
static Arguments *args;

static void save_image(Image *image) {
    printf(
        "Got image from wayland: %dx%d, %d bytes in total\n",
        image->width,
        image->height,
        image->stride * image->height
    );
    printf(
        "Top-left pixel: %x %x %x\n",
        image->data[0],
        image->data[1],
        image->data[2]
    );

    image_save_png(image, "./screenshot.png");
    image_destroy(image);
    is_finished = true;
}

static void add_new_output(WrappedOutput *output) {
    printf(
        "Got output %p with name %s\n",
        (void *)output->wl_output,
        output->name ? output->name : "NULL"
    );

    if (args->mode == CAPTURE_OUTPUT) {
        if (strcmp(output->name, args->output_params.output_name) == 0) {
            printf("...which is correct\n");
            correct_output_found = true;
            take_output_screenshot(output->wl_output, save_image);
        }
    } else if (args->mode == CAPTURE_REGION) {
        if (args->region_params.has_region) {
            // TODO: test if contained
            if (bbox_contains(
                    output->logical_bounds, args->region_params.region
                )) {
                printf("... which is correct\n");
                correct_output_found = true;
                take_output_screenshot(output->wl_output, save_image);
            }
        } else {
            // TODO: open the picker
        }
    } else {
        fprintf(
            stderr, "%s: unhandled mode %d\n", args->executable_name, args->mode
        );
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv) {
    args = parse_argv(argc, argv);

    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return 1;
    }

    bool found_everything = find_wayland_globals(display, &add_new_output);
    if (!found_everything) {
        fprintf(stderr, "Didn't find every required Wayland object\n");
        return 1;
    }

    wl_display_roundtrip(display);
    if (!correct_output_found) {
        const char *output_name = args->mode == CAPTURE_OUTPUT
                                      ? args->output_params.output_name
                                      : "[unspecified]";
        fprintf(
            stderr,
            "%s: couldn't find output %s\n",
            args->executable_name,
            output_name
        );
        exit(EXIT_FAILURE);
    }

    while (wl_display_dispatch(display) != -1) {
        if (is_finished)
            break;
        printf("waiting\n");
    }

    wl_display_disconnect(display);
    return 0;
}
