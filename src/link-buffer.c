#include "link-buffer.h"
#include "log.h"
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

LinkBuffer *link_buffer_new(size_t block_size) {
    LinkBuffer *result = malloc(sizeof(LinkBuffer));
    if (!result) {
        report_error_fatal("couldn't allocate buffer");
    }
    result->block_size = block_size;

    result->first = malloc(sizeof(LinkBufferBlock) + block_size);
    if (!result->first) {
        report_error_fatal(
            "couldn't allocate buffer block of size %zu", block_size
        );
    }
    result->first->next = NULL;
    result->first->used_size = 0;

    result->last = result->first;

    return result;
}

// Switch to the next block once the last block is filled up.
static void next_block(LinkBuffer *buffer) {
    if (buffer->last->next) {
        // There's a block leftover from before the reset.
        buffer->last = buffer->last->next;
    } else {
        LinkBufferBlock *new_block =
            malloc(sizeof(LinkBufferBlock) + buffer->block_size);
        if (!new_block) {
            report_error_fatal(
                "couldn't allocate buffer block of size %zu", buffer->block_size
            );
        }
        buffer->last->next = new_block;
        new_block->next = NULL;
        new_block->used_size = 0;
    }
}

void link_buffer_append(
    LinkBuffer *buffer, const void *raw_data, size_t length
) {
    LinkBufferBlock *cur_block = buffer->last;
    const uint8_t *data = raw_data;

    while (cur_block->used_size + length > buffer->block_size) {
        size_t remaining_space = buffer->block_size - cur_block->used_size;
        memcpy(cur_block->data + cur_block->used_size, data, remaining_space);
        cur_block->used_size = buffer->block_size;
        data += remaining_space;
        length -= remaining_space;

        next_block(buffer);
        cur_block = buffer->last;
    }
    memcpy(cur_block->data + cur_block->used_size, data, length);
    cur_block->used_size += length;
}

static inline uint8_t *align_pointer(uint8_t *base, size_t align) {
    if (align == 0) {
        return base;
    }

    size_t offset = (uintptr_t)base % align;
    if (offset == 0) {
        return base;
    } else {
        return base + (align - offset);
    }
}

void *link_buffer_alloc(LinkBuffer *buffer, size_t size, size_t align) {
    size_t max_padding = align > 1 ? align - 1 : 0;
    assert(size + max_padding <= buffer->block_size);

    uint8_t *result =
        align_pointer(buffer->last->data + buffer->last->used_size, align);
    ptrdiff_t space = (buffer->last->data + buffer->block_size) - result;
    if (space < (ptrdiff_t)size) {
        // Not enough space in the last block, add in a new block.
        next_block(buffer);
        // buffer->last is now guaranteed to be empty
        result = align_pointer(buffer->last->data, align);
    }
    buffer->last->used_size = (result - buffer->last->data) + size;
    return result;
}

void link_buffer_reset(LinkBuffer *buffer) {
    LinkBufferBlock *block = buffer->first;
    while (block != NULL) {
        block->used_size = 0;
        block = block->next;
    }
    buffer->last = buffer->first;
}

void link_buffer_write(LinkBuffer *buffer, FILE *out) {
    // TODO: use writev(2)
    LinkBufferBlock *block = buffer->first;
    while (block != NULL && block->used_size > 0) {
        size_t items_written = fwrite(block->data, block->used_size, 1, out);
        if (items_written != 1) {
            if (errno && errno != EPIPE) {
                perror("data transfer failed");
            }
            return;
        }
        block = block->next;
    }
}

void link_buffer_destroy(LinkBuffer *buffer) {
    LinkBufferBlock *block = buffer->first;
    while (block != NULL) {
        LinkBufferBlock *to_free = block;
        block = block->next;
        free(to_free);
    }
    free(buffer);
}
