#pragma once
#include <spaceshot-config-struct-decl.h> // IWYU pragma: export
#include <stdint.h>

Config *config_get();
const char **config_get_locations();

/**
 * Load the file at @p path as configuration.
 * @returns whether the file exists
 */
bool config_load_file(const char *path);
void config_load();

/**
 * Convert a ConfigLength to device pixels, keeping in mind surface scale.
 * The value is rounded to the nearest pixel.
 */
int config_length_to_pixels(ConfigLength length, uint32_t surface_scale);
