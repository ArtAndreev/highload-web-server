#include "serve.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PORT_NUMBER 65535

void print_usage() {
    fprintf(stderr, "Usage:\n./server port\nport: [0 - %u]", MAX_PORT_NUMBER);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        print_usage();
        return 1;
    }
    long port = strtol(argv[1], NULL, 10);
    if (port < 0 || port > MAX_PORT_NUMBER) {
        fprintf(stderr, "Error: wrong port: %s\n", argv[1]);
        print_usage();
        return 1;
    }

    serve_config cfg = {
        .addr = INADDR_ANY,
        .port = (unsigned short)port,
        .worker_num = 0,

        .static_root = "/var/www/html",
    };
    return listen_and_serve_http(&cfg);
}
