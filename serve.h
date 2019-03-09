#ifndef SERVE_H
#define SERVE_H

#include <netinet/in.h>
#include <pthread.h> 

enum serve_result {
    MEMORY_ERROR = 1,
    SOCKET_ERROR,
    BIND_ERROR,
    LISTEN_ERROR,
    SYSCONF_ERROR,
};

typedef struct serve_server {
    struct sockaddr_in name;
    int sockfd; // should be ready for accept()

    pthread_t *_workers;
    void (*_handler)(int sockfd);
} serve_server;

int serve_server_multithreaded_accept(serve_server *server);
void *_serve_server_accept_worker(const serve_server *server);

/*
 * Takes ip address for binding and callback for handling request.
 * Callback has an interface: void (int clientfd).
 */
int listen_and_serve(unsigned int addr, 
                     unsigned short port, 
                     void (*handler)(int));

#endif // SERVE_H
