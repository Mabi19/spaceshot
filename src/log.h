#pragma once

// This header contains functions similar to the glibc `error` extension.

/** This function needs to be called before any error-reporting functions. */
void set_program_name(const char *name);

/** Report a generic error message, including the program name. */
[[gnu::format(printf, 1, 2)]]
void report_error(const char *format, ...);

/**
 * Report a generic error message (as with repor_error), and exit with a non-0
 * status code.
 */
[[gnu::format(printf, 1, 2)]] [[noreturn]] void
report_error_fatal(const char *format, ...);

#define XSTR(x) STR(x)
#define STR(x) #x

/**
 * Report an error message, including the file and line number, and exit with a
 * non-0 status code.
 */
#define REPORT_ERROR_INTERNAL(format, ...)                                     \
    report_error_fatal(                                                        \
        "internal error: " format "\nat " __FILE__                             \
        ":" XSTR(__LINE__) " (in function %s)",                                \
        __VA_ARGS__,                                                           \
        __func__                                                               \
    )

/** Report that a value, like an enum entry, was unhandled.  */
#define REPORT_UNHANDLED(description, printf_specifier, value)                 \
    REPORT_ERROR_INTERNAL(                                                     \
        "unhandled " description " " printf_specifier, (value)                 \
    )
