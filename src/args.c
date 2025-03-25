#include "args.h"
#include "bbox.h"
#include "log.h"
#include <build-config.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(const char *program_name) {
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

static void print_version(const char *program_name) {
    printf("%s version %s\n", program_name, SPACESHOT_VERSION);
}

static void interpret_option(Arguments *args, char opt, char *value) {
    switch (opt) {
    case 'h':
        print_help(args->executable_name);
        exit(EXIT_SUCCESS);
    case 'v':
        print_version(args->executable_name);
        exit(EXIT_SUCCESS);
    default:
        REPORT_UNHANDLED("converted option", "%c", opt);
    }
}

typedef struct {
    char *long_name;
    char short_name;
    bool has_parameter;
} LongOption;

static const LongOption LONG_OPTIONS[] = {
    {"output", 'o', true}, {"verbose", 'V', false}
};
static const int LONG_OPTION_COUNT = sizeof(LONG_OPTIONS) / sizeof(LongOption);

Arguments *parse_argv(int argc, char **argv) {
    Arguments *result = calloc(1, sizeof(Arguments));
    result->executable_name = argv[0];

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        // do not include flags where the first character is a digit,
        // because that can be part of a region specifier
        if (arg[0] == '-' && arg[1] != '\0' && !isdigit(arg[1])) {
            // this is a flag
            if (arg[1] == '-') {
                // long option
                char *option_contents = arg + 2;
                char *equals_sign = strchr(arg, '=');
                size_t provided_len = equals_sign
                                          ? equals_sign - option_contents
                                          : strlen(option_contents);

                for (int i = 0; i < LONG_OPTION_COUNT; i++) {
                    // TODO
                    REPORT_UNHANDLED(
                        "long options are unimplemented", "%s", ":("
                    );
                }

                report_error(
                    "invalid option %s\ntry %s --help for more information",
                    arg,
                    argv[0]
                );
                goto error;
            } else {
                // short option, map to equiv. short option
                for (int i = 1; arg[i] != '\0'; i++) {
                    switch (arg[i]) {
                    // no-argument options
                    case 'h':
                    case 'v':
                        interpret_option(result, arg[i], NULL);
                        break;
                    // options with arguments
                    // must be at the end of the string
                    case 'o':
                        if (arg[i + 1] != '\0') {
                            report_error(
                                "option -%c requires an argument and "
                                "can't be placed here",
                                arg[i]
                            );
                        }
                        if (i + 1 >= argc) {
                            report_error(
                                "option -%c requires an argument", arg[i]
                            );
                        }
                        interpret_option(result, arg[i], argv[i + 1]);
                        // the next argument is consumed
                        i++;
                        break;
                    default:
                        report_error(
                            "invalid option %c\ntry %s --help for more "
                            "information",
                            arg[i],
                            argv[0]
                        );
                        goto error;
                    }
                }
            }
        } else {
            if (result->captured_mode_params == 0) {
                // this is the mode
                char *mode = argv[i];
                if (strcmp(mode, "help") == 0) {
                    print_help(argv[0]);
                    exit(EXIT_SUCCESS);
                }

                if (strcmp(mode, "version") == 0) {
                    print_version(argv[0]);
                    exit(EXIT_SUCCESS);
                }

                if (strcmp(mode, "output") == 0) {
                    result->mode = CAPTURE_OUTPUT;
                    result->output_params =
                        (OutputCaptureParams){.output_name = NULL};
                } else if (strcmp(mode, "region") == 0) {
                    result->mode = CAPTURE_REGION;
                    result->region_params.region =
                        (BBox){.x = 0.0, .y = 0.0, .width = 0.0, .height = 0.0};
                    result->region_params.has_region = false;
                } else {
                    report_error(
                        "invalid mode %s\n"
                        "valid modes are 'output <output-name>' "
                        "and 'region [region]'",
                        mode
                    );
                    goto error;
                }
            } else {
                // this is a mode parameter
                if (result->mode == CAPTURE_OUTPUT) {
                    if (result->captured_mode_params == 1) {
                        result->output_params.output_name = arg;
                    } else {
                        report_error(
                            "too many parameters for mode 'output' (max 1)"
                        );
                        goto error;
                    }
                } else if (result->mode == CAPTURE_REGION) {
                    if (result->captured_mode_params == 1) {
                        if (!bbox_parse(arg, &result->region_params.region)) {
                            report_error(
                                "invalid region\nregion format is 'X,Y WxH'"
                            );
                            goto error;
                        }

                        result->region_params.has_region = true;
                    } else {
                        report_error(
                            "too many parameters for mode 'region' (max 1)"
                        );
                        goto error;
                    }
                } else {
                    REPORT_UNHANDLED("mode", "%d", result->mode);
                    goto error;
                }
            }

            result->captured_mode_params++;
        }
    }

    if (result->captured_mode_params == 0) {
        report_error(
            "a mode is required\ntry %s --help for more information", argv[0]
        );
    }

    // capture output mode requires an output name
    if (result->mode == CAPTURE_OUTPUT && result->captured_mode_params < 2) {
        report_error("an output name is required");
        goto error;
    }

    return result;
error:
    free(result);
    exit(EXIT_FAILURE);
}
