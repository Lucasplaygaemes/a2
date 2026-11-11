#ifndef A2_IO_UTILS_H
#define A2_IO_UTILS_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

static inline bool a2_write_all(int fd, const void *buffer, size_t length) {
    const unsigned char *next = buffer;

    while (length > 0) {
        ssize_t written = write(fd, next, length);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) {
            errno = EIO;
            return false;
        }

        next += (size_t)written;
        length -= (size_t)written;
    }

    return true;
}

#endif