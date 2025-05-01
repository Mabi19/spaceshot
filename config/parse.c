#include "parse.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// logging functions
// src/log.c will not always be available, so these are separate
static void config_print(const char *prefix, const char *format, va_list args) {
    char wrapped_format[strlen(prefix) + strlen(format) + 2];
    strcpy(wrapped_format, prefix);
    strcat(wrapped_format, format);
    strcat(wrapped_format, "\\n");

    vprintf(wrapped_format, args);
}

void config_warn(const char *format, ...) {
    va_list args;
    va_start(args);
    config_print("warning(config): ", format, args);
    va_end(args);
}

void config_error(const char *format, ...) {
    va_list args;
    va_start(args);
    config_print("error(config): ", format, args);
    va_end(args);
}

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
            config_warn("unexpected text after section definition");
        }
        *section = line + 1;
    } else {
        // key-value pair
        char *equals_sign = strchr(line, '=');
        if (!equals_sign) {
            config_error("expected = in key-value pair");
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
                config_error("missing terminating quote character");
                return;
            }
            *other_quote = '\0';
            char *past_string = other_quote + 1;
            advance_whitespace(&past_string);
            if (*past_string != '\0' && *past_string != ';' &&
                *past_string != '#') {
                config_warn("unexpected text after end of quoted value");
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
