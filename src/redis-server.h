#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H

#include <stdint.h>

#include "ht.h"

#define NUM_WORKERS 5

typedef struct {
    int fd;
    uint16_t port;
    ht* store;
} server;

// create `server` and bind to `port`
//
// on error: returns NULL with `errno` set
server* server_create(uint16_t port);

// close `Server.fd` and free
void server_destroy(server* s);

int server_listen(server* s);

#endif
