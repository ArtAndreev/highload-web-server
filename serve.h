#ifndef SERVE_H
#define SERVE_H

enum serve_result {
    SOCKET_ERROR = 1,
    BIND_ERROR,
    LISTEN_ERROR,
};

/*
 * Takes ip address for binding and callback for handling request.
 * Callback has an interface: void (int clientfd).
 */
int listen_and_serve(unsigned int addr, 
                     unsigned short port, 
                     void (*handler)(int));

#endif // SERVE_H
