#ifndef SERVE_H
#define SERVE_H

#include <event2/event.h>

#include <netinet/in.h>
#include <pthread.h> 

enum serve_result {
    SERVE_MEMORY_ERROR = 1,
    SERVE_PTHREAD_ERROR,
    SERVE_SOCKET_ERROR,
    SERVE_BIND_ERROR,
    SERVE_LISTEN_ERROR,
    SERVE_SYSCONF_ERROR,
    SERVE_LIBEVENT_ERROR,
};

typedef struct _serve_worker {
    pthread_t _worker;
    struct event_base *_worker_ev_base;
} _serve_worker;

typedef struct serve_server {
    struct sockaddr_in name;
    int sockfd; // should be ready for accept()

    _serve_worker *_workers;
    int worker_num;
    void (*_handler)(char *response, char *request);
} serve_server;

int serve_server_accept(serve_server *server);

/*
 * Takes ip address for binding and callback for handling request.
 * Callback has an interface: void (int clientfd).
 */
int listen_and_serve(unsigned int addr, 
                     unsigned short port, 
                     void (*handler)(char *, char *));

#endif // SERVE_H
