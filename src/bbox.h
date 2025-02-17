#pragma once
#include <stdint.h>

typedef struct {
    double x;
    double y;
    double width;
    double height;
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
char *bbox_stringify(const BBox *src);

/**
 * Test whether @p inner is contained fully within @p outer.
 */
bool bbox_contains(const BBox *outer, const BBox *inner);
