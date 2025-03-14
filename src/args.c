#include "args.h"
#include "bbox.h"
#include "log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(char *program_name) {
    printf("Usage: %s <mode> <mode parameters> [options]\n", program_name);
    printf(
        "Modes:\n"
        "  - output <output-name>: screenshot an entire output\n"
        "  - region [region]: screenshot a region\n"
        "    region format is 'X,Y WxH', in global compositor space\n"
        "    if region is not specified, opens the region picker to let the "
        "user choose\n"
        "    note that the region must be fully contained within one output\n"
    );
}

Arguments *parse_argv(int argc, char **argv) {
    Arguments *result = calloc(1, sizeof(Arguments));
    result->executable_name = argv[0];

    if (argc == 1) {
        report_error(
            "mode is required\nTry '%s --help' for more information.", argv[0]
        );
        goto error;
    }

    if (argc >= 2) {
        char *mode = argv[1];
        if (strcmp(mode, "help") == 0 || strcmp(mode, "--help") == 0 ||
            strcmp(mode, "-h") == 0) {
            print_help(argv[0]);
            exit(EXIT_SUCCESS);
        }

        if (strcmp(mode, "output") == 0) {
            result->mode = CAPTURE_OUTPUT;
            result->output_params = (OutputCaptureParams){.output_name = NULL};
        } else if (strcmp(mode, "region") == 0) {
            result->mode = CAPTURE_REGION;
            result->region_params.region =
                (BBox){.x = 0.0, .y = 0.0, .width = 0.0, .height = 0.0};
            result->region_params.has_region = false;
        } else {
            report_error(
                "invalid mode %s\n"
                "Valid modes are 'output <output-name>' "
                "and 'region [region]'",
                argv[1]
            );
            if (argv[1][0] == '-') {
                fprintf(
                    stderr, "note: flags must be specified after the mode\n"
                );
            }
            goto error;
        }
    }

    for (int i = 2; i < argc; i++) {
        char *arg = argv[i];
        int arg_len = strlen(arg);
        // do not include flags where the first character is a digit,
        // because that can be part of a region specifier
        if (arg[0] == '-' && !(arg_len > 1 && isdigit(arg[1]))) {
            // this is a flag
            // TODO
        } else {
            // this is a mode parameter
            if (result->mode == CAPTURE_OUTPUT) {
                if (result->captured_mode_params == 0) {
                    result->output_params.output_name = arg;
                } else {
                    report_error("too many parameters for mode 'output' (max 1)"
                    );
                    goto error;
                }
            } else if (result->mode == CAPTURE_REGION) {
                if (result->captured_mode_params == 0) {
                    if (!bbox_parse(arg, &result->region_params.region)) {
                        report_error(
                            "invalid region\nregion format is 'X,Y WxH'"
                        );
                        goto error;
                    }

                    result->region_params.has_region = true;
                } else {
                    report_error("too many parameters for mode 'region' (max 1)"
                    );
                    goto error;
                }
            } else {
                REPORT_UNHANDLED("mode", "%d", result->mode);
                goto error;
            }

            result->captured_mode_params++;
        }
    }

    // capture output mode requires an output name
    if (result->mode == CAPTURE_OUTPUT && result->captured_mode_params < 1) {
        report_error("an output name is required");
        goto error;
    }

    return result;
error:
    free(result);
    exit(EXIT_FAILURE);
}
