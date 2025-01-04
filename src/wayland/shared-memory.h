#pragma once

#include <stdint.h>
#include <stdlib.h>

int create_shm_fd(size_t size);
// This function just calls close(), but it's useful because you don't have to
// include it yourself
void destroy_shm_fd(int fd);
