#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>

#include "redis.h"

struct {
    int fds[NUM_WORKERS]; // file descriptors for sockets to connections
    size_t len;
} jobs = {};

pthread_mutex_t jobs_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t available_jobs = PTHREAD_COND_INITIALIZER;

static void
exit_err(const char* msg) {
    printf("err: %s", msg);
    exit(1);
}

Server*
server_create(uint16_t port) {
    Server* s = malloc(sizeof(Server));
    s->port = port;
	s->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (s->fd == -1) {
		printf("socket creation failed: %s\n", strerror(errno));
		return NULL;
    }

	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("setsockopt SO_REUSEADDR failed: %s\n", strerror(errno));
		return NULL;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET ,
        .sin_port = htons(port),
        .sin_addr = { htonl(INADDR_ANY) },
    };

	if (bind(s->fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("bind port %d failed: %s\n", port, strerror(errno));
		return NULL;
    }

    return s;
}

void server_destroy(Server* s) {
    free(s);
}

void
server_close(Server* s) {
    close(s->fd);
    free(s);
}

// read till \r\n and return data before \r\n
// static char*
// _read_elem(thread_info* r) {
//     size_t i, buf_cap = 8;
//     char* buf = malloc(buf_cap * sizeof(char));
//     if (buf == NULL) exit_err("malloc");
//
//     for (i = 0; (i + r->pos) < r->len; i++) {
//         if (i > buf_cap) {
//             buf_cap *= 2;
//             buf = realloc(buf, buf_cap * sizeof(char));
//             if (buf == NULL) exit_err("realloc");
//         }
//         buf[i] = r->data[r->pos + i];
//     }
//     r->pos += i;
//     return buf;
// }

static void*
handle(void* arg) {
    int id = (uintptr_t)arg; // :p
    size_t nrecv, nsend, buflen = 100;
    char buf[buflen]; // malloc?

    while(1) {
        pthread_mutex_lock(&jobs_mutex);
        if (jobs.len <= 0) // in case :p
                           // wait until `available_jobs` is signaled
            pthread_cond_wait(&available_jobs, &jobs_mutex);

        int fd = jobs.fds[jobs.len - 1];
        jobs.len--;
        pthread_mutex_unlock(&jobs_mutex);

        struct pollfd pfd = { fd, POLLIN };

        printf("worker %d: handling connection (fd: %d)\n", id, fd);

        do {
            nrecv = recv(fd, buf, buflen, 0);
            if (nrecv == -1) {
                printf("worker %d: read from connection (fd: %d) failed: %s\n",
                        id, fd, strerror(errno));
                break;
            } else if (nrecv == 0) {
                printf("worker %d: recieved: 0 bytes\n", id);
                break;
            } else
                printf("worker %d: recieved: %zd bytes\n", id, nrecv);
                // printf("worker %d: recieved: %zd bytes: %.*s\n",
                //         id, nrecv, (int)nrecv, buf);

            char* msg = "+PONG\r\n";
            size_t msglen = strlen(msg);
            nsend = send(fd, msg, msglen, 0);
            if (nsend == -1) {
                printf("worker %d: send to connection (fd: %d) failed: %s\n",
                        id, fd, strerror(errno));
                break;
            }
        } while (1);

        close(fd);
    }
}

int
server_listen(Server* s) {
    int connection_backlog = 5;
    if (listen(s->fd, connection_backlog) != 0) {
        printf("listen on port %d failed: %s\n", s->port, strerror(errno));
        return -1;
    }
    printf("listening on port: %d\n", s->port);

    pthread_t threads[NUM_WORKERS];
    for (uintptr_t i = 0; i < NUM_WORKERS; i++) {
        pthread_create(threads+i, NULL, &handle, (void*)i);
    }

    while (1) {
        // struct sockaddr_in client_addr;
        // socklen_t client_addr_len = sizeof(client_addr);
        int conn_fd = accept(s->fd, NULL, NULL);
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
        pthread_cond_signal(&available_jobs);
        pthread_mutex_unlock(&jobs_mutex);
    }
    return 0;
}
