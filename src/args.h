#pragma once
#include "bbox.h"
#include <stdint.h>

typedef enum {
    CAPTURE_OUTPUT,
    CAPTURE_REGION,
    CAPTURE_DEFER,
} CaptureMode;

typedef struct {
    char *output_name;
} OutputCaptureParams;

typedef struct {
    BBox region;
    /** Whether a region was specified. */
    bool has_region;
} RegionCaptureParams;

typedef struct {
    CaptureMode mode;
    union {
        OutputCaptureParams output_params;
        RegionCaptureParams region_params;
    };
    int captured_mode_params;
    const char *executable_name;
} Arguments;

void parse_argv(Arguments *out, int argc, char **argv);
