#pragma once
#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} BBox;

/**
 * Convert slurp's string format into a BBox.
 * Note that @p out may be left in an invalid state if conversion fails.
 * @returns true if conversion was successful, false otherwise
 */
bool bbox_parse(const char *str_form, BBox *out);
/**
 * Convert a BBox into slurp's string format.
 * @returns a newly allocated string, which should be free'd.
 */
char *bbox_stringify(BBox src);

/**
 * Test whether @p inner is contained fully within @p outer.
 */
bool bbox_contains(BBox outer, BBox inner);
