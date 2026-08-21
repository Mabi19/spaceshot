#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct LinkBufferBlock {
    struct LinkBufferBlock *next;
    size_t used_size;
    uint8_t data[];
} LinkBufferBlock;

/**
 * The size to pass in to link_buffer_new if the link buffer is to be used as a
 * growable arena.
 */
constexpr size_t LINK_BUFFER_ARENA_SIZE = 1024 - sizeof(LinkBufferBlock);

/**
 * The size to pass in to link_buffer_new if the link buffer is to be used to
 * store streaming data.
 */
constexpr size_t LINK_BUFFER_DATA_SIZE = 65536 - sizeof(LinkBufferBlock);

/**
 * A growable buffer implemented as a linked list of blocks.
 * Useful as a growable arena.
 * Also used to store PNG data as it comes in.
 */
typedef struct LinkBuffer {
    // The first block.
    LinkBufferBlock *first;
    // The last *used* block.
    // This is where new appends and allocations go.
    // If any blocks are linked after it, they are empty.
    LinkBufferBlock *last;
    size_t block_size;
} LinkBuffer;

LinkBuffer *link_buffer_new(size_t block_size);

/**
 * Append the data contiguously to the end of the buffer.
 */
void link_buffer_append(LinkBuffer *buffer, const void *data, size_t length);
/**
 * Allocate some (aligned) space on the link buffer.
 * The allocation + alignment padding must fit in the link buffer's block size.
 */
void *link_buffer_alloc(LinkBuffer *buffer, size_t size, size_t align);
/**
 * Reset the link buffer without deallocating the blocks.
 * In effect, all the data inside it is cleared, and the link buffer can be
 * reused afterwards.
 */
void link_buffer_reset(LinkBuffer *buffer);

/**
 * Write the contents of the link buffer to a file descriptor.
 */
void link_buffer_write(LinkBuffer *buffer, FILE *out);

void link_buffer_destroy(LinkBuffer *buffer);
