#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include "redis.h"

static void
exit_err(const char* msg) {
    printf("err: %s", msg);
    exit(1);
}

Server* server_create(uint16_t port) {
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

void server_close(Server* s) {
    close(s->fd);
    free(s);
}

typedef struct {
    const char* data;
    const size_t len;
    size_t pos;
} Request;

// read till \r\n, returning data before \r\n
static char*
_read_elem(Request* r) {
    size_t i, buf_cap = 8;
    char* buf = malloc(buf_cap * sizeof(char));
    if (buf == NULL) exit_err("malloc");

    for (i = 0; (i + r->pos) < r->len; i++) {
        if (i > buf_cap) {
            buf_cap *= 2;
            buf = realloc(buf, buf_cap * sizeof(char));
            if (buf == NULL) exit_err("realloc");
        }
        buf[i] = r->data[r->pos + i];
    }
    r->pos += i;
    return buf;
}

static char*
_parse_simple_string(Request* r) {
    printf("simple string\n");
    return 0;
}

static int
_handle(const int fd, Request* r) {
    // char* buf;
    char* msg = "+PONG\r\n";
    size_t msglen = strlen(msg);
    size_t n = send(fd, msg, msglen, 0);
    if (n == -1) {
        printf("send to connection failed: %s \n", strerror(errno));
        return -1;
    }

    // switch (r->data[0]) {
    //     case '+':
    //         return _handle_simple_string(conn_fd, cmd, len);
    //     default:
    //         printf("command for '%c' not implemented\n", cmd[0]);
    //         break;
    // }

    return 0;
}

int server_listen(Server* s) {
	int connection_backlog = 5;
	if (listen(s->fd, connection_backlog) != 0) {
		printf("listen on port %d failed: %s \n", s->port, strerror(errno));
		return -1;
	}
	printf("waiting for a client to connect...\n");

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	int conn_fd = accept(s->fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (conn_fd == -1) {
		printf("accept connection failed: %s\n", strerror(errno));
        return -1;
    }
	printf("client connected\n");

    struct pollfd pfd[1] = { conn_fd, POLLIN };

    while (1) {
        int ready = poll((struct pollfd*)&pfd, 1, -1);
        if (ready == -1) {
            printf("poll: %s\n", strerror(errno));
            break;
        }

        if (!(pfd[0].revents & POLLIN)) {
            printf("poll: %s%s\n",
                    (pfd[0].revents & POLLHUP) ? "POLLHUP " : "",
                    (pfd[0].revents & POLLERR) ? "POLLERR " : "");
            break;
        }

        size_t nread, buflen = 100; // malloc?
        char buf[buflen];
        nread = recv(conn_fd, buf, buflen, 0);
        if (nread == -1) {
            printf("read from connection failed: %s\n", strerror(errno));
            return 0;
        }

        if (nread == 0) {
            puts("recieved: 0 bytes");
            break; // avoid infinite loop
        } else
            printf("recieved: %zd bytes: %.*s\n", nread, (int)nread, buf);

        Request r = {  buf, nread, 0 };
        if (_handle(conn_fd, &r) == -1) {
            break;
        }
    }

    close(conn_fd);
    return -1;
}
