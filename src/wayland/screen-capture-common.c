#include "config/config.h"
#include "log.h"
#include "screen-capture.h"

void capture_output_ext(
    WrappedOutput *output, OutputCaptureCallback image_callback, void *data
);

bool capture_output_ext_is_available();

void capture_output_wlr(
    WrappedOutput *output, OutputCaptureCallback image_callback, void *data
);

bool capture_output_wlr_is_available();

typedef enum {
    OUTPUT_CAPTURE_BACKEND_NONE,
    OUTPUT_CAPTURE_BACKEND_EXT,
    OUTPUT_CAPTURE_BACKEND_WLR,
} OutputCaptureBackend;

void capture_output(
    WrappedOutput *output, OutputCaptureCallback image_callback, void *data
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
