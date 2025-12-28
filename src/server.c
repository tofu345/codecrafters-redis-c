#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "server.h"
#include "utils.h"

// Methodology:
//
// The main thread accept incoming requests and attempts to append them to
// [jobs], where they are popped and handled by workers.  If [jobs] is full,
// the main thread waits (sleeps) until a worker is available.

struct worker {
    const pthread_t thread;
    int id;
    int fd; // current connection being handled
};

struct server {
    int jobs[MAX_JOBS];
    size_t num_jobs;

    int fd; // server file descr
    handler_func *handler;
    cleanup_func *user_cleanup_fn;
    struct worker *workers;
} serv = {0};

// used to access jobs global variable
pthread_mutex_t jobs_mutex = PTHREAD_MUTEX_INITIALIZER;

// used by main thread to wake up asleep worker threads
pthread_cond_t new_jobs_cond = PTHREAD_COND_INITIALIZER;

// used by main thread to wait for available workers when jobs is full
pthread_cond_t worker_available_cond = PTHREAD_COND_INITIALIZER;

void server_cleanup();
static void *worker(void *w);
static void sigint_handler(int arg);

int listen_and_serve(uint16_t port, handler_func *handler_fn,
                     cleanup_func *cleanup_fn) {

    // another server is running.
    if (serv.workers) { return -1; }

    signal(SIGINT, sigint_handler);

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
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = { htonl(INADDR_ANY) },
    };
    if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
        die("bind port %d failed:", port);
    }

    serv = (struct server) {
        .fd = server_fd,
        .handler = handler_fn,
        .user_cleanup_fn = cleanup_fn,
        .workers = calloc(NUM_WORKERS, sizeof(struct worker)),
    };
    if (serv.workers == NULL) { die("calloc workers"); }

    int err;
    for (int i = 0; i < NUM_WORKERS; i++) {
        serv.workers[i].id = i;
        err = pthread_create((pthread_t *)&serv.workers[i].thread,
                NULL, &worker, serv.workers + i);
        if (err != 0) { die("pthread_create worker %d", i); }
    }

    if (listen(server_fd, BACKLOG) == -1) {
        die("listen on port %d failed:", port);
    }
    printf("listening on port: %d\n", port);

    int conn_fd;
    while (1) {
        conn_fd = accept(server_fd, NULL, NULL);
        if (conn_fd == -1) {
            printf("accept connection failed: %s\n", strerror(errno));
            continue;
        }

#ifdef DEBUG
        printf("recieved connection on (fd: %d)\n", conn_fd);
#endif

        pthread_mutex_lock(&jobs_mutex);
        if (serv.num_jobs == MAX_JOBS) {
            // wait until a worker is available
            pthread_cond_wait(&worker_available_cond, &jobs_mutex);
        }

        serv.jobs[serv.num_jobs] = conn_fd;
        serv.num_jobs++;

        pthread_cond_signal(&new_jobs_cond);
        pthread_mutex_unlock(&jobs_mutex);
    }

    // should not happen, but in case.
    server_cleanup();
    return 0;
}

#define WORKER(w) ((struct worker *)w)

static void cleanup_worker(void *w);

static void *
worker(void *w) {
    int fd;
    size_t nrecv, buflen = 1024;
    char *buf = malloc(buflen * sizeof(char));
    if (buf == NULL) {
        die("malloc: worker %d", WORKER(w)->id);
    }

    pthread_cleanup_push(cleanup_worker, buf);

    while(1) {
        pthread_mutex_lock(&jobs_mutex);
        if (serv.num_jobs == 0) {
            // signal to main thread that a worker is available
            pthread_cond_signal(&worker_available_cond);

            // wait until `new_jobs_cond` is signaled and there are available
            // jobs.
            do {
                pthread_cond_wait(&new_jobs_cond, &jobs_mutex);
            } while (serv.num_jobs == 0);
        }

        fd = serv.jobs[--serv.num_jobs];
        pthread_mutex_unlock(&jobs_mutex);
        WORKER(w)->fd = fd;

#ifdef DEBUG
        printf("worker %d: handling connection (fd: %d)\n", WORKER(w)->id, fd);
#endif

        do {
            nrecv = recv(fd, buf, buflen, 0);
#ifdef DEBUG
            if (nrecv == -1) {
                printf("worker %d: read from connection (fd: %d) failed: %s\n",
                        WORKER(w)->id, fd, strerror(errno));
                break;

            } else if (nrecv == 0) {
                printf("worker %d: closing connection\n", WORKER(w)->id);
                break;

            } else {
                printf("worker %d: recieved %zd bytes\n", WORKER(w)->id, nrecv);
            }
#else
            // close connection: on failure (nrecv is -1) or on EOF (nrecv is 0)
            if (nrecv <= 0) {
                break;
            }
#endif

            // TODO:
            // - logging
            // - timeout
            serv.handler(fd, buf);
        } while (1);

        close(fd);
        WORKER(w)->fd = 0;
    }

    pthread_cleanup_pop(1);
}

int send_msg(const int fd, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char* msg = NULL;
    if (vasprintf(&msg, format, args) == -1) die("send_msg");
    va_end(args);

    size_t msglen = strlen(msg),
           nsend = send(fd, msg, msglen, 0);
    free(msg);
    if (nsend == -1) {
        printf("send on fd %d failed: %s\n", fd, strerror(errno));
        return -1;
    }
    return 0;
}

void server_cleanup() {
    for (int i = 0; i < NUM_WORKERS; ++i) {
        pthread_cancel(serv.workers[i].thread);
        if (serv.workers[i].fd != 0) {
            close(serv.workers[i].fd);
        }
    }
    free(serv.workers);
    close(serv.fd);

    serv = (struct server){0};
}

static void cleanup_worker(void *buf) {
    free(buf);
}

static void sigint_handler(int arg) {
    server_cleanup();

    if (serv.user_cleanup_fn) {
        serv.user_cleanup_fn();
    }

    exit(0);
}
