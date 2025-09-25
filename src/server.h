#pragma once

#include <stddef.h>
#include <stdint.h>

#define NUM_WORKERS 4

// do not call close on [conn_fd] as it is managed by the server.
// should return 0 on success.
typedef int handler_func(const int conn_fd, char *data, size_t length);

// wrapper around [send] send message 
int send_msg(const int fd, const char* format, ...);

// bind to [port], listen for requests with [NUM_WORKERS] worker threads and
// respond to requests with [handler].
int listen_and_serve(uint16_t port, handler_func *handler);
