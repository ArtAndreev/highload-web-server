#include "http.h"
#include "serve.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

#define INADDR INADDR_ANY

void print_usage() {
    printf("Usage:\n./server port\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage();
        return 1;
    }
    unsigned short port = atoi(argv[1]);

    return listen_and_serve(INADDR, port, http_handler);
}
