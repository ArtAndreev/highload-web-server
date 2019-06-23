#include "serve.h"

#include "buffer.h"
#include "http.h"

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/event-config.h>
#include <event2/thread.h>

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_QUEUE_LEN 65535
#define CLIENT_IO_TIMEOUT 60 // 1 minute

#define CHUNK_SIZE 4096
#define MAX_REQUEST_BODY_SIZE 4096 // 4 kb

typedef struct worker {
    pthread_t worker_thread;
    struct event_base *worker_ev_base;
} worker;

typedef struct server {
    struct sockaddr_in name;
    int sockfd; // should be ready for accept()

    serve_config *cfg;
    worker *workers;
} server;

typedef struct client_ctx {
    struct sockaddr_in address;
    char *cfg_static_root;

    buffer *read_buf;

    http_response *response;
    size_t wrote_bytes_to_socket;
} client_ctx;

static int server_accept(const server *server);
static int init_worker_pool(worker *pool, int size);

int listen_and_serve_http(const serve_config *cfg) {
    assert(cfg != NULL);
    assert(cfg->static_root != NULL);

    evthread_use_pthreads();

    server server;
    if ((server.cfg = malloc(sizeof(serve_config))) == NULL) {
        perror("Malloc error");
        return SERVE_MEMORY_ERROR;
    }
    memcpy(server.cfg, cfg, sizeof(serve_config));
    if ((server.cfg->static_root = strdup(cfg->static_root)) == NULL) {
        perror("Strdup cfg->static_root error");
        free(server.cfg);
        return SERVE_MEMORY_ERROR;
    }

    if ((server.sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Socket error");
        free(server.cfg->static_root);
        free(server.cfg);
        return SERVE_SOCKET_ERROR;
    }

    server.name = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(server.cfg->port),
        .sin_addr.s_addr = htonl(server.cfg->addr),
    };
    if (bind(server.sockfd, (struct sockaddr *)&server.name, sizeof(struct sockaddr_in)) < 0) {
        perror("Bind error");
        free(server.cfg->static_root);
        free(server.cfg);
        return SERVE_BIND_ERROR;
    }

    if (listen(server.sockfd, MAX_QUEUE_LEN) < 0) {
        perror("Listen error");
        free(server.cfg->static_root);
        free(server.cfg);
        return SERVE_LISTEN_ERROR;
    }

    if (server.cfg->worker_num <= 0) {
        if ((server.cfg->worker_num = sysconf(_SC_NPROCESSORS_ONLN)) < 0) {
            perror("Cannot get number of CPU");
            free(server.cfg->static_root);
            free(server.cfg);
            return SERVE_SYSCONF_ERROR;
        }
    }

    if ((server.workers = calloc(server.cfg->worker_num, sizeof(worker))) == NULL) {
        perror("Malloc error");
        free(server.cfg->static_root);
        free(server.cfg);
        return SERVE_MEMORY_ERROR;
    }
    int r;
    if ((r = init_worker_pool(server.workers, server.cfg->worker_num)) != 0) {
        free(server.workers);
        free(server.cfg->static_root);
        free(server.cfg);
        return r;
    }
    printf("Initialized %d workers\n", server.cfg->worker_num);

    r = server_accept(&server);

    for (int i = 0; i < server.cfg->worker_num; i++) {
        event_base_free(server.workers[i].worker_ev_base);
    }
    free(server.workers);
    free(server.cfg->static_root);
    free(server.cfg);
    close(server.sockfd);
    printf("Server stopped\n");
    return r;
}

static void worker_read_cb(struct bufferevent *bev, void *ctx);
static void worker_write_cb(struct bufferevent *bev, void *ctx);
static void worker_event_cb(struct bufferevent *bev, short events, void *ctx);

static client_ctx *new_client_ctx(const struct sockaddr_in *inet_data, const char *static_root);
static void free_client_ctx(client_ctx *ctx);

