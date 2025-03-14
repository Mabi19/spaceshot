#include "log.h"
#define _POSIX_C_SOURCE 200112L
#include "shared-memory.h"
#include "wayland/globals.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

// The contents of this file are adapted from wayland-book.com

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int make_shm_file() {
    int retries = 100;
    do {
        char name[] = "/spaceshot_wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

SharedBuffer *shared_buffer_new(
    uint32_t width, uint32_t height, uint32_t stride, enum wl_shm_format format
) {
    SharedBuffer *result = calloc(1, sizeof(SharedBuffer));
    result->fd = make_shm_file();
    result->width = width;
    result->height = height;
    result->stride = stride;
    result->format = format;
    uint32_t size = height * stride;

    if (result->fd < 0) {
        goto error;
    }

    // truncate the file to the new size
    int ret;
    do {
        ret = ftruncate(result->fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret != 0) {
        goto error;
    }

    // re-map
    result->data =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, result->fd, 0);
    if (result->data == MAP_FAILED) {
        goto error;
    }

    struct wl_shm_pool *pool =
        wl_shm_create_pool(wayland_globals.shm, result->fd, size);
    log_debug("format: %x\n", result->format);
    result->wl_buffer = wl_shm_pool_create_buffer(
        pool, 0, result->width, result->height, result->stride, result->format
    );
    wl_shm_pool_destroy(pool);

    return result;
error:
    shared_buffer_destroy(result);
    return NULL;
}

void shared_buffer_destroy(SharedBuffer *buffer) {
    if (buffer->data && buffer->data != MAP_FAILED) {
        munmap(buffer->data, buffer->height * buffer->stride);
    }

    if (buffer->fd != 0) {
        close(buffer->fd);
    }

    if (buffer->wl_buffer) {
        wl_buffer_destroy(buffer->wl_buffer);
    }

    free(buffer);
}
