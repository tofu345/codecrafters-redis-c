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

// #define DEBUG

struct {
    int fds[MAX_JOBS]; // sockets to connections
    size_t len;
} jobs = {};

// used to sleep / wakeup worker threads
pthread_cond_t new_jobs_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t jobs_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int id, fd;
    bool awake;
    char *buf;
    pthread_t const thread;
} worker_data;

struct server {
    handler_func *handler;
    worker_data *workers;
    int w_count;
    int fd;
} serv = {
    .workers = NULL,
    .w_count = INITIAL_WORKERS,
    .fd = -1,
};

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

#define WORKER(w) ((worker_data *)w)

static void cleanup_worker(void *w) {
    free(WORKER(w)->buf);
    if (WORKER(w)->fd != -1) {
        close(WORKER(w)->fd);
    }
}

static void *
worker(void *w) {
    size_t nrecv, buflen = 1024;
    char *buf = malloc(buflen * sizeof(char));
    if (buf == NULL) { die("malloc: worker"); }
    WORKER(w)->buf = buf;

    pthread_cleanup_push(cleanup_worker, w);

    while(1) {
        pthread_mutex_lock(&jobs_mutex);
        if (jobs.len <= 0) {
            WORKER(w)->awake = false;

            // wait until `new_jobs_cond` is signaled
            // and there are available jobs.
            do {
                pthread_cond_wait(&new_jobs_cond, &jobs_mutex);
            } while (jobs.len <= 0);

            WORKER(w)->awake = true;
        }

        int fd = jobs.fds[--jobs.len];
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
                printf("worker %d: recieved 0 bytes, closing connection\n",
                        WORKER(w)->id);
                break;

            } else {
                printf("worker %d: recieved %zd bytes\n", WORKER(w)->id, nrecv);
            }
#else
            if (nrecv <= 0) {
                break;
            }
#endif

            // TODO:
            // - logging
            // - timeout
            // - no strlen()?
            serv.handler(fd, buf, strlen(buf));
        } while (1);

        close(fd);
        WORKER(w)->fd = -1;
    }

    pthread_cleanup_pop(1);
}

cleanup_func *user_cleanup_fn = NULL;

static void sigint_handler(int arg);
void server_cleanup();

int listen_and_serve(uint16_t port, handler_func *handler_fn, cleanup_func *cleanup_fn) {
    // only one server per process
    if (serv.workers) { return -1; }

    user_cleanup_fn = cleanup_fn;
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
        die("bind port %d failed:");
    }

    serv = (struct server) {
        .handler = handler_fn,
        .workers = calloc(INITIAL_WORKERS, sizeof(worker_data)),
        .w_count = INITIAL_WORKERS,
        .fd = server_fd,
    };
    if (serv.workers == NULL) { die("malloc"); }

    int err;
    for (int i = 0; i < INITIAL_WORKERS; i++) {
        serv.workers[i].id = i;
        err = pthread_create((pthread_t *)&serv.workers[i].thread,
                NULL, &worker, serv.workers + i);
        if (err != 0) { die("pthread_create"); }
    }

    int connection_backlog = 128;
    if (listen(server_fd, connection_backlog) == -1) {
        die("listen on port %d failed:");
    }
    printf("listening on port: %d\n", port);

    int conn_fd;

#ifdef DEBUG
    char awake[serv.w_count + 1];
    awake[serv.w_count] = '\0';
#endif

    while (1) {
#ifdef DEBUG
        for (int i = 0; i < serv.w_count; i++) {
            awake[i] = serv.workers[i].awake ? 'x' : '_';
        }
        printf("workers: %s\n", awake);
#endif

        conn_fd = accept(server_fd, NULL, NULL);
        if (conn_fd == -1) {
            printf("accept connection failed: %s\n", strerror(errno));
            continue;
        }

#ifdef DEBUG
        printf("recieved connection on (fd: %d)\n", conn_fd);
#endif

        pthread_mutex_lock(&jobs_mutex);
        if (jobs.len >= MAX_JOBS) {
            pthread_mutex_unlock(&jobs_mutex);

            fprintf(stderr,
                    "too many concurrent requests, rejecting conn (fd: %d)\n",
                    conn_fd);

            close(conn_fd);
            continue;
        }

        jobs.fds[jobs.len] = conn_fd;
        jobs.len++;

        // main thread recieves requests and creates and destroys workers as needed
        //
        // - workers thread state: 'awake' (and processing a request) and 'asleep'. in bool[]
        // - number of current 'awake' workers
        //
        // when to spawn new workers:
        // - when num jobs in queue > 75% ?
        //
        // spawning new workers?

        pthread_cond_signal(&new_jobs_cond);
        pthread_mutex_unlock(&jobs_mutex);
    }
    return 0;
}

static void sigint_handler(int arg) {
    server_cleanup();

    if (user_cleanup_fn) {
        user_cleanup_fn();
    }

    exit(0);
}

void server_cleanup() {
    for (int i = 0; i < serv.w_count; ++i) {
        worker_data *cur = &serv.workers[i];
        pthread_cancel(cur->thread);
        if (cur->fd != -1) {
            close(cur->fd);
        }
    }
    free(serv.workers);
    serv.workers = NULL;
    close(serv.fd);
}
