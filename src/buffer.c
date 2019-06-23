#include "buffer.h"

buffer *buffer_new(size_t capacity) {
    buffer *buf = malloc(sizeof(buffer));
    if (buf == NULL) {
        return NULL;
    }
    buf->data = calloc(capacity, 1);
    if (buf->data == NULL) {
        free(buf);
        return NULL;
    }
    buf->cap = capacity;
    buf->len = 0;

    return buf;
}

void buffer_free(buffer *buf) {
    if (buf == NULL) {
        return;
    }

    free(buf->data);
    free(buf);
}

int buffer_append(buffer *buf, const char *data, size_t len) {
    if (buf->len + len > buf->cap) {
        // overflow
        return -1;
    }

    memcpy(buf->data + buf->len, data, len);
    buf->len += len;

    return 0;
}

int buffer_append_string(buffer *buf, const char *data) {
    if (buf->len + strlen(data) > buf->cap) {
        // overflow
        return -1;
    }

    memcpy(buf->data + buf->len, data, strlen(data));
    buf->len += strlen(data);

    return 0;
}

int buffer_append_dynamically(buffer **buf, const char *data, size_t len) {
    if ((*buf)->len + len > (*buf)->cap) {
        buffer *tmp = realloc(*buf, (*buf)->cap * 2);
        if (tmp == NULL) {
            return -1;
        }
        *buf = tmp;
        (*buf)->cap *= 2;
    }

    memcpy((*buf)->data + (*buf)->len, data, len);
    (*buf)->len += len;

    return 0;
}

int buffer_append_string_dynamically(buffer **buf, const char *data) {
    if ((*buf)->len + strlen(data) > (*buf)->cap) {
        buffer *tmp = realloc(*buf, (*buf)->cap * 2);
        if (tmp == NULL) {
            return -1;
        }
        *buf = tmp;
        (*buf)->cap *= 2;
    }

    memcpy((*buf)->data + (*buf)->len, data, strlen(data));
    (*buf)->len += strlen(data);

    return 0;
}

void buffer_clear(buffer *buf) {
    assert(buf != NULL);

    buf->len = 0;
    memset(buf->data, 0, buf->cap);
}
