#ifndef FILE_H
#define FILE_H

#include <unistd.h>

void file_open(const char *path, void **file, size_t *file_size);
void file_close(void *file, size_t file_size);

struct stat *file_get_info(const char *path);

#endif // FILE_H
