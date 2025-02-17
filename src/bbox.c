#include "bbox.h"
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

char *bbox_stringify(const BBox *src) {
    const char *const FORMAT = "%.4lf,%.4lf %.4lfx%.4lf";

    int buf_len =
        snprintf(NULL, 0, FORMAT, src->x, src->y, src->width, src->height) + 1;
    char *result = malloc(buf_len);
    snprintf(result, buf_len, FORMAT, src->x, src->y, src->width, src->height);
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
