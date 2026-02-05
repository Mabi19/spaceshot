#include "args.h"
#include "bbox.h"
#include "log.h"
#include <build-config.h>
#include <config/config.h>
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
        "  - defer: prepare a screenshot for later\n"
        "    prints 'ready' when everything is captured; "
        "afterwards, write mode and additional arguments to stdin, separated "
        "by null bytes, to continue as normal\n"
        "Options:\n"
        "  -h, --help        display this help and exit\n"
        "  -v, --version     output version information and exit\n"
        "  -b, --background  move to background after screenshotting\n"
        "  -c, --copy        copy screenshot to clipboard\n"
        "  --no-copy         do not copy the screenshot\n"
        "  -C, --config-file load an additional configuration file\n"
        "  -n, --notify      send a notification after screenshotting\n"
        "  --no-notify       do not send notifications\n"
        "  -o, --output-file set output file path template\n"
        "  --verbose         enable debug logging\n"
    );
}

static void print_version() {
    printf("spaceshot version %s\n", SPACESHOT_VERSION);
}

static void interpret_option(Arguments *args, char opt, char *value) {
    switch (opt) {
    case 'b':
        config_get()->move_to_background = true;
        break;
    case 'h':
        print_help(args->executable_name);
        exit(EXIT_SUCCESS);
    case 'c':
        config_get()->copy_to_clipboard = true;
        break;
    case '!':
        // only as --no-copy
        config_get()->copy_to_clipboard = false;
        break;
    case 'C':
        if (!config_load_file(value)) {
            report_warning("couldn't read configuration file %s", value);
        }
        break;
    case 'n':
        config_get()->notify.enabled = true;
        break;
    case '@':
        config_get()->notify.enabled = false;
        break;
    case 'o':
        free(config_get()->output_file);
        config_get()->output_file = strdup(value);
        break;
    case '#':
        // only as --verbose
        config_get()->verbose = true;
        break;
    case 'v':
        print_version();
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
    {"background", 'b', false},
    {"copy", 'c', false},
    {"no-copy", '!', false},
    {"config-file", 'C', true},
    {"help", 'h', false},
    {"notify", 'n', false},
    {"no-notify", '@', false},
    {"output-file", 'o', true},
    {"verbose", '#', false},
    {"version", 'v', false}
};
static const int LONG_OPTION_COUNT = sizeof(LONG_OPTIONS) / sizeof(LongOption);

void parse_argv(Arguments *result, int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        char *arg = argv[i];
        // do not include flags where the first character is a digit,
        // because that can be part of a region specifier
        if (arg[0] == '-' && arg[1] != '\0' && !isdigit(arg[1])) {
            // this is a flag
            if (arg[1] == '-') {
                // long option
                char *option_contents = arg + 2;
                char *equals_sign = strchr(arg, '=');
                size_t provided_len =
                    equals_sign ? (size_t)(equals_sign - option_contents)
                                : strlen(option_contents);

                for (int j = 0; j < LONG_OPTION_COUNT; j++) {
                    const LongOption *option = &LONG_OPTIONS[j];
                    for (size_t k = 0; k < provided_len; k++) {
                        if (option->long_name[k] != option_contents[k]) {
                            goto next;
                        }
                    }
                    if (option->has_parameter) {
                        if (equals_sign) {
                            interpret_option(
                                result, option->short_name, equals_sign + 1
                            );
                        } else {
                            if (i + 1 >= argc) {
                                report_error(
                                    "option --%s requires an argument",
                                    option_contents
                                );
                                goto error;
                            }
                            interpret_option(
                                result, option->short_name, argv[i + 1]
                            );
                            // the next argument is consumed
                            i++;
                        }
                    } else {
                        interpret_option(result, option->short_name, NULL);
                    }
                    goto finish_option;
                next:
                }

                report_error(
                    "invalid option %s\ntry %s --help for more information",
                    arg,
                    result->executable_name
                );
                goto error;

            finish_option:
            } else {
                // short option
                for (int j = 1; arg[j] != '\0'; j++) {
                    switch (arg[j]) {
                    // no-argument options
                    case 'b':
                    case 'c':
                    case 'n':
                    case 'h':
                    case 'v':
                        interpret_option(result, arg[j], NULL);
                        break;
                    // options with arguments
                    // must be at the end of the string
                    case 'C':
                    case 'o':
                        if (arg[j + 1] != '\0') {
                            report_error(
                                "option -%c requires an argument and "
                                "can't be placed here",
                                arg[j]
                            );
                            goto error;
                        }
                        if (j + 1 >= argc) {
                            report_error(
                                "option -%c requires an argument", arg[j]
                            );
                            goto error;
                        }
                        interpret_option(result, arg[j], argv[i + 1]);
                        // the next argument is consumed
                        i++;
                        break;
                    default:
                        log_debug(
                            "arg: %s, i: %d, argv[i + 1]: %s\n",
                            arg,
                            j,
                            argv[i + 1]
                        );
                        report_error(
                            "invalid option -%c\ntry %s --help for more "
                            "information",
                            arg[j],
                            result->executable_name
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
                    print_help(result->executable_name);
                    exit(EXIT_SUCCESS);
                }

                if (strcmp(mode, "version") == 0) {
                    print_version();
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
                } else if (strcmp(mode, "defer") == 0) {
                    result->mode = CAPTURE_DEFER;
                } else {
                    report_error(
                        "invalid mode %s\n"
                        "valid modes are 'output <output-name>', "
                        "'region [region]', "
                        "and 'defer'\n",
                        mode
                    );
                    goto error;
                }
            } else {
                // this is a mode parameter
                if (result->mode == CAPTURE_OUTPUT) {
                    if (result->captured_mode_params == 1) {
                        result->output_params.output_name = strdup(arg);
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
                } else if (result->mode == CAPTURE_DEFER) {
                    report_error(
                        "too many parameters for mode 'defer' (max 0)"
                    );
                    goto error;
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
            "a mode is required\ntry %s --help for more information",
            result->executable_name
        );
        goto error;
    }

    return;
error:
    exit(EXIT_FAILURE);
}
