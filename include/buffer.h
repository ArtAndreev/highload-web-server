#ifndef BUFFER_H
#define BUFFER_H

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct buffer {
    char *data;
    size_t len;
    size_t cap;
} buffer;

buffer *buffer_new(size_t capacity);
void buffer_free(buffer *buf);

int buffer_append(buffer *buf, const char *data, size_t len);
int buffer_append_string(buffer *buf, const char *data);
int buffer_append_dynamically(buffer **buf, const char *data, size_t len);
int buffer_append_string_dynamically(buffer **buf, const char *data);
void buffer_clear(buffer *buf);

#endif // BUFFER_H
