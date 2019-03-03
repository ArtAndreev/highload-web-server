#include "serve.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_QUEUE_LEN 1024

int listen_and_serve(unsigned int addr, unsigned short port, void (*handler)(int)) {
    int sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        perror("Socket error");
        return SOCKET_ERROR;
    }

    struct sockaddr_in name = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(addr),
    };
    if (bind(sockfd, (struct sockaddr *)&name, sizeof(struct sockaddr_in)) < 0) {
        perror("Bind error");
        return BIND_ERROR;
    }

    if (listen(sockfd, MAX_QUEUE_LEN) < 0) {
        perror("Listen error");
        return LISTEN_ERROR;
    }

    struct sockaddr_in client;
    unsigned int addrlen = sizeof(struct sockaddr_in);
    while (1) {
        int clientfd = accept(sockfd, (struct sockaddr *)&client, &addrlen);
        if (clientfd < 0) {
            perror("Accept error");
            continue;
        }
        printf("Accepted client: %s:%u\n", inet_ntoa(client.sin_addr), client.sin_port);

        handler(clientfd);

        close(clientfd);
    }
}
