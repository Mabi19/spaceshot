#pragma once
#include "bbox.h"

// These declarations live in their own header to prevent circular includes.

typedef enum {
    WRAPPED_OUTPUT_HAS_NAME = 1,
    WRAPPED_OUTPUT_HAS_LOGICAL_POSITION = 2,
    WRAPPED_OUTPUT_HAS_LOGICAL_SIZE = 4,
    WRAPPED_OUTPUT_CREATE_WAS_CALLED = 8,
    WRAPPED_OUTPUT_HAS_ALL = 1 | 2 | 4,
} WrappedOutputFillState;

typedef struct {
    struct wl_output *wl_output;
    struct zxdg_output_v1 *xdg_output;
    const char *name;
    BBox logical_bounds;
    WrappedOutputFillState fill_state;
} WrappedOutput;
