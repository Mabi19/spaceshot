#include "args.h"
#include "bbox.h"
#include "image.h"
#include "region-picker.h"
#include "wayland/globals.h"
#include "wayland/screenshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

static bool is_finished = false;
static bool correct_output_found = false;
static Arguments *args;

static void
save_image(WrappedOutput * /* output */, Image *image, void * /* data */) {
    printf(
        "Got image from wayland: %dx%d, %d bytes in total\n",
        image->width,
        image->height,
        image->stride * image->height
    );
    printf(
        "Top-left pixel: %x %x %x\n",
        image->data[2],
        image->data[1],
        image->data[0]
    );

    image_save_png(image, "./screenshot.png");
    image_destroy(image);
    is_finished = true;
}

static void
crop_and_save_image(WrappedOutput *output, Image *image, void *data) {
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
    image_save_png(cropped, "./screenshot.png");
    image_destroy(cropped);
    is_finished = true;
}

static void create_region_picker_for_output(
    WrappedOutput *output, Image *image, void * /* data */
) {
    // TODO: Save this so that it can be destroyed properly later
    region_picker_new(output, image);
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
            take_output_screenshot(output, save_image, NULL);
        }
    } else if (args->mode == CAPTURE_REGION) {
        if (args->region_params.has_region) {
            if (bbox_contains(
                    output->logical_bounds, args->region_params.region
                )) {
                printf("... which is correct\n");
                correct_output_found = true;
                take_output_screenshot(
                    output, crop_and_save_image, &args->region_params.region
                );
            }
        } else {
            //! This is for debugging so that I don't lock myself out of my
            //! terminal
            if (strcmp(output->name, "HDMI-A-1") != 0) {
                return;
            }
            correct_output_found = true;

            take_output_screenshot(
                output, create_region_picker_for_output, NULL
            );
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
        // printf("waiting\n");
    }

    wl_display_disconnect(display);
    return 0;
}
