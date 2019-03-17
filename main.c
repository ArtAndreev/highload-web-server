#include "http.h"
#include "serve.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

#define INADDR INADDR_ANY
#define MAX_PORT_NUMBER 65535

void print_usage() {
    fprintf(stderr, "Usage:\n./server port\nport: [0 - %u]", MAX_PORT_NUMBER);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage();
        return 1;
    }
    int port = atoi(argv[1]);
    if (port < 0 || port > MAX_PORT_NUMBER) {
        fprintf(stderr, "Error: wrong port\n");
        print_usage();
        return 1;
    }

    return listen_and_serve(INADDR, port, http_handler);
}
