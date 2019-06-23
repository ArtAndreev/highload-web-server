#include "config.h"
#include "serve.h"

#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage() {
    fprintf(stderr, "Usage:\n./server -c /path/to/config\n");
}

int main(int argc, char **argv) {
    int opt;
    char *cfg_location = NULL;
    while ((opt = getopt(argc, argv, "c:")) != -1)
    {
        if (opt == 'c') {
            if ((cfg_location = strdup(optarg)) == NULL) {
                fprintf(stderr, "Cannot init config path string: %s\n", strerror(errno));
                return 1;
            }
        } else {
            print_usage();
            return 1;
        }
    }
    if (cfg_location == NULL) {
        print_usage();
        return 1;
    }

    serve_config *cfg = parse_serve_config(cfg_location);
    if (cfg == NULL) {
        return 1;
    }
    cfg->addr = INADDR_ANY;

    int r = listen_and_serve_http(cfg);
    free(cfg);
    return r;
}
