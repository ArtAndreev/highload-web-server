#ifndef SERVE_H
#define SERVE_H

#include "config.h"

enum serve_result {
    SERVE_MEMORY_ERROR = 1,
    SERVE_PTHREAD_ERROR,
    SERVE_SOCKET_ERROR,
    SERVE_BIND_ERROR,
    SERVE_LISTEN_ERROR,
    SERVE_SYSCONF_ERROR,
    SERVE_LIBEVENT_ERROR,
};

int listen_and_serve_http(const serve_config *cfg);

#endif // SERVE_H
