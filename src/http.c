#include "http.h"

#include "file.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

#define INITIAL_HEADERS_BUF_SIZE 1024

const char *http_end_of_request = "\r\n\r\n";
const char *crlf = "\r\n";

static const char *default_directory_file = "index.html";

static const char *status_200_ok = "200 OK";
static const char *status_400_bad_request = "400 Bad Request";
static const char *status_403_forbidden = "403 Forbidden";
static const char *status_404_not_found = "404 Not_Found";
static const char *status_405_method_not_allowed = "405 Method Not Allowed";
static const char *status_505_http_version_not_supported = "505 HTTP Version Not Supported";

static const char *header_server = "Server";
static const char *header_date = "Date";
static const char *header_connection_close = "Connection: close";

static const char *header_content_length = "Content-Length";
static const char *header_content_type = "Content-Type";

static const char *mime_type_html = "text/html";
static const char *mime_type_css = "text/css";
static const char *mime_type_js = "application/javascript";
static const char *mime_type_jpeg = "image/jpeg";
static const char *mime_type_png = "image/png";
static const char *mime_type_gif = "image/gif";
static const char *mime_type_swf = "application/x-shockwave-flash";

typedef struct http_request {
    char *http_method;
    char *http_version;
    char *path;
    char *query;

    char *host;
} http_request;

static http_request *http_parse(const buffer *raw_request);
static void http_request_free(http_request *request);

static int respond_with_bad_request(const http_request *request, http_response *response);
static int respond_with_unsupported_http_version(http_response *response);
static int respond_with_method_not_allowed(const http_request *request, http_response *response);

static int process_request(const http_request *request, http_response *response, const char *static_root);

http_response *http_handler(const buffer *raw_request, const char *static_root) {
    if (raw_request == NULL) {
        fprintf(stderr, "http: got empty raw request\n");
        return NULL;
    }
    if (static_root == NULL) {
        fprintf(stderr, "http: got empty static root");
        return NULL;
    }

    http_request *request = http_parse(raw_request);
    if (request == NULL) {
        fprintf(stderr, "http: got empty parsed request\n");
        return NULL;
    }

    http_response *response = http_response_new();
    if (response == NULL) {
        fprintf(stderr, "http: cannot allocate response\n");
        http_request_free(request);
        return NULL;
    }

    do {
        if (request->http_method == NULL || request->path == NULL || request->http_version == NULL) {
            if ((respond_with_bad_request(request, response)) < 0) {
                fprintf(stderr, "http: error respond with bad request\n");
                http_request_free(request);
                http_response_free(response);
                return NULL;
            }
            break;
        }

        if (strncmp(request->http_version, "HTTP/1.1", strlen(request->http_version)) != 0 && // todo: check host?
            strncmp(request->http_version, "HTTP/1.0", strlen(request->http_version)) != 0) {
            if ((respond_with_unsupported_http_version(response)) < 0) {
                fprintf(stderr, "http: error respond with bad protocol version\n");
                http_request_free(request);
                http_response_free(response);
                return NULL;
            }
            break;
        }

        if (strncmp(request->http_method, "GET", strlen(request->http_method)) == 0 ||
            strncmp(request->http_method, "HEAD", strlen(request->http_method)) == 0) {
            if ((process_request(request, response, static_root)) < 0) {
                fprintf(stderr, "http: processing method %s error\n", request->http_method);
                http_request_free(request);
                http_response_free(response);
                return NULL;
            }
            break;
        }

        if ((respond_with_method_not_allowed(request, response)) < 0) {
            fprintf(stderr, "http: error respond with method not allowed\n");
            http_request_free(request);
            http_response_free(response);
            return NULL;
        }
    } while (0);

    http_request_free(request);
    return response;
}


static int write_start_string(http_response *response, const char *version, const char *status) {
    if ((buffer_append_string_dynamically(&response->headers, version)) < 0) return -1;
    if ((buffer_append_string_dynamically(&response->headers, " ")) < 0) return -1;
    if ((buffer_append_string_dynamically(&response->headers, status)) < 0) return -1;

    return buffer_append_string_dynamically(&response->headers, crlf);
}

