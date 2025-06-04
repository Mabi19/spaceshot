#pragma once
#include <spaceshot-config-struct-decl.h> // IWYU pragma: export

Config *config_get();
const char **config_get_locations();

/**
 * Load the file at @p path as configuration.
 * @returns whether the file exists
 */
bool config_load_file(const char *path);
void config_load();
