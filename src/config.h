#pragma once

typedef struct {
    char *output_file;
    bool is_verbose;
    int png_compression_level;
} Config;

void load_config();
Config *get_config();
