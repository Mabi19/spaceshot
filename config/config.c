#include "config.h"
#include "parse.h"
#include <memory.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// This file uses #embed, which ccache doesn't support. Therefore:
// ccache:disable

static Config config = {0};

Config *config_get() { return &config; }

static const char *get_home_directory() {
    static char *result = NULL;
    // if already computed, return it
    if (result) {
        return result;
    }
    // $HOME overrides /etc/passwd
    result = getenv("HOME");
    if (result) {
        return result;
    }
    result = getpwuid(getuid())->pw_dir;
    return result;
}

/** Returns a malloc'd string */
static char *config_dir_to_file(const char *dir) {
    const char *SUFFIX = "/spaceshot/config.ini";
    char *result = malloc(strlen(dir) + strlen(SUFFIX) + 1);
    strcpy(result, dir);
    strcat(result, SUFFIX);
    return result;
}

/**
 * Obtain all of the configuration directories, ordered from least to most
 * important (so they should be loaded in the order they're returned).
 */
const char **config_get_locations() {
    static const char **result = NULL;
    if (result) {
        return result;
    }

    const char *xdg_dirs = getenv("XDG_CONFIG_DIRS");
    if (!xdg_dirs || xdg_dirs[0] == '\0')
        xdg_dirs = "/etc/xdg";

    // each : ends the current path and starts a new one
    // no colons = one path
    int path_count = 1;
    for (const char *c = xdg_dirs; *c != 0; c++) {
        if (*c == ':') {
            path_count++;
        }
    }

    // one member for the file in $XDG_CONFIG_HOME, one member for the
    // terminating NULL
    result = calloc(path_count + 2, sizeof(const char *));
    int i = 0;
    // read from back to front, so the most important directory is read last
    char *check_dirs = strdup(xdg_dirs);
    char *current = check_dirs + strlen(check_dirs);
    while (current >= check_dirs) {
        if (*current == ':') {
            result[i] = config_dir_to_file(current + 1);
            *current = '\0';
            i++;
        }
        current--;
    }
    result[i] = config_dir_to_file(check_dirs);
    i++;

    const char *config_home = getenv("XDG_CONFIG_HOME");
    if (config_home && config_home[0] != '\0') {
        result[i] = config_dir_to_file(config_home);
        i++;
    } else {
        const char *home_dir = get_home_directory();
        char full_path[strlen(home_dir) + strlen("/.config") + 1];
        strcpy(full_path, home_dir);
        strcat(full_path, "/.config");
        result[i] = config_dir_to_file(full_path);
        i++;
    }

    return result;
}

static char DEFAULT_CONFIG[] = {
#embed "defaults.ini"
    , '\0'
};

extern void config_parse_entry(
    void *data, const char *section, const char *key, char *value
);

bool config_load_file(const char *path) {
    FILE *config_file = fopen(path, "rb");
    if (!config_file) {
        return false;
    }
    fseek(config_file, 0, SEEK_END);
    size_t data_len = ftell(config_file);
    char data[data_len + 1];
    fseek(config_file, 0, SEEK_SET);
    fread(data, 1, data_len, config_file);
    fclose(config_file);

    config_parse_string(data, config_parse_entry, &config);
    return true;
}

void config_load() {
    config_parse_string(DEFAULT_CONFIG, config_parse_entry, &config);

    const char *const CONFIG_SUBPATH = "/spaceshot/config.ini";
    const int CONFIG_SUBPATH_LENGTH = strlen(CONFIG_SUBPATH);

    const char **config_dirs = config_get_locations();
    for (int i = 0; config_dirs[i] != NULL; i++) {
        const char *directory = config_dirs[i];
        char path_buf[strlen(directory) + CONFIG_SUBPATH_LENGTH + 1];
        strcpy(path_buf, directory);
        strcat(path_buf, CONFIG_SUBPATH);

        config_load_file(path_buf);
    }
}
