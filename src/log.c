#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static const char *program_name = NULL;

void set_program_name(const char *name) { program_name = name; }

static void report_error_valist(const char *format, va_list args) {
    va_list args_for_print;
    va_copy(args_for_print, args);
    // determine length of formatted text
    size_t formatted_length = vsnprintf(NULL, 0, format, args);
    // va_end is not called for args because that's supposed to be done by the
    // caller

    char formatted_text_buf[formatted_length + 1];
    vsnprintf(formatted_text_buf, formatted_length + 1, format, args_for_print);
    // but this is our va_list, so close it ourselves
    va_end(args_for_print);

    fprintf(stderr, "%s: %s\n", program_name, formatted_text_buf);
}

void report_error(const char *format, ...) {
    va_list args;
    va_start(args);
    report_error_valist(format, args);
    va_end(args);
}

void report_error_fatal(const char *format, ...) {
    va_list args;
    va_start(args);
    report_error_valist(format, args);
    va_end(args);
    exit(EXIT_FAILURE);
}
