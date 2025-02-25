#include "bbox.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool bbox_parse(const char *str_form, BBox *out) {
    int read_char_count;
    int read_specifier_count = sscanf(
        str_form,
        "%lf,%lf %lfx%lf%n",
        &out->x,
        &out->y,
        &out->width,
        &out->height,
        &read_char_count
    );

    if (read_specifier_count != 4 || read_char_count != (int)strlen(str_form)) {
        return false;
    }
    return true;
}

static char *format_double(double input) {
    int buf_len = snprintf(NULL, 0, "%.4lf", input) + 1;
    char *output_buf = malloc(buf_len);
    snprintf(output_buf, buf_len, "%.4lf", input);

    // remove trailing zeroes
    // buf_len includes null terminator and indices are zero-indexed, so
    // subtract 2
    int i = buf_len - 2;
    while (i > 0 && output_buf[i] == '0') {
        output_buf[i] = '\0';
        i--;
    }
    // if a decimal point was left without anything after it, remove it as well
    if (output_buf[i] == '.') {
        output_buf[i] = '\0';
    }

    return output_buf;
}

char *bbox_stringify(const BBox *src) {
    char *x = format_double(src->x);
    char *y = format_double(src->y);
    char *width = format_double(src->width);
    char *height = format_double(src->height);

    int buf_len =
        strlen(x) + 1 + strlen(y) + 1 + strlen(width) + 1 + strlen(height) + 1;
    char *result = malloc(buf_len);
    snprintf(result, buf_len, "%s,%s %sx%s", x, y, width, height);
    free(x);
    free(y);
    free(width);
    free(height);
    return result;
}

bool bbox_contains(const BBox *outer, const BBox *inner) {
    int32_t outer_right = outer->x + outer->width;
    int32_t outer_bottom = outer->y + outer->height;
    int32_t inner_right = inner->x + inner->width;
    int32_t inner_bottom = inner->y + inner->height;
    return (outer->x <= inner->x) && (outer->y <= inner->y) &&
           (outer_right >= inner_right) && (outer_bottom >= inner_bottom);
}

BBox bbox_translate(const BBox *src, double dx, double dy) {
    return (BBox){
        .x = src->x + dx,
        .y = src->y + dy,
        .width = src->width,
        .height = src->height,
    };
}

BBox bbox_scale(const BBox *src, double factor) {
    return (BBox){
        .x = src->x * factor,
        .y = src->y * factor,
        .width = src->width * factor,
        .height = src->height * factor,
    };
}

BBox bbox_round(const BBox *src) {
    double right = floor(src->x + src->width);
    double bottom = floor(src->y + src->height);
    double new_x = floor(src->x);
    double new_y = floor(src->y);

    return (BBox){
        .x = new_x,
        .y = new_y,
        .width = right - new_x,
        .height = bottom - new_y,
    };
}
