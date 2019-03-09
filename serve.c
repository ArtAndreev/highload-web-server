#include "serve.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_QUEUE_LEN 1024

int serve_server_multithreaded_accept(serve_server *server) {
    long num_CPU = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_CPU < 0) {
        perror("Cannot get number of CPU");
        return SYSCONF_ERROR;
    }

    server->_workers = malloc(num_CPU * sizeof(pthread_t));
    if (server->_workers == NULL) {
        perror("Malloc error");
        return MEMORY_ERROR;
    }
    for (int i = 0; i < num_CPU; i++) {
        if (pthread_create(&server->_workers[i], NULL, (void *)_serve_server_accept_worker, server) != 0) {
            perror("Pthread creation error");
            return PTHREAD_ERROR;
        }
    }

    printf("Accepting connections at %s:%hu\n", inet_ntoa(server->name.sin_addr), ntohs(server->name.sin_port));
    for (int i = 0; i < num_CPU; i++) {
        if (pthread_join(server->_workers[i], NULL) != 0) {
            perror("Pthread joining error");
            return PTHREAD_ERROR;
        }
    }

    free(server->_workers);
    return 0;
}

void *_serve_server_accept_worker(const serve_server *server) {
    struct sockaddr_in client;
    unsigned int addrlen = sizeof(struct sockaddr_in);
    while (1) {
        int clientfd = accept(server->sockfd, (struct sockaddr *)&client, &addrlen);
        if (clientfd < 0) {
            perror("Accept error");
            continue;
        }
        printf("Accepted client: %s:%hu\n", inet_ntoa(client.sin_addr), client.sin_port);

        server->_handler(clientfd);

        close(clientfd);
    }
}

int listen_and_serve(unsigned int addr, unsigned short port, void (*handler)(int)) {
    serve_server server = { ._handler = handler };
    server.sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server.sockfd < 0) {
        perror("Socket error");
        return SOCKET_ERROR;
    }

    server.name = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(addr),
    };
    if (bind(server.sockfd, (struct sockaddr *)&server.name, sizeof(struct sockaddr_in)) < 0) {
        perror("Bind error");
        return BIND_ERROR;
    }

    if (listen(server.sockfd, MAX_QUEUE_LEN) < 0) {
        perror("Listen error");
        return LISTEN_ERROR;
    }

    return serve_server_multithreaded_accept(&server);
}
