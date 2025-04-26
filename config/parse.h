#pragma once
typedef void (*ConfigEntryFunc)(
    void *data, const char *section, const char *key, char *value
);

/**
    Parse the @p data as an INI file, calling the @p callback for each key-value
   pair. Note that this function modifies the data in-place.
*/
void config_parse_string(
    char *ini_contents, ConfigEntryFunc callback, void *data
);
