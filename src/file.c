#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void file_open(const char *path, void **file, size_t *file_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        *file = NULL;
        *file_size = 0;
        return;
    }

    struct stat *file_stats = file_get_info(path);
    if (file_stats == NULL) {
        close(fd);
        *file = NULL;
        *file_size = 0;
        return;
    }
    size_t size = (size_t)file_stats->st_size;
    free(file_stats);

    void *mapped_file = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapped_file == MAP_FAILED) {
        *file = NULL;
        *file_size = 0;
        return;
    }

    *file = mapped_file;
    *file_size = size;
}

void file_close(void *file, size_t file_size) {
    munmap(file, file_size);
}

struct stat *file_get_info(const char *path) {
    struct stat *file_stats = malloc(sizeof(struct stat));
    if (file_stats == NULL) {
        return NULL;
    }
    if (stat(path, file_stats) < 0) {
        free(file_stats);
        return NULL;
    }

    return file_stats;
}
