#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <wayland-client.h>

typedef struct {
    int fd;
    size_t size;
    uint8_t *data;
    struct wl_shm_pool *wl_pool;
} SharedPool;

SharedPool *shared_pool_new(size_t size);
/**
 * Ensure the pool is at least `size` bytes large.
 * Note that this function will never shrink the pool.
 * @returns Whether the resize was successful.
 */
bool shared_pool_ensure_size(SharedPool *pool, size_t new_size);
void shared_pool_destroy(SharedPool *pool);
