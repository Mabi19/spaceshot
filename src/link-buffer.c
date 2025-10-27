#include "link-buffer.h"
#include "log.h"
#include <errno.h>
#include <string.h>

LinkBuffer *link_buffer_new() {
    LinkBuffer *result = malloc(sizeof(LinkBuffer));
    result->used_size = 0;
    result->next = NULL;
    if (!result) {
        report_error_fatal("couldn't allocate buffer");
    }
    return result;
}

void link_buffer_append(LinkBuffer **block, void *data, size_t length) {
    LinkBuffer *cur_block = *block;
    if (cur_block->used_size + length <= LINK_BUFFER_SIZE) {
        memcpy(cur_block->data + cur_block->used_size, data, length);
        cur_block->used_size += length;
    } else {
        LinkBuffer *new_block = link_buffer_new();
        memcpy(new_block->data, data, length);
        new_block->used_size = length;
        cur_block->next = new_block;
        *block = new_block;
    }
}

void link_buffer_write(LinkBuffer *buffer, FILE *out) {
    while (buffer != NULL) {
        size_t items_written = fwrite(buffer->data, buffer->used_size, 1, out);
        if (items_written != 1) {
            if (errno && errno != EPIPE) {
                perror("clipboard transfer failed");
            }
            return;
        }
        buffer = buffer->next;
    }
}

void link_buffer_destroy(LinkBuffer *buffer) {
    while (buffer != NULL) {
        LinkBuffer *to_free = buffer;
        buffer = buffer->next;
        free(to_free);
    }
}