static int write_server_and_date(http_response *response) {
    if ((buffer_append_string_dynamically(&response->headers, header_server)) < 0) return -1;
    if ((buffer_append_string_dynamically(&response->headers, ": v1.0\r\n")) < 0) return -1;
    if ((buffer_append_string_dynamically(&response->headers, header_date)) < 0) return -1;
    time_t timer = time(NULL);
    char *now = malloc(27);
    sprintf(now, ": %.24s", ctime((&timer)));
    if ((buffer_append_string_dynamically(&response->headers, now)) < 0) {
        free(now);
        return -1;
    }
    free(now);

    return buffer_append_string_dynamically(&response->headers, crlf);
}

static int write_headers_beginning(http_response *response, const char *version, const char *status) {
    if ((write_start_string(response, version, status) < 0)) return -1;
    if ((write_server_and_date(response)) < 0) return -1;

    return buffer_append_string_dynamically(&response->headers, header_connection_close);
}

static int respond_with_unsupported_http_version(http_response *response) {
    if ((write_headers_beginning(response, "HTTP/1.1", status_505_http_version_not_supported)) < 0) return -1;

    return buffer_append_string_dynamically(&response->headers, http_end_of_request);
}

static int respond_with_bad_request(const http_request *request, http_response *response) {
    if ((write_headers_beginning(response, request->http_version == NULL ? "HTTP/1.1" : request->http_version,
        status_400_bad_request)) < 0) return -1;

    return buffer_append_string_dynamically(&response->headers, http_end_of_request);
}

static int respond_with_forbidden(const http_request *request, http_response *response) {
    if ((write_headers_beginning(response, request->http_version, status_403_forbidden)) < 0) return -1;

    return buffer_append_string_dynamically(&response->headers, http_end_of_request);
}

static int respond_with_not_found(const http_request *request, http_response *response) {
    if ((write_headers_beginning(response, request->http_version, status_404_not_found)) < 0) return -1;

    return buffer_append_string_dynamically(&response->headers, http_end_of_request);
}

static int respond_with_method_not_allowed(const http_request *request, http_response *response) {
    if ((write_headers_beginning(response, request->http_version, status_405_method_not_allowed)) < 0) return -1;

    return buffer_append_string_dynamically(&response->headers, http_end_of_request);
}

static int respond_ok(const http_request *request, http_response *response, const char *file_path, size_t file_len) {
    if ((write_headers_beginning(response, request->http_version, status_200_ok)) < 0) return -1;
    if ((buffer_append_string_dynamically(&response->headers, crlf)) < 0) return -1;
    if ((buffer_append_string_dynamically(&response->headers, header_content_length)) < 0) return -1;

    char string_file_len[32];
    snprintf(string_file_len, sizeof(string_file_len), ": %zu", file_len);

    if ((buffer_append_string_dynamically(&response->headers, string_file_len)) < 0) return -1;

    char *ext = strrchr(file_path, '.');
    if (ext != NULL) {
        do {
            if ((strncmp(ext, ".html", strlen(ext))) == 0) {
                if ((buffer_append_string_dynamically(&response->headers, crlf)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, header_content_type)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, ": ")) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, mime_type_html)) < 0) return -1;
                break;
            }
            if ((strncmp(ext, ".css", strlen(ext))) == 0) {
                if ((buffer_append_string_dynamically(&response->headers, crlf)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, header_content_type)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, ": ")) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, mime_type_css)) < 0) return -1;
                break;
            }
            if ((strncmp(ext, ".js", strlen(ext))) == 0) {
                if ((buffer_append_string_dynamically(&response->headers, crlf)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, header_content_type)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, ": ")) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, mime_type_js)) < 0) return -1;
                break;
            }
            if ((strncmp(ext, ".jpg", strlen(ext))) == 0 || (strncmp(ext, ".jpeg", strlen(ext))) == 0) {
                if ((buffer_append_string_dynamically(&response->headers, crlf)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, header_content_type)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, ": ")) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, mime_type_jpeg)) < 0) return -1;
                break;
            }
            if ((strncmp(ext, ".png", strlen(ext))) == 0) {
                if ((buffer_append_string_dynamically(&response->headers, crlf)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, header_content_type)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, ": ")) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, mime_type_png)) < 0) return -1;
                break;
            }
            if ((strncmp(ext, ".gif", strlen(ext))) == 0) {
                if ((buffer_append_string_dynamically(&response->headers, crlf)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, header_content_type)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, ": ")) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, mime_type_gif)) < 0) return -1;
                break;
            }
            if ((strncmp(ext, ".swf", strlen(ext))) == 0) {
                if ((buffer_append_string_dynamically(&response->headers, crlf)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, header_content_type)) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, ": ")) < 0) return -1;
                if ((buffer_append_string_dynamically(&response->headers, mime_type_swf)) < 0) return -1;
            }
        } while (0);
    }

    return buffer_append_string_dynamically(&response->headers, http_end_of_request);
}

