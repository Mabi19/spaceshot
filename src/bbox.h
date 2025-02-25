#pragma once
#include <stdint.h>

/**
 * A bounding box.
 * Defined with doubles because a device pixel can be a fractional amount of
 * logical pixels. Functions treat this struct immutably.
 */
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

/**
 * Translate a BBox by the specified @p dx and @p dy.
 */
BBox bbox_translate(const BBox *src, double dx, double dy);

/**
 * Scale a BBox by the specified @p factor, with the origin at (0, 0).
 */
BBox bbox_scale(const BBox *src, double factor);

/**
 * Round a BBox to the nearest integer coordinates.
 * This snaps to the top-left corner of the grid cell.
 */
BBox bbox_round(const BBox *src);
