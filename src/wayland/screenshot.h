#include "image.h"
#include <wayland-client.h>

typedef void (*ScreenshotCallback)(Image *);

/** Takes a screenshot of an output.
 * You are responsible for `image_free`ing the result yourself. */
void take_output_screenshot(
    struct wl_output *output, ScreenshotCallback image_callback
);