int server_accept(const server *server) {
    assert(server != NULL);
    assert(server->workers != NULL);

    printf("Accepting connections at %s:%hu\n", 
        inet_ntoa(server->name.sin_addr), 
        ntohs(server->name.sin_port));

    int i = 0; // round-robin
    while (1) {
        struct sockaddr_in client;
        unsigned int addrlen = sizeof(struct sockaddr_in);
        int clientfd = accept(server->sockfd, (struct sockaddr *)&client, &addrlen);
        if (clientfd < 0) {
            perror("Accept error");
            continue;
        }
        printf("Accepted client: %s:%hu\n", inet_ntoa(client.sin_addr), client.sin_port);

        client_ctx *client_data = new_client_ctx(&client, server->cfg->static_root);
        if (client_data == NULL) {
            fprintf(stderr, "Memory error: client struct malloc error: %s; dropping client %s:%hu\n",
                    strerror(errno), inet_ntoa(client.sin_addr), client.sin_port);
            close(clientfd);
            continue;
        }

        if (evutil_make_socket_nonblocking(clientfd) != 0) {
            fprintf(stderr, "Cannot make socket nonblocking: %s; dropping client %s:%hu\n",
                    strerror(errno), inet_ntoa(client.sin_addr), client.sin_port);
            free_client_ctx(client_data);
            close(clientfd);
            continue;
        }

        struct bufferevent *client_ev = bufferevent_socket_new(server->workers[i].worker_ev_base,
            clientfd, BEV_OPT_CLOSE_ON_FREE); // close client socket when freeing the bufferevent
        if (client_ev == NULL) {
            fprintf(stderr, "Accepting: event new error: %s; dropping client %s:%hu\n",
                    strerror(errno), inet_ntoa(client.sin_addr), client.sin_port);
            free_client_ctx(client_data);
            close(clientfd);
            continue;
        }
        bufferevent_setcb(client_ev, worker_read_cb, worker_write_cb, worker_event_cb, client_data);
        static const struct timeval io_timeout = { CLIENT_IO_TIMEOUT, 0 };
        if (bufferevent_set_timeouts(client_ev, &io_timeout, &io_timeout) < 0) {
            fprintf(stderr, "Accepting: event set timeouts error: %s; dropping client %s:%hu\n",
                    strerror(errno), inet_ntoa(client.sin_addr), client.sin_port);
            free_client_ctx(client_data);
            bufferevent_free(client_ev);
            continue;
        }
        if (bufferevent_enable(client_ev, EV_READ/*|EV_WRITE*/) < 0) {
            fprintf(stderr, "Accepting: cannot enable client event (read): %s; dropping client %s:%hu\n",
                    strerror(errno), inet_ntoa(client.sin_addr), client.sin_port);
            free_client_ctx(client_data);
            bufferevent_free(client_ev);
            continue;
        }

        i = (i + 1) % server->cfg->worker_num; // round-robin: next worker
    }
}

static void *worker_process(struct event_base *_worker_ev_base);

static int init_worker_pool(worker *pool, int size) {
    assert(pool != NULL);

    if (size < 1) {
        size = 1;
    }
    for (int i = 0; i < size; i++) {
        pool[i].worker_ev_base = event_base_new();
        if (pool[i].worker_ev_base == NULL) {
            perror("Event base init error");
            for (int j = 0; j < i; j++) {
                event_base_free(pool[j].worker_ev_base);
            }
            return SERVE_LIBEVENT_ERROR;
        }

        if (pthread_create(&pool[i].worker_thread, NULL,
                (void *)worker_process, pool[i].worker_ev_base) != 0) {
            perror("Pthread creation error");
            for (int j = 0; j < i; j++) {
                event_base_free(pool[j].worker_ev_base);
            }
            return SERVE_PTHREAD_ERROR;
        }
    }

    return 0;
}

//
// worker
//

static void *worker_process(struct event_base *_worker_ev_base) {
    assert(_worker_ev_base != NULL);

    if (event_base_loop(_worker_ev_base, EVLOOP_NO_EXIT_ON_EMPTY) != 1) {
        perror("Event base loop error");
        return (void *)SERVE_LIBEVENT_ERROR;
    }
    // EVLOOP_NO_EXIT_ON_EMPTY is set, endless event loop

    return (void *)0;
}

static void worker_event_cb(struct bufferevent *bev, short events, void *ctx) {
    client_ctx *client = (client_ctx *)ctx;

    if (events & BEV_EVENT_ERROR) {
        fprintf(stderr, "Error %s: dropping client %s:%hu\n",
            strerror(errno), inet_ntoa(client->address.sin_addr), client->address.sin_port);
    } else if (events & BEV_EVENT_TIMEOUT) {
        fprintf(stderr, "Client timeout: dropping client %s:%hu\n",
            inet_ntoa(client->address.sin_addr), client->address.sin_port);
    } else if (events & BEV_EVENT_EOF) {
        fprintf(stderr, "Client disconnected: dropping client %s:%hu\n",
            inet_ntoa(client->address.sin_addr), client->address.sin_port);
    } else if (events & (BEV_EVENT_READING | BEV_EVENT_WRITING)) {
        fprintf(stderr, "Error while read/write process: dropping client %s:%hu\n",
            inet_ntoa(client->address.sin_addr), client->address.sin_port);
    } else {
        fprintf(stderr, "Unknown error %d: dropping client %s:%hu\n", events,
            inet_ntoa(client->address.sin_addr), client->address.sin_port);
    }
    bufferevent_free(bev);
    free_client_ctx(ctx);
}

