#define _POSIX_C_SOURCE 200112L
#include "shared-memory.h"
#include "wayland/globals.h"
#include <errno.h>
#include <fcntl.h>
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

void destroy_shm_fd(int fd) { close(fd); }

SharedPool *shared_pool_new(size_t size) {
    SharedPool *result = calloc(1, sizeof(SharedPool));
    result->fd = make_shm_file();

    if (result->fd < 0) {
        free(result);
        return NULL;
    }

    if (!shared_pool_ensure_size(result, size)) {
        shared_pool_destroy(result);
        return NULL;
    }

    return result;
}

bool shared_pool_ensure_size(SharedPool *pool, size_t new_size) {
    if (pool->size >= new_size) {
        return true;
    }

    // truncate the file to the new size
    int ret;
    do {
        ret = ftruncate(pool->fd, new_size);
    } while (ret < 0 && errno == EINTR);
    if (ret != 0) {
        return false;
    }

    // re-map
    pool->size = new_size;
    pool->data = mmap(
        pool->data, pool->size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0
    );

    // this function is also called by shared_pool_new when there is no wl_pool,
    // so create one if necessary
    if (!pool->wl_pool) {
        pool->wl_pool =
            wl_shm_create_pool(wayland_globals.shm, pool->fd, pool->size);
    } else {
        wl_shm_pool_resize(pool->wl_pool, pool->size);
    }

    return true;
}

void shared_pool_destroy(SharedPool *pool) {
    munmap(pool->data, pool->size);
    close(pool->fd);
    wl_shm_pool_destroy(pool->wl_pool);
    free(pool);
}
