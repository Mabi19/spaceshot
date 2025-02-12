#include "args.h"
#include "image.h"
#include "wayland/globals.h"
#include "wayland/screenshot.h"
#include <stdio.h>
#include <wayland-client-core.h>
#include <wayland-client.h>
#include <wayland-util.h>

void add_new_output(OutputListElement *output) {
    printf(
        "Got output %p with name %s\n",
        (void *)output->output,
        output->name ? output->name : "NULL"
    );
}

static bool is_finished = false;

void save_image(Image *image) {
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

int main(int argc, char **argv) {
    Arguments *args = parse_argv(argc, argv);

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

    OutputListElement *test_output;
    test_output =
        wl_container_of(wayland_globals.outputs.next, test_output, link);
    take_output_screenshot(test_output->output, &save_image);
    while (wl_display_dispatch(display) != -1) {
        if (is_finished)
            break;
        printf("waiting\n");
    }

    wl_display_disconnect(display);
    return 0;
}
