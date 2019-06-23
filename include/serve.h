#ifndef SERVE_H
#define SERVE_H

enum serve_result {
    SERVE_MEMORY_ERROR = 1,
    SERVE_PTHREAD_ERROR,
    SERVE_SOCKET_ERROR,
    SERVE_BIND_ERROR,
    SERVE_LISTEN_ERROR,
    SERVE_SYSCONF_ERROR,
    SERVE_LIBEVENT_ERROR,
};

typedef struct serve_config {
    unsigned int addr;
    unsigned short port;
    int worker_num;

    char *static_root;
} serve_config;

int listen_and_serve_http(const serve_config *cfg);

#endif // SERVE_H
