#pragma once
#include "bbox.h"
#include <stdint.h>

typedef enum {
    CAPTURE_OUTPUT,
    CAPTURE_REGION,
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
    const char *output_filename;
    const char *executable_name;
} Arguments;

Arguments *parse_argv(int argc, char **argv);
