#ifndef CONFIG_H
#define CONFIG_H

typedef struct serve_config {
    unsigned int addr;
    unsigned short port;
    int worker_num;

    char *static_root;
} serve_config;

serve_config *parse_serve_config(const char *path);

#endif // CONFIG_H
