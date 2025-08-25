#ifndef REDIS_H
#define REDIS_H

#include <stdint.h>

#define NUM_WORKERS 5

typedef struct {
    int fd;
    uint16_t port;
} Server;

// create `Server` and bind to `port`
// 
// on error: returns NULL with `errno` set
Server* server_create(uint16_t port);

// close `Server.fd` and free
void server_destroy(Server* s);

int server_listen(Server* s);

#endif
