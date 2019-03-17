#include "serve.h"

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/event-config.h>
#include <event2/thread.h>

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_QUEUE_LEN 65535

int _serve_worker_init_worker_pool(_serve_worker *pool, int size);

int listen_and_serve(unsigned int addr, unsigned short port, void (*handler)(char *, char *)) {
    assert(handler != NULL);

    event_enable_debug_mode();
    evthread_use_pthreads();

    serve_server server = { ._handler = handler };
    server.sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server.sockfd < 0) {
        perror("Socket error");
        return SERVE_SOCKET_ERROR;
    }

    server.name = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(addr),
    };
    if (bind(server.sockfd, (struct sockaddr *)&server.name, sizeof(struct sockaddr_in)) < 0) {
        perror("Bind error");
        return SERVE_BIND_ERROR;
    }

    if (listen(server.sockfd, MAX_QUEUE_LEN) < 0) {
        perror("Listen error");
        return SERVE_LISTEN_ERROR;
    }

    long num_CPU = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_CPU < 0) {
        perror("Cannot get number of CPU");
        return SERVE_SYSCONF_ERROR;
    }

    server._workers = calloc(num_CPU, sizeof(_serve_worker));
    if (server._workers == NULL) {
        perror("Malloc error");
        return SERVE_MEMORY_ERROR;
    }
    int result_code = _serve_worker_init_worker_pool(server._workers, num_CPU);
    if (result_code != 0) {
        free(server._workers);
        server._workers = NULL;
        return result_code;
    }
    server.worker_num = num_CPU;
    printf("Initialized %d workers\n", server.worker_num);

    result_code = serve_server_accept(&server);
    close(server.sockfd);
    printf("Server stopped");

    return result_code;
}

void _serve_worker_read_cb(struct bufferevent *bev, void *ctx);
void _serve_worker_write_cb(struct bufferevent *bev, void *ctx);
void _serve_worker_event_cb(struct bufferevent *bev, short events, void *ctx);

int serve_server_accept(serve_server *server) {
    assert(server != NULL);
    assert(server->_workers != NULL);

    printf("Accepting connections at %s:%hu\n", 
        inet_ntoa(server->name.sin_addr), 
        ntohs(server->name.sin_port));

    int i = 0; // round-robin
    while (1) {
        struct sockaddr_in client;
        unsigned int addrlen = sizeof(struct sockaddr_in);
        evutil_socket_t clientfd = accept(server->sockfd, (struct sockaddr *)&client, &addrlen);
        if (clientfd < 0) {
            perror("Accept error");
            continue;
        }
        printf("Accepted client: %s:%hu\n", inet_ntoa(client.sin_addr), client.sin_port);

        if (evutil_make_socket_nonblocking(clientfd) != 0) {
            perror("Cannot make socket nonblocking");
            close(clientfd);
            continue;
        }

        struct bufferevent *client_ev = bufferevent_socket_new(server->_workers[i]._worker_ev_base, 
            clientfd, BEV_OPT_CLOSE_ON_FREE);
        if (client_ev == NULL) {
            perror("Accepting: event new error");
            close(clientfd);
            continue;
        }
        bufferevent_setcb(client_ev, _serve_worker_read_cb, _serve_worker_write_cb, 
            _serve_worker_event_cb, NULL);
        struct timeval io_timeout = { 60, 0 }; // 1 minute
        if (bufferevent_set_timeouts(client_ev, &io_timeout, &io_timeout) < 0) {
            perror("Accepting: event set timeouts error");
            bufferevent_free(client_ev);
            continue;
        }
        if (bufferevent_enable(client_ev, EV_READ|EV_WRITE) < 0) {
            perror("Accepting: cannot enable client event");
            bufferevent_free(client_ev);
            continue;
        }

        i = (i + 1) % server->worker_num; // round-robin: next worker
    }
}

void *_serve_worker_process(struct event_base *_worker_ev_base);

int _serve_worker_init_worker_pool(_serve_worker *pool, int size) {
    assert(pool != NULL);

    if (size < 1) {
        size = 1;
    }
    for (int i = 0; i < size; i++) {
        pool[i]._worker_ev_base = event_base_new();
        if (pool[i]._worker_ev_base == NULL) {
            perror("Event base init error");
            for (int j = 0; j < i; j++) {
                event_base_free(pool[j]._worker_ev_base);
            }
            return SERVE_LIBEVENT_ERROR;
        }

        if (pthread_create(&pool[i]._worker, NULL, 
                (void *)_serve_worker_process, pool[i]._worker_ev_base) != 0) {
            perror("Pthread creation error");
            for (int j = 0; j < i; j++) {
                event_base_free(pool[j]._worker_ev_base);
            }
            return SERVE_PTHREAD_ERROR;
        }
    }

    return 0;
}

void *_serve_worker_process(struct event_base *_worker_ev_base) {
    assert(_worker_ev_base != NULL);

    if (event_base_loop(_worker_ev_base, EVLOOP_NO_EXIT_ON_EMPTY) != 1) {
        perror("Event base loop error");
        return (void *)SERVE_LIBEVENT_ERROR;
    }

    printf("Thread exited\n"); // EVLOOP_NO_EXIT_ON_EMPTY is set
    return (void *)0;
}

void _serve_worker_event_cb(struct bufferevent *bev, short events, void *ctx) {
    if (events & BEV_EVENT_ERROR) {
         /* An error occured while connecting. */
        perror("Client error");
    } else if (events & BEV_EVENT_TIMEOUT) {
        printf("Client timeout");
    }
    printf("Unknow error\n");
    bufferevent_free(bev);
}

void _serve_worker_read_cb(struct bufferevent *bev, void *ctx) {
    printf("readcb\n");
    struct evbuffer *buf_input = bufferevent_get_input(bev);
}

void _serve_worker_write_cb(struct bufferevent *bev, void *ctx) {
    printf("writecb\n");
    struct evbuffer *buf_output = bufferevent_get_output(bev);
}
