#include "log.h"
#include <config/config.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *program_name = NULL;

void set_program_name(const char *name) { program_name = name; }

static void print_stderr_valist(const char *format, va_list args) {
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
    print_stderr_valist(format, args);
    va_end(args);
}

void report_error_fatal(const char *format, ...) {
    va_list args;
    va_start(args);
    print_stderr_valist(format, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

void report_warning(const char *format, ...) {
    const char *PREFIX = "warning: ";
    char wrapped_format[strlen(PREFIX) + strlen(format) + 1];
    strcpy(wrapped_format, PREFIX);
    strcat(wrapped_format, format);

    va_list args;
    va_start(args);
    print_stderr_valist(wrapped_format, args);
    va_end(args);
}

void log_debug(const char *format, ...) {
    if (!get_config()->verbose) {
        return;
    }
    va_list args;
    va_start(args);
    vfprintf(stderr, format, args);
    va_end(args);
}

#ifdef SPACESHOT_TIMING

static int timespec_subtract(
    struct timespec *result, struct timespec *x, struct timespec *y
) {
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_nsec < y->tv_nsec) {
        int nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
        y->tv_nsec -= 1000000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_nsec - y->tv_nsec > 1000000000) {
        int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
        y->tv_nsec += 1000000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
       tv_nsec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_nsec = x->tv_nsec - y->tv_nsec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

void timing_display(
    const char *name, struct timespec *start, struct timespec *end
) {
    struct timespec ts_diff;
    timespec_subtract(&ts_diff, end, start);
    uint64_t us = ts_diff.tv_sec * 1'000'000 + ts_diff.tv_nsec / 1000;
    if (us >= 1000) {
        fprintf(stderr, "%s took %ldms\n", name, us / 1000);
    } else {
        fprintf(stderr, "%s took %ldµs\n", name, us);
    }
}

#endif
