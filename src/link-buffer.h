#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

constexpr size_t LINK_BUFFER_SIZE = 65536 - sizeof(void *) - sizeof(size_t);

/**
 * A growable buffer implemented as a linked list of blocks.
 * Used to store PNG data as it comes in.
 */
typedef struct LinkBuffer {
    struct LinkBuffer *next;
    size_t used_size;
    uint8_t data[LINK_BUFFER_SIZE];
} LinkBuffer;

LinkBuffer *link_buffer_new();
void link_buffer_append(LinkBuffer **block, void *data, size_t length);
/**
 * Write the contents of the link buffer to a file descriptor.
 */
void link_buffer_write(LinkBuffer *buffer, FILE *out);

void link_buffer_destroy(LinkBuffer *buffer);
