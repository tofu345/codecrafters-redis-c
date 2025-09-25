#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "server.h"
#include "parser.h"
#include "utils.h"

struct {
    int fds[NUM_WORKERS]; // sockets to connections
    size_t len;
} jobs = {};

// used to sleep / wakeup worker threads
pthread_cond_t new_jobs_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t jobs_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int id;
    handler_func *handler;
} thread_data;

int send_msg(const int fd, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char* msg = NULL;
    if (vasprintf(&msg, format, args) == -1) die("vasprintf");
    va_end(args);

    size_t msglen = strlen(msg);
    size_t nsend = send(fd, msg, msglen, 0);
    free(msg);
    if (nsend == -1) {
        printf("send on fd %d failed: %s\n", fd, strerror(errno));
        return -1;
    }
    return 0;
}

static void*
worker(void* arg) {
    thread_data* td = arg;
    size_t nrecv, nsend, buflen = 100;
    char buf[buflen] = {}; // TODO: dynamic buf
    resp data;

    while(1) {
        pthread_mutex_lock(&jobs_mutex);
        if (jobs.len <= 0) {
            // wait until `new_jobs_cond` is signaled
            pthread_cond_wait(&new_jobs_cond, &jobs_mutex);
        }

        int fd = jobs.fds[jobs.len - 1];
        jobs.len--;
        pthread_mutex_unlock(&jobs_mutex);

        printf("worker %d: handling connection (fd: %d)\n", td->id, fd);

        do {
            nrecv = recv(fd, buf, buflen, 0);
            if (nrecv == -1) {
                printf("worker %d: read from connection (fd: %d) failed: %s\n",
                        td->id, fd, strerror(errno));
                break;

            } else if (nrecv == 0) {
                printf("worker %d: recieved 0 bytes, closing connection\n", td->id);
                break;

            } else
                printf("worker %d: recieved %zd bytes\n", td->id, nrecv);

            // TODO: logging or smthn
            td->handler(fd, buf, buflen);
        } while (1);

        close(fd);
    }
}

int listen_and_serve(uint16_t port, handler_func *handler) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        die("socket creation failed:");
    }

    // ensures no 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        die("setsockopt SO_REUSEADDR failed:");
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET ,
        .sin_port = htons(port),
        .sin_addr = { htonl(INADDR_ANY) },
    };
    if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
        die("bind port %d failed:");
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) == -1) {
        die("listen on port %d failed:");
    }
    printf("listening on port: %d\n", port);

    pthread_t threads[NUM_WORKERS];
    thread_data* tds[NUM_WORKERS];
    for (size_t i = 0; i < NUM_WORKERS; i++) {
        tds[i] = malloc(sizeof(thread_data));
        if (tds[i] == NULL) die("malloc");
        tds[i]->handler = handler;
        tds[i]->id = (int)i;
        pthread_create(threads+i, NULL, &worker, (void*)tds[i]);
    }

    while (1) {
        int conn_fd = accept(server_fd, NULL, NULL);
        if (conn_fd == -1) {
            printf("accept connection failed: %s\n", strerror(errno));
            continue;
        }
        printf("recieved connection on (fd: %d)\n", conn_fd);

        pthread_mutex_lock(&jobs_mutex);
        if (jobs.len >= NUM_WORKERS) {
            printf("too many conncurrent requests, rejecting conn (fd: %d)",
                    conn_fd);
            pthread_mutex_unlock(&jobs_mutex);
            continue;
        }

        jobs.fds[jobs.len] = conn_fd;
        jobs.len++;
        pthread_cond_signal(&new_jobs_cond);
        pthread_mutex_unlock(&jobs_mutex);
    }

    // why not?
    for (uintptr_t i = 0; i < NUM_WORKERS; i++) {
        free(tds[i]);
    }
    close(server_fd);
    return 0;
}
