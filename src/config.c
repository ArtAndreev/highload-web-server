#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PORT_NUMBER 65535

static const char *http_port = "port";
static const char *cpu_limit = "cpu_limit";
static const char *document_root = "document_root";

static int fill_parameter(serve_config *cfg, const char *key, const char *val);

serve_config *parse_serve_config(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Config `%s` read error: %s\n", path, strerror(errno));
        return NULL;
    }

    serve_config *cfg = calloc(1, sizeof(serve_config));
    if (cfg == NULL) {
        fprintf(stderr, "Config `%s` allocate error: %s\n", path, strerror(errno));
        fclose(file);
        return NULL;
    }

    char line[128];
    char key[128], val[128], *sep;
    size_t key_len, val_len;
    while (fgets(line, sizeof(line), file) != NULL) {
        if ((sep = strstr(line, " ")) != NULL) {
            key_len = sep - line;
            memcpy(key, line, key_len);
            key[key_len] = '\0';

            sep++;
            val_len = line + strlen(line) - sep - 1; // without \n
            memcpy(val, sep, val_len);
            val[val_len] = '\0';

            if ((fill_parameter(cfg, key, val)) < 0) {
                fclose(file);
                free(cfg);
                return NULL;
            }
        }
    }

    fclose(file);

    return cfg;
}

static int fill_parameter(serve_config *cfg, const char *key, const char *val) {
    if ((strcmp(key, http_port)) == 0) {
        long port = strtol(val, NULL, 10);
        if (port < 0 || port > MAX_PORT_NUMBER) {
            fprintf(stderr, "Wrong port value: %s\n", val);
            return -1;
        }
        cfg->port = port;
        return 0;
    }

    if ((strcmp(key, cpu_limit)) == 0) {
        cfg->worker_num = strtol(val, NULL, 10);
        return 0;
    }

    if ((strcmp(key, document_root)) == 0) {
        if ((cfg->static_root = strdup(val)) == NULL) {
            fprintf(stderr, "Cannot initialize static_root: %s\n", strerror(errno));
            return -1;
        }
        return 0;
    }

    fprintf(stderr, "Unknown key: %s, ignoring it\n", val);
    return 0;
}