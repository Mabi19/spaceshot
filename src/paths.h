#pragma once

const char *get_pictures_directory();
/**
 * Obtain all of the configuration directories, ordered from least to most
 * important (so they should be loaded in the order they're returned).
 */
const char **get_config_locations();

/**
 * Create an output filename. Note that this function returns a newly-allocated
 * string that must be free'd.
 */
char *get_output_filename();
