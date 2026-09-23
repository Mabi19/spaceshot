#include "log.h"
#include "render/renderer.h"
#include "screen-capture.h"
#include "wayland/toplevel.h"
#include <assert.h>
#include <config/config.h>

const Image *capture_frame_get_image(CaptureFrame *frame) {
    // TODO: when image downloading exists, use it
    return frame->image;
}

const RenderTexture *capture_frame_get_texture(CaptureFrame *frame) {
    if (!frame->texture) {
        assert(frame->image);
        frame->texture =
            renderer_get_default()->texture_new_from_image(frame->image);
    }
    return frame->texture;
}

Image *capture_frame_steal_image(CaptureFrame *frame) {
    // TODO: when image downloading exists, use it like capture_frame_get_image
    Image *result = frame->image;
    // Renderer textures may borrow from the image's data,
    // so they have to be destroyed as well.
    if (frame->texture) {
        renderer_get_default()->texture_destroy(frame->texture);
        frame->texture = NULL;
    }
    frame->image = NULL;
    return result;
}

void capture_frame_destroy(CaptureFrame *frame) {
    log_debug("destroying capture frame %p\n", (void *)frame);
    if (frame->texture) {
        renderer_get_default()->texture_destroy(frame->texture);
    }
    if (frame->image) {
        image_destroy(frame->image);
    }
    wl_list_remove(&frame->link);
    free(frame);
}

void capture_frame_destroy_list(struct wl_list *captures) {
    CaptureFrame *frame, *tmp;
    wl_list_for_each_safe(frame, tmp, captures, link) {
        capture_frame_destroy(frame);
    }
}

void capture_output_ext(
    WrappedOutput *output, FrameCaptureCallback image_callback, void *data
);
bool capture_output_ext_is_available();
void capture_output_wlr(
    WrappedOutput *output, FrameCaptureCallback image_callback, void *data
);
bool capture_output_wlr_is_available();

typedef enum {
    OUTPUT_CAPTURE_BACKEND_NONE,
    OUTPUT_CAPTURE_BACKEND_EXT,
    OUTPUT_CAPTURE_BACKEND_WLR,
} OutputCaptureBackend;

void capture_output(
    WrappedOutput *output, FrameCaptureCallback image_callback, void *data
) {
    static bool has_selected_backend = false;
    static OutputCaptureBackend backend;

    if (!has_selected_backend) {
        has_selected_backend = true;
        backend = OUTPUT_CAPTURE_BACKEND_NONE;

        auto backends = config_get()->output_capture_backends;
        for (size_t i = 0; i < backends.count; i++) {
            switch (backends.items[i]) {
            case CONFIG_OUTPUT_CAPTURE_BACKENDS_ITEM_EXT:
                log_debug("trying output backend ext...\n");
                if (capture_output_ext_is_available()) {
                    backend = OUTPUT_CAPTURE_BACKEND_EXT;
                    goto end;
                }
                break;
            case CONFIG_OUTPUT_CAPTURE_BACKENDS_ITEM_WLR:
                log_debug("trying output backend wlr...\n");
                if (capture_output_wlr_is_available()) {
                    backend = OUTPUT_CAPTURE_BACKEND_WLR;
                    goto end;
                }
                break;
            default:
                REPORT_UNHANDLED(
                    "output capture backend", "%d", backends.items[i]
                );
            }
        }
    end:
        log_debug("chosen output backend: %d\n", backend);
    }

    switch (backend) {
    case OUTPUT_CAPTURE_BACKEND_NONE:
        report_error_fatal("couldn't choose an output capture backend");
    case OUTPUT_CAPTURE_BACKEND_EXT:
        capture_output_ext(output, image_callback, data);
        break;
    case OUTPUT_CAPTURE_BACKEND_WLR:
        capture_output_wlr(output, image_callback, data);
        break;
    }
}

void capture_toplevel_ext(
    WrappedToplevel *toplevel, FrameCaptureCallback image_callback, void *data
);

bool capture_toplevel_ext_is_available();

typedef enum {
    TOPLEVEL_CAPTURE_BACKEND_NONE,
    TOPLEVEL_CAPTURE_BACKEND_EXT,
} ToplevelCaptureBackend;

void capture_toplevel(
    WrappedToplevel *toplevel, FrameCaptureCallback image_callback, void *data
) {
    static bool has_selected_backend = false;
    static ToplevelCaptureBackend backend;

    if (!has_selected_backend) {
        has_selected_backend = true;
        backend = TOPLEVEL_CAPTURE_BACKEND_NONE;

        if (capture_toplevel_ext_is_available()) {
            backend = TOPLEVEL_CAPTURE_BACKEND_EXT;
        }

        log_debug("chosen output backend: %d\n", backend);
    }

    switch (backend) {
    case TOPLEVEL_CAPTURE_BACKEND_NONE:
        report_error_fatal("couldn't choose an output capture backend");
    case TOPLEVEL_CAPTURE_BACKEND_EXT:
        capture_toplevel_ext(toplevel, image_callback, data);
        break;
    }
}
