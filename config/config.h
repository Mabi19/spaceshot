#pragma once
#include <spaceshot-config-struct-decl.h>

Config *get_config();

/**
 * Load the file at @p path as configuration.
 * @returns whether the file exists
 */
bool load_config_file(const char *path);
void load_config();
