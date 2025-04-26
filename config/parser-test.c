// This is testing code for the INI file parser.
// It doesn't go through meson; instead, compile it with
// `gcc parser-test.c parse.c -o ./parser-test`

#include "parse.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void handle_entry(
    void *data, const char *section, const char *key, char *value
) {
    (void)data;
    if (section) {
        printf("[%s] '%s' = '%s'\n", section, key, value);
    } else {
        printf("'%s' = '%s'\n", key, value);
    }
}

int main() {
    FILE *fi = fopen("test.ini", "r");
    fseek(fi, 0, SEEK_END);
    uint64_t bufsize = ftell(fi);
    fseek(fi, 0, SEEK_SET);

    char *buf = malloc(bufsize + 1);
    fread(buf, sizeof(char), bufsize, fi);
    buf[bufsize] = '\0';

    config_parse_string(buf, handle_entry, NULL);

    free(buf);
    fclose(fi);
}
