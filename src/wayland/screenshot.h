#include "image.h"
#include "wayland/output.h"
#include <wayland-client.h>

/**
 * The image may be NULL if an error occurred while screenshotting.
 * Note that the output isn't guaranteed to exist when this function is called.
 */
typedef void (*ScreenshotCallback)(
    WrappedOutput *output, Image *image, void *data
);

/** Takes a screenshot of an output.
 * You are responsible for `image_free`ing the result yourself. */
void capture_output(
    WrappedOutput *output, ScreenshotCallback image_callback, void *data
);
