#pragma once

#include <stdint.h>
#include <wayland-client.h>

typedef struct {
    int fd;
    uint32_t width, height, stride;
    uint8_t *data;
    enum wl_shm_format format;
    struct wl_buffer *wl_buffer;
} SharedBuffer;

SharedBuffer *shared_buffer_new(
    uint32_t width, uint32_t height, uint32_t stride, enum wl_shm_format format
);
void shared_buffer_destroy(SharedBuffer *buffer);
