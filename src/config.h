#pragma once

typedef struct {
    char *output_file;
    bool is_verbose;
    int png_compression_level;
    bool move_to_background;
    bool should_copy_to_clipboard;
    bool should_notify;
} Config;

void load_config();
Config *get_config();
