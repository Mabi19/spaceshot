#pragma once

typedef struct {
    char *output_file;
    bool is_verbose;
} Config;

void load_config();
Config *get_config();
