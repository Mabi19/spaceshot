#pragma once
#include <stdint.h>

typedef enum {
    CAPTURE_OUTPUT,
    CAPTURE_REGION,
} CaptureMode;

typedef struct {
    char *output_name;
} OutputCaptureParams;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
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
