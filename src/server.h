#pragma once

#include <stddef.h>
#include <stdint.h>

#define NUM_WORKERS 8   // number of worker threads
#define BACKLOG     128 // server connection backlog, see: man 'listen(2)'
#define MAX_JOBS    32

// do not call close on [conn_fd] as it is managed by the server.
typedef int handler_func(const int conn_fd, const char *data);

typedef void cleanup_func(void);

// wrapper around [send]
int send_msg(const int conn_fd, const char* format, ...);

// Create a server that binds to [port] and listen for requests with
// NUM_WORKERS threads, responding to requests with [handler].
//
// The server sets up a signal handler for SIGINT (Ctrl-c) to free all
// resources and calls [cleanup_func] (if not NULL) before exiting.
//
// Only one can be run server per process.
int listen_and_serve(uint16_t port, handler_func *, cleanup_func *);
