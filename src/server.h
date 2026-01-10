#pragma once

#include <stddef.h>
#include <stdint.h>

// #define DEBUG

#define NUM_WORKERS 8   // number of worker threads
#define BACKLOG     128 // server connection backlog, see: man 'listen(2)'
#define MAX_JOBS    32  // max number of waiting accepted connections

struct worker;

// request handler, called from a worker thread.
typedef int (*handler_function) (struct worker *, const char *data);

// send a message over the connection.
int send_msg(struct worker *, const char* format, ...);

// perform any actions during server cleanup.
typedef void (*cleanup_function) (void);

// Create a server that binds to [port] and listen for requests with
// NUM_WORKERS worker threads, responding to requests with [handler].
//
// The server sets up a signal handler for SIGINT (Ctrl-c) to free all
// resources and calls [cleanup] (if not NULL) before exiting.
//
// Only one can be run server per process.
int listen_and_serve(uint16_t port, handler_function, cleanup_function);
