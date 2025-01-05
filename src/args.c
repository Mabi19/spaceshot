#include "args.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(char *program_name) {
    printf("Usage: %s <mode> <mode parameters> [options]\n", program_name);
    printf(
        "Modes:\n"
        "  - output <output-name>: screenshot an entire output\n"
        "  - region [region] [output-name]: screenshot a region\n"
        "    region format is 'X,Y WxH'\n"
        "    if output-name is specified, then the region is relative to that "
        "output, otherwise it's in global compositor space\n"
        "    if region is not specified, opens the region picker to let the "
        "user choose\n"
        "    note that the region must be fully contained within one output\n"
    );
}

Arguments *parse_argv(int argc, char **argv) {
    Arguments *result = calloc(1, sizeof(Arguments));

    if (argc == 1) {
        fprintf(
            stderr,
            "%s: mode is required\nTry '%s --help' for more information.\n",
            argv[0],
            argv[0]
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
            result->region_params =
                (RegionCaptureParams){.x = 0, .y = 0, .width = 0, .height = 0};
        } else {
            fprintf(
                stderr,
                "%s: invalid mode %s\n"
                "Valid modes are 'output <output-name>' "
                "and 'region [output-region] [output-name]'\n",
                argv[0],
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
                    fprintf(
                        stderr,
                        "%s: too many parameters for mode 'output' (max 1)\n",
                        argv[0]
                    );
                    goto error;
                }
            } else if (result->mode == CAPTURE_REGION) {
                if (result->captured_mode_params == 0) {
                    int read_char_count;
                    int read_specifier_count = sscanf(
                        arg,
                        "%d,%d %ux%u%n",
                        &result->region_params.x,
                        &result->region_params.y,
                        &result->region_params.width,
                        &result->region_params.height,
                        &read_char_count
                    );

                    if (read_specifier_count != 4 ||
                        read_char_count != arg_len) {
                        fprintf(
                            stderr,
                            "%s: invalid region\nregion format is 'X,Y WxH'\n",
                            argv[0]
                        );
                        goto error;
                    }
                } else if (result->captured_mode_params == 1) {
                    result->region_params.output_name = arg;
                } else {
                    fprintf(
                        stderr,
                        "%s: too many parameters for mode 'region' (max 2)\n",
                        argv[0]
                    );
                    goto error;
                }
            } else {
                fprintf(
                    stderr, "internal error: unhandled mode %d\n", result->mode
                );
                goto error;
            }

            result->captured_mode_params++;
        }
    }

    // capture output mode requires an output name
    if (result->mode == CAPTURE_OUTPUT && result->captured_mode_params < 1) {
        fprintf(stderr, "%s: an output name is required\n", argv[0]);
        goto error;
    }

    return result;
error:
    free(result);
    exit(EXIT_FAILURE);
}
