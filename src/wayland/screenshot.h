#include "image.h"
#include "wayland/globals.h"
#include <wayland-client.h>

typedef void (*ScreenshotCallback)(
    WrappedOutput *output, Image *image, void *data
);

/** Takes a screenshot of an output.
 * You are responsible for `image_free`ing the result yourself. */
void take_output_screenshot(
    WrappedOutput *output, ScreenshotCallback image_callback, void *data
);
