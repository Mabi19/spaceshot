#pragma once
#include "image.h"
#include "render/texture.h"
#include "wayland/output.h"
#include "wayland/toplevel.h"
#include <wayland-client.h>

typedef enum {
    CAPTURE_FRAME_TYPE_OUTPUT,
    CAPTURE_FRAME_TYPE_TOPLEVEL,
} CaptureFrameType;

/**
 * A helper struct to assist with using captured frames.
 * At least one of the image or texture fields will be set;
 * the getter for the other one will create it on demand.
 * This is because with dmabuf-based capture (not implemented yet),
 * an Image will not exist, only a texture created from the dmabuf.
 */
typedef struct CaptureFrame {
    Image *image;
    RenderTexture *texture;
    uint32_t width;
    uint32_t height;
    /**
     * A pixel format which can store the frame,
     * not necessarily the image data's format.
     * Used to create drawing canvases.
     */
    ImageFormat compatible_format;
    CaptureFrameType type;
    union {
        /** Valid if type is OUTPUT. */
        WrappedOutput *output;
        /** Valid if type is TOPLEVEL. */
        WrappedToplevel *toplevel;
    };
    /** main.c stores these in a linked list */
    struct wl_list link;
} CaptureFrame;

/** The image is owned by the frame. */
const Image *capture_frame_get_image(CaptureFrame *frame);
/** The texture is owned by the frame. */
const RenderTexture *capture_frame_get_texture(CaptureFrame *frame);
/**
 * Take ownership of the capture frame's image.
 * After this, the capture frame is left in an invalid state.
 */
Image *capture_frame_steal_image(CaptureFrame *frame);

void capture_frame_destroy(CaptureFrame *frame);
/**
 * Convenience function to destroy all of the CaptureFrames in a list.
 */
void capture_frame_destroy_list(struct wl_list *captures);

/**
 * The frame may be NULL if an error occurred while screenshotting.
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
