#include "parse.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void advance_whitespace(char **str) {
    while (isspace(**str)) {
        (*str)++;
    }
}

static void
parse_line(char **section, char *line, ConfigEntryFunc callback, void *data) {
    advance_whitespace(&line);
    if (*line == '\0' || *line == ';' || *line == '#') {
        // empty line or a comment
    } else if (*line == '[') {
        // start of a section
        char *end_bracket = strchr(line, ']');
        *end_bracket = '\0';
        // check that nothing is afterwards
        char *past_end_bracket = end_bracket + 1;
        advance_whitespace(&past_end_bracket);
        if (*past_end_bracket != '\0' && *past_end_bracket != ';' &&
            *past_end_bracket != '#') {
            // TODO: emit a warning
        }
        *section = line + 1;
    } else {
        // key-value pair
        char *equals_sign = strchr(line, '=');
        if (!equals_sign) {
            // TODO: emit a parse error
            return;
        }

        char *after_key = equals_sign - 1;
        while (after_key > line && isspace(*after_key)) {
            after_key--;
        }
        after_key++;
        *after_key = '\0';

        char *key = line;
        char *value = equals_sign + 1;
        advance_whitespace(&value);

        // handle quotes
        if (*value == '"' || *value == '\'') {
            char quote_type = *value;
            value++;
            char *other_quote = strchr(value, quote_type);
            if (!other_quote) {
                // TODO: emit a parse error
                return;
            }
            *other_quote = '\0';
            char *past_string = other_quote + 1;
            advance_whitespace(&past_string);
            if (*past_string != '\0' && *past_string == ';' &&
                *past_string == '#') {
                // TODO: emit a warning
            }
        } else {
            char *end = value;
            while (*end != '\0' && *end != ';' && *end != '#') {
                end++;
            }
            end--;
            while (isspace(*end)) {
                end--;
            }
            end++;
            *end = '\0';
        }

        callback(data, *section, key, value);
    }
}

void config_parse_string(
    char *ini_contents, ConfigEntryFunc callback, void *data
) {
    char *section = NULL;

    while (*ini_contents != '\0') {
        char *line_start = ini_contents;
        while (1) {
            char c = *ini_contents;
            if (c == '\n' || c == '\0') {
                break;
            } else {
                ini_contents++;
            }
        }
        // terminate this line and move to the next one
        *ini_contents = '\0';
        ini_contents++;
        parse_line(&section, line_start, callback, data);
    }
}
