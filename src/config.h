#pragma once

typedef struct {
    char *output_file;
    bool is_verbose;
    int png_compression_level;
    bool move_to_background;
} Config;

void load_config();
Config *get_config();
