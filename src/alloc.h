#pragma once
#include <stdlib.h>

// Thin wrappers around the memory allocation functions
// which check for success.

/**
 * A thin checking wrapper around malloc.
 * Allocate size bytes of memory.
 */
void *cmalloc(size_t size);
/**
 * A thin checking wrapper around calloc.
 * Allocate nmemb elements of size bytes each
 * and initialize them to 0.
 */
void *ccalloc(size_t nmemb, size_t size);
/**
 * A thin checking wrapper around realloc.
 * Resize a block of data, making it size bytes long.
 */
void *crealloc(void *data, size_t size);
/**
 * A thin checking wrapper around strdup.
 * Duplicate a string, returning an identical malloc'd buffer.
 */
char *cstrdup(const char *str);
