#include "config.h"
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

void load_config() {
    // TODO: make more config options, actually make the get_config_locations
    // function
    // also consider loading only a single configuration file instead of
    // merging; look at what walker does?

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

        dictionary *config_dict = iniparser_load_file(config_file, path_buf);
        CONFIG_STRING(config.output_file, "output-file");
        CONFIG_BOOL(config.is_verbose, "verbose");

        iniparser_freedict(config_dict);
    }
}

Config *get_config() { return &config; }
