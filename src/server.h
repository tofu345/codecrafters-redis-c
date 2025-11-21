#pragma once

#include <stddef.h>
#include <stdint.h>

#define INITIAL_WORKERS 8
#define MAX_JOBS 128

// do not call close on [conn_fd] as it is managed by the server.
// should return 0 on success.
typedef int handler_func(const int conn_fd, char *data, size_t length);

// if provided, runs when the server recieves SIGINT
typedef void cleanup_func(void);

// wrapper around [send]
int send_msg(const int conn_fd, const char* format, ...);

// bind to [port], listen for requests with [NUM_WORKERS] worker threads and
// respond to requests with [handler].
//
// only one can be run server per process
int listen_and_serve(uint16_t port, handler_func *handler, cleanup_func *);
