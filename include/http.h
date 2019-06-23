#ifndef HTTP_H
#define HTTP_H

#include "buffer.h"

#include <unistd.h>

const char *http_end_of_request;

typedef struct http_response {
    buffer *headers;
    char *body;
    size_t body_len;
} http_response;

http_response *http_handler(const buffer *raw_request, const char *static_root);

http_response *http_response_new();
void http_response_free(http_response *resp);

#endif // HTTP_H
