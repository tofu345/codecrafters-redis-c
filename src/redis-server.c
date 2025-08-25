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

void server_destroy(server* s) {
    free(s);
}

void
server_close(server* s) {
    close(s->fd);
    free(s);
}

// static size_t
// pos_clrf(const char* buf, size_t buf_len) {
//     size_t i = 0;
//     for (; (i + 1) < buf_len; i++) {
//         if (buf[i] == '\r' && buf[i + 1] == '\n')
//             return i + 1;
//     }
//     return 0;
// }
//
// static char*
// parse_element(size_t* pos, const char* buf, size_t buf_len) {
//     const char* buf_ = buf + *pos;
//     switch(buf_[0]) {
//         case '+':
//             size_t len = pos_clrf(buf_, buf_len);
//             if (len == -1) {
//                 printf("NULL: %s", buf);
//                 return NULL;
//             }
//
//             char* elem = malloc((len - 1) * sizeof(char));
//             strncpy(elem, buf_, len);
//             elem[len - 1] = '\0';
//             printf("elem %s\n", elem);
//             return elem;
//
//         default:
//             printf("parse_element: RESP data types %c not handled in %s\n", buf[0], buf);
//             return NULL;
//     }
// }
//
// static int
// parse(const char* buf, size_t buf_len, char** elems, size_t* resp_len) {
//     size_t pos = 1;
//     switch(buf[0]) {
//         case '*':
//             *resp_len = atoi(buf + 1);
//             if (*resp_len == 0)
//                 return NULL;
//
//             pos += pos_clrf(buf, buf_len);
//             elems = malloc(*resp_len * sizeof(char*));
//             for (int i = 0; i < *resp_len; i++)
//                 elems[i] = parse_element(&pos, buf, buf_len);
//
//             return 0;
//
//         default:
//             printf("RESP data types %c not handled\n", buf[0]);
//             return -1;
//     }
// }

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
    int id = (uintptr_t)arg; // :p
    size_t nrecv, nsend, buflen = 100;
    // malloc?
    char buf[buflen] = {};

    while(1) {
        pthread_mutex_lock(&jobs_mutex);
        if (jobs.len <= 0) // in case :p
                           // wait until `new_jobs_cond` is signaled
            pthread_cond_wait(&new_jobs_cond, &jobs_mutex);

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
                printf("worker %d: recieved 0 bytes\n", id);
                break;
            } else
                printf("worker %d: recieved %zd bytes ", id, nrecv);

            resp* data = parse(buf);
            resp_display(data);

            resp* cmd = data->raw[0];

            if (strcmp("PING", (char*)cmd->raw) == 0) {
                if (send_msg(id, fd, "+PONG\r\n") != 0)
                    break;

            } else if (strcmp("ECHO", (char*)cmd->raw) == 0) {
                if (data->len < 2) {
                    send_msg(id, fd, "-invalid ECHO command\r\n");
                    continue;
                }

                resp* echo = data->raw[1];
                char* msg = calloc(echo->len + 10, sizeof(char));
                if (sprintf(msg, "$%d\r\n%s\r\n",
                            echo->len, (char*)echo->raw) == -1) {
                    close(fd);
                    printf("worker %d: asprintf response failed, quitting", id);
                    return NULL;
                }
                int err = send_msg(id, fd, msg);
                free(msg);
                if (err != 0) break;

            } else {
                if (send_msg(id, fd, "+PONG\r\n") != 0)
                    break;
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
    for (uintptr_t i = 0; i < NUM_WORKERS; i++) {
        pthread_create(threads+i, NULL, &worker, (void*)i);
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
    return 0;
}
