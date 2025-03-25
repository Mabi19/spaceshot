#include "config.h"
#include "log.h"
#include "paths.h"
#include <iniparser.h>
#include <memory.h>
#include <stdlib.h>

static Config config;

#define CONFIG_STRING(config_loc, key)                                         \
    do {                                                                       \
        const char *value;                                                     \
        if ((value = iniparser_getstring(config_dict, key, NULL))) {           \
            free(config_loc);                                                  \
            config_loc = strdup(value);                                        \
        }                                                                      \
    } while (false)

#define CONFIG_BOOL(config_loc, key)                                           \
    do {                                                                       \
        int value;                                                             \
        if ((value = iniparser_getboolean(config_dict, key, -1) != -1)) {      \
            config_loc = value;                                                \
        }                                                                      \
    } while (false)

void config_string(
    dictionary *config_dict, char **config_loc, const char *key
) {
    const char *value = iniparser_getstring(config_dict, key, NULL);
    if (value) {
        free(*config_loc);
        *config_loc = strdup(value);
    }
}

void config_bool(dictionary *config_dict, bool *config_loc, const char *key) {
    const char *value = iniparser_getstring(config_dict, key, NULL);
    if (!value)
        return;

    if (strcmp(value, "true") == 0) {
        *config_loc = true;
    } else if (strcmp(value, "false") == 0) {
        *config_loc = false;
    } else {
        report_warning("config: invalid boolean %s", value);
    }
}

void load_config() {
    // TODO: make more config options, actually make the get_config_locations
    // function

    TIMING_START(config_load);

    // default config; strings must be malloc'd because overriding them frees
    // their previous versions
    config = (Config){
        .output_file = strdup("~~/%Y-%m-%d-%H%M%S-spaceshot.png"),
        .is_verbose = false,
    };

    const char *const CONFIG_SUBPATH = "/spaceshot/config.ini";
    const int CONFIG_SUBPATH_LENGTH = strlen(CONFIG_SUBPATH);

    const char **config_dirs = get_config_locations();
    for (int i = 0; config_dirs[i] != NULL; i++) {
        const char *directory = config_dirs[i];
        char path_buf[strlen(directory) + CONFIG_SUBPATH_LENGTH + 1];
        strcpy(path_buf, directory);
        strcat(path_buf, CONFIG_SUBPATH);

        FILE *config_file = fopen(path_buf, "r");
        if (!config_file) {
            continue;
        }

        dictionary *d = iniparser_load_file(config_file, path_buf);
        config_string(d, &config.output_file, "output-file");
        config_bool(d, &config.is_verbose, "verbose");

        iniparser_freedict(d);
    }

    TIMING_END(config_load);
}

Config *get_config() { return &config; }
