#include "alloc.h"
#include "log.h"
#include <string.h>

void *cmalloc(size_t size) {
    void *result = malloc(size);
    if (!result) {
        report_error_fatal("memory allocation failed");
    }
    return result;
}

void *ccalloc(size_t nmemb, size_t size) {
    void *result = calloc(nmemb, size);
    if (!result) {
        report_error_fatal("memory allocation failed");
    }
    return result;
}

void *crealloc(void *data, size_t size) {
    void *result = realloc(data, size);
    if (!result) {
        report_error_fatal("memory allocation failed");
    }
    return result;
}

char *cstrdup(const char *str) {
    char *result = strdup(str);
    if (!result) {
        report_error_fatal("memory allocation failed");
    }
    return result;
}
