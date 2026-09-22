#pragma once
#include "image.h"
#include "render/texture.h"
#include "wayland/output.h"
#include "wayland/toplevel.h"
#include <wayland-client.h>

/**
 * A helper struct to assist with using captured frames.
 * At least one of the fields will be set;
 * the getter for the other one will create it on demand.
 * This is because with dmabuf-based capture (not implemented yet),
 * an Image will not exist, only a texture created from the dmabuf.
 */
typedef struct {
    Image *image;
    RenderTexture *texture;
    /**
     * A pixel format which can store the frame,
     * not necessarily the image data's format.
     * Used to create drawing canvases.
     */
    ImageFormat pixel_format;
} CaptureFrame;

Image *capture_frame_get_image(CaptureFrame *frame);
RenderTexture *capture_frame_get_texture(CaptureFrame *frame);
void capture_frame_destroy(CaptureFrame *frame);

/**
 * The image may be NULL if an error occurred while screenshotting.
 * Note that the output isn't guaranteed to exist when this function is called.
 */
typedef void (*FrameCaptureCallback)(CaptureFrame *frame, void *data);

/**
 * Takes a screenshot of an output.
 * You are responsible for destroying the result yourself.
 */
void capture_output(
    WrappedOutput *output, FrameCaptureCallback image_callback, void *data
);

/**
 * Takes a screenshot of a toplevel.
 * You are responsible for destroying the result yourself.
 */
void capture_toplevel(
    WrappedToplevel *toplevel, FrameCaptureCallback image_callback, void *data
);