static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a')
                a -= 'a'-'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a'-'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

static char *clean_and_get_full_path(const char *static_root, const char *path) {
    char *file_path = malloc(strlen(path));
    if (file_path == NULL) {
        return NULL;
    }

    url_decode(file_path, path);

    // check directory
    size_t file_path_len = strlen(file_path);
    if (file_path[file_path_len - 1] == '/') {
        // append index.html
        size_t default_len = strlen(default_directory_file);
        char *tmp = realloc(file_path, file_path_len + default_len + 1);
        if (tmp == NULL) {
            free(file_path);
            return NULL;
        }
        file_path = tmp;

        memcpy(file_path + file_path_len, default_directory_file, default_len);
        file_path[file_path_len + default_len] = '\0';
    }

    // concat root and path
    size_t full_path_len = strlen(static_root) + strlen(file_path) + 1;
    char *full_path = malloc(full_path_len);
    if (full_path == NULL) {
        free(file_path);
        return NULL;
    }
    snprintf(full_path, full_path_len, "%s%s", static_root, file_path);
    free(file_path);

    return full_path;
}

static int process_request(const http_request *request, http_response *response, const char *static_root) {
    if (strstr(request->path, "/../")) { // document root escaping forbidden
        if ((respond_with_forbidden(request, response)) < 0) {
            return -1;
        }
    }

    char *full_path = clean_and_get_full_path(static_root, request->path);
    size_t file_len;
    if ((strncmp(request->http_method, "GET", strlen(request->http_method))) == 0) {
        void *file = (void *) response->body;
        file_open(full_path, &file, &file_len);
        if (file == NULL) {
            switch (errno) {
                case ENOENT: // file doesn't exist
                case EINVAL:
                    if ((respond_with_not_found(request, response)) < 0) {
                        free(full_path);
                        return -1;
                    }
                    break;
                default:
                    free(full_path);
                    return -1;
            }
            free(full_path);
            return 0;
        }

        response->body = file;
        response->body_len = file_len;
    } else {
        // HEAD
        struct stat *file_stats = file_get_info(full_path);
        if (file_stats == NULL) {
            switch (errno) {
                case ENOENT: // file doesn't exist
                case EINVAL:
                    if ((respond_with_not_found(request, response)) < 0) {
                        free(full_path);
                        return -1;
                    }
                    break;
                default:
                    free(full_path);
                    return -1;
            }
            free(full_path);
            return 0;
        }
        file_len = file_stats->st_size;
        free(file_stats);
    }

    int r = respond_ok(request, response, full_path, file_len);
    free(full_path);
    return r;
}

//
// http_response
//

http_response *http_response_new() {
    http_response *response = calloc(1, sizeof(http_response));
    if (response == NULL) {
        return NULL;
    }
    if ((response->headers = buffer_new(INITIAL_HEADERS_BUF_SIZE)) == NULL) {
        free(response);
        return NULL;
    }

    return response;
}

void http_response_free(http_response *resp) {
    if (resp == NULL) {
        return;
    }

    buffer_free(resp->headers);
    if (resp->body != NULL) {
        file_close(resp->body, resp->body_len);
    }
}

//
// http_request (internal)
//

