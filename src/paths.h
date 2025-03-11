#pragma once

const char *get_pictures_directory();
/**
 * Create an output filename. Note that this function returns a newly-allocated
 * string that must be free'd.
 */
char *get_output_filename();