static void worker_read_cb(struct bufferevent *bev, void *ctx) {
    client_ctx *client = (client_ctx *)ctx;

    char tmp[CHUNK_SIZE];
    size_t n;
    while ((n = bufferevent_read(bev, tmp, CHUNK_SIZE)) > 0) {
        if (buffer_append(client->read_buf, tmp, n) < 0) {
            // overflow of remaining space
            fprintf(stderr, "Too big request body: %s:%hu\n", inet_ntoa(client->address.sin_addr), client->address.sin_port);
            bufferevent_free(bev);
            free_client_ctx(client);
            return;
        }
    }

    if (memmem(client->read_buf->data, client->read_buf->len, http_end_of_request, strlen(http_end_of_request)) != NULL) {
        // fwrite(client->read_buf->data, 1, client->read_buf->len, stdout);
        if ((client->response = http_handler(client->read_buf, client->cfg_static_root)) == NULL) {
            fprintf(stderr, "Processing: cannot process http request (write): %s; dropping client %s:%hu\n",
                    strerror(errno), inet_ntoa(client->address.sin_addr), client->address.sin_port);
            bufferevent_free(bev);
            free_client_ctx(client);
            return;
        }
        buffer_clear(client->read_buf); // we always close connection, so we don't care about data after \r\n\r\n
        if (bufferevent_enable(bev, EV_WRITE) < 0) {
            fprintf(stderr, "Processing: cannot enable client event (write): %s; dropping client %s:%hu\n",
                    strerror(errno), inet_ntoa(client->address.sin_addr), client->address.sin_port);
            bufferevent_free(bev);
            free_client_ctx(client);
        }
    }
}

static void worker_write_cb(struct bufferevent *bev, void *ctx) {
    client_ctx *client = (client_ctx *)ctx;

    size_t headers_len = client->response->headers->len;
    size_t body_len = client->response->body_len;
    size_t wrote = client->wrote_bytes_to_socket;
    // printf("headers %zu, body %zu, wrote %zu\n", headers_len, body_len, wrote);
    if (wrote == (headers_len + body_len) /* end of write */) {
        if (body_len != 0) {
            bufferevent_write(bev, http_end_of_request, strlen(http_end_of_request));
        }
        printf("End of write, closing connection %s:%hu\n", inet_ntoa(client->address.sin_addr), client->address.sin_port);
        bufferevent_free(bev);
        free_client_ctx(client);
        return;
    }

    size_t chunk_size, left_to_write;
    if (wrote < headers_len) {
        // write headers
        left_to_write = headers_len - wrote;
        chunk_size = left_to_write < CHUNK_SIZE ? left_to_write : CHUNK_SIZE;
        bufferevent_write(bev, client->response->headers->data + wrote, chunk_size);
        client->wrote_bytes_to_socket += chunk_size;
    } else {
        // headers are written, write body
        size_t body_wrote = wrote - headers_len;
        left_to_write = body_len - body_wrote;
        chunk_size = left_to_write < CHUNK_SIZE ? left_to_write : CHUNK_SIZE;
        bufferevent_write(bev, client->response->body + body_wrote, chunk_size);
        client->wrote_bytes_to_socket += chunk_size;
    }
}

//
// client
//

static client_ctx *new_client_ctx(const struct sockaddr_in *inet_data, const char *static_root) {
    client_ctx *ctx = calloc(1, sizeof(client_ctx));
    if (ctx == NULL) {
        return NULL;
    }
    memcpy(&ctx->address, inet_data, sizeof(struct sockaddr_in));

    if ((ctx->read_buf = buffer_new(MAX_REQUEST_BODY_SIZE)) == NULL) {
        free(ctx);
        return NULL;
    }

    if ((ctx->cfg_static_root = strdup(static_root)) == NULL) {
        buffer_free(ctx->read_buf);
        free(ctx);
        return NULL;
    }

    return ctx;
}

static void free_client_ctx(client_ctx *ctx) {
    if (ctx == NULL) {
        return;
    }

    free(ctx->cfg_static_root);
    buffer_free(ctx->read_buf);
    http_response_free(ctx->response);
    free(ctx);
}