static http_request *http_request_new() {
    return calloc(1, sizeof(http_request));
}

static void http_request_free(http_request *req) {
    if (req == NULL) {
        return;
    }

    free(req->http_method);
    free(req->http_version);
    free(req->path);
    free(req->query);
    free(req->host);
    free(req);
}

//
// parse
//

static http_request *http_parse(const buffer *raw_request) {
    http_request *request = http_request_new();
    if (request == NULL) {
        return NULL;
    }

    size_t lines_cap = 4;
    size_t lines_len = 0;
    char **lines = malloc(lines_cap * sizeof(char *));
    if (lines == NULL) {
        free(request);
        return NULL;
    }

    // split raw request into lines
    char *cur_line = raw_request->data; // start at beginning
    size_t remaining_length = raw_request->len;
    size_t word_len;
    size_t sep_len = strlen(crlf);
    char *cur_sep_position;
    while ((cur_sep_position = memmem(cur_line, remaining_length, crlf, sep_len)) != NULL) {
        word_len = cur_sep_position - cur_line;
        if (word_len == 0) { // that's end (because it's substring between crlf and crlf
            // we don't care if request starts with crlf
            break;
        }

        if (lines_len == lines_cap) {
            char **tmp = realloc(lines, lines_cap * sizeof(char *) * 2);
            if (tmp == NULL) {
                http_request_free(request);
                for (size_t j = 0; j < lines_len; j++) {
                    free(lines[j]);
                }
                free(lines);
                return NULL;
            }
            lines = tmp;
            lines_cap *= 2;
        }
        lines[lines_len] = malloc(word_len + 1);
        memcpy(lines[lines_len], cur_line, word_len);
        lines[lines_len][word_len] = '\0'; // terminate string
        lines_len++;

        cur_line = cur_sep_position + sep_len;
        if (cur_line >= (raw_request->data + raw_request->len)) {
            // end of string, this should never happen, if string has crlf-crlf ending
            fprintf(stderr, "Got strange raw_request: `%.*s\n`", (int)raw_request->len, raw_request->data);
            break;
        }
        remaining_length -= (word_len + sep_len);
    }

    if (lines_len == 0) { // oops, there are no headers
        http_request_free(request);
        for (size_t j = 0; j < lines_len; j++) {
            free(lines[j]);
        }
        free(lines);
        return NULL;
    }

    // first line should be METHOD /PATH HTTP/VERSION
    char *query;
    for (size_t i = 0; (cur_line = strsep(&lines[0], " ")) != NULL; i++) {
        switch (i) {
            case 0: // method
                if ((request->http_method = strdup(cur_line)) == NULL) {
                    http_request_free(request);
                    for (size_t j = 0; j < lines_len; j++) {
                        free(lines[j]);
                    }
                    free(lines);
                    return NULL;
                }
                break;
            case 1: // path
                if ((query = strstr(cur_line, "?")) != NULL) {
                    *query = '\0';
                    if ((request->query = strdup(++query)) == NULL) {
                        http_request_free(request);
                        for (size_t j = 0; j < lines_len; j++) {
                            free(lines[j]);
                        }
                        free(lines);
                    }
                }
                if ((request->path = strdup(cur_line)) == NULL) {
                    http_request_free(request);
                    for (size_t j = 0; j < lines_len; j++) {
                        free(lines[j]);
                    }
                    free(lines);
                }
                break;
            case 2: // http version
                if ((request->http_version = strdup(cur_line)) == NULL) {
                    http_request_free(request);
                    for (size_t j = 0; j < lines_len; j++) {
                        free(lines[j]);
                    }
                    free(lines);
                    return NULL;
                }
                break;
            default: // unexpected tokens, expected 3
                http_request_free(request);
                for (size_t j = 0; j < lines_len; j++) {
                    free(lines[j]);
                }
                free(lines);
                return NULL;
        }
    }

    for (size_t i = 1; i < lines_len; i++) {
        // todo: parse headers
    }

    for (size_t i = 0; i < lines_len; i++) {
        free(lines[i]);
    }
    free(lines);

    return request;
}
