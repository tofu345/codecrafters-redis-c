#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>

#include "redis-server.h"
#include "redis-parser.h"

struct {
    int fds[NUM_WORKERS]; // sockets to connections
    size_t len;
} jobs = {};

pthread_mutex_t jobs_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t new_jobs_cond = PTHREAD_COND_INITIALIZER;

typedef struct {
    int id;
    ht* store;
} thread_data;

pthread_mutex_t store_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
exit_err(const char* msg) {
    printf("err: %s", msg);
    exit(1);
}

server*
server_create(uint16_t port) {
    server* s = malloc(sizeof(server));
    s->port = port;
	s->fd = socket(AF_INET, SOCK_STREAM, 0);
    s->store = ht_create();
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

void server_destroy(server* s) { free(s); }

void
server_close(server* s) {
    close(s->fd);
    free(s);
}

static int
send_msg(const int worker_id, const int fd, const char* msg) {
    size_t msglen = strlen(msg);
    size_t nsend = send(fd, msg, msglen, 0);
    if (nsend == -1) {
        printf("worker %d: send to connection (fd: %d) failed: %s\n",
                worker_id, fd, strerror(errno));
        return -1;
    }
    return 0;
}

static void*
worker(void* arg) {
    thread_data* td = arg;
    size_t nrecv, nsend, buflen = 100;
    char buf[buflen] = {}; // malloc?
    resp *data, *cmd;

    while(1) {
        pthread_mutex_lock(&jobs_mutex);
        if (jobs.len <= 0) // in case :p
                           // wait until `new_jobs_cond` is signaled
            pthread_cond_wait(&new_jobs_cond, &jobs_mutex);

        int fd = jobs.fds[jobs.len - 1];
        jobs.len--;
        pthread_mutex_unlock(&jobs_mutex);

        struct pollfd pfd = { fd, POLLIN };

        printf("worker %d: handling connection (fd: %d)\n", td->id, fd);

        do {
            nrecv = recv(fd, buf, buflen, 0);
            if (nrecv == -1) {
                printf("worker %d: read from connection (fd: %d) failed: %s\n",
                        td->id, fd, strerror(errno));
                break;
            } else if (nrecv == 0) {
                printf("worker %d: recieved 0 bytes\n", td->id);
                break;
            } else
                printf("worker %d: recieved %zd bytes ", td->id, nrecv);

            data = parse(buf);
            resp_display(data);

            cmd = data->raw[0];
            if (resp_str_is(cmd, "PING")) {
                send_msg(td->id, fd, "+PONG\r\n");

            } else if (resp_str_is(cmd, "ECHO")) {
                if (data->len < 2) {
                    send_msg(td->id, fd, "-invalid ECHO command\r\n");
                    continue;
                }

                resp* echo = data->raw[1];
                char* msg = calloc(echo->len + 15, sizeof(char)); // better safe than sorry
                if (sprintf(msg, "$%d\r\n%s\r\n",
                            echo->len, (char*)echo->raw) == -1) {
                    close(fd);
                    resp_destroy(data);
                    printf("worker %d: asprintf response failed, quitting", td->id);
                    return NULL;
                }
                send_msg(td->id, fd, msg);
                free(msg);

            } else if (resp_str_is(cmd, "SET")) {
                if (data->len < 3) {
                    send_msg(td->id, fd, "-invalid SET command\r\n");
                    continue;
                }

                pthread_mutex_lock(&store_mutex);
                resp *key = data->raw[1],
                     *val = data->raw[2];
                char* val_str = strdup((char*)val->raw);

                if (ht_set(td->store, (char*)key->raw, val_str) == NULL) {;
                    pthread_mutex_unlock(&store_mutex);
                    close(fd);
                    resp_destroy(data);
                    printf("worker %d: ht_set failed, quitting", td->id);
                    return NULL;
                }

                pthread_mutex_unlock(&store_mutex);

                send_msg(td->id, fd, "+OK\r\n");

            } else if (resp_str_is(cmd, "GET")) {
                if (data->len < 2) {
                    send_msg(td->id, fd, "-invalid GET command\r\n");
                    continue;
                }

                pthread_mutex_lock(&store_mutex);
                resp *key = data->raw[1];
                char* val = ht_get(td->store, (char*)key->raw);
                if (val == NULL) {
                    pthread_mutex_unlock(&store_mutex);
                    send_msg(td->id, fd, "$-1\r\n");
                    continue;
                }
                pthread_mutex_unlock(&store_mutex);

                size_t len = strlen(val);
                char* msg = calloc(len + 15, sizeof(char));
                if (sprintf(msg, "$%zu\r\n%s\r\n", len, val) == -1) {
                    close(fd);
                    resp_destroy(data);
                    printf("worker %d: asprintf response failed, quitting", td->id);
                    return NULL;
                }
                send_msg(td->id, fd, msg);
                free(msg);

            } else {
                send_msg(td->id, fd, "-command not handled\r\n");
            }

            resp_destroy(data);
        } while (1);

        close(fd);
    }
}

int
server_listen(server* s) {
    int connection_backlog = 5;
    if (listen(s->fd, connection_backlog) != 0) {
        printf("listen on port %d failed: %s\n", s->port, strerror(errno));
        return -1;
    }
    printf("listening on port: %d\n", s->port);

    pthread_t threads[NUM_WORKERS];
    thread_data* tds[NUM_WORKERS];
    for (uintptr_t i = 0; i < NUM_WORKERS; i++) {
        tds[i] = malloc(sizeof(thread_data));
        tds[i]->store = s->store;
        tds[i]->id = (int)i;
        pthread_create(threads+i, NULL, &worker, (void*)tds[i]);
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
        pthread_cond_signal(&new_jobs_cond);
        pthread_mutex_unlock(&jobs_mutex);
    }

    // why not, okay?
    for (uintptr_t i = 0; i < NUM_WORKERS; i++) {
        free(tds[i]);
    }
    return 0;
}
