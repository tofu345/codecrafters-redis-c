#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "hash-table/ht.h"
#include "server.h"
#include "parser.h"

int handler(const int conn_fd, char *data, size_t length);
int echo_handler(const int conn_fd, resp req);
int set_handler(const int conn_fd, resp req);
int get_handler(const int conn_fd, resp req);

pthread_mutex_t store_mutex = PTHREAD_MUTEX_INITIALIZER;
ht *store;

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    store = ht_create();

    if (listen_and_serve(6379, &handler) != 0) {
        puts("something happened!");
        return 1;
    }
    return EXIT_SUCCESS;
}

#define STR_IS(msg, r) strcasecmp(msg, r.data.string) == 0
#define STRING(el) (el.type == r_String || el.type == r_BulkString)

int handler(const int conn_fd, char *data, size_t length) {
    resp req = parse(data);
    if (req.type == 0 || req.type != r_Array || !STRING(req.data.array[0])) {
        return send_msg(conn_fd, "-Invalid command\r\n");
    }

    resp cmd = req.data.array[0];
    printf("recieved: "); resp_display(req);

    int err;
    if (STR_IS("PING", cmd)) {
        err = send_msg(conn_fd, "+PONG\r\n");

    } else if (STR_IS("ECHO", cmd)) {
        err = echo_handler(conn_fd, req);

    } else if (STR_IS("GET", cmd)) {
        err = get_handler(conn_fd, req);

    } else if (STR_IS("SET", cmd)) {
        err = set_handler(conn_fd, req);

    } else {
        err = send_msg(conn_fd, "-Command not handled\r\n");
    }

    resp_destroy(req);
    return err;
}

int echo_handler(const int conn_fd, resp req) {
    if (req.length < 2 && STRING(req.data.array[0])) {
        return send_msg(conn_fd, "-Invalid ECHO command\r\n");
    }

    resp echo = req.data.array[1];
    char* msg = NULL;
    if (asprintf(&msg, "$%d\r\n%s\r\n", echo.length, echo.data.string) == -1) {
        resp_destroy(req);
        return send_msg(conn_fd, "-Error sending message\r\n");
    }

    int err = send_msg(conn_fd, msg);
    free(msg);
    return err;
}

int get_handler(const int conn_fd, resp req) {
    if (req.length < 2 && STRING(req.data.array[0])) {
        return send_msg(conn_fd, "-Invalid GET command\r\n");
    }

    resp key = req.data.array[1];
    pthread_mutex_lock(&store_mutex);
    char* val = ht_get(store, key.data.string);
    pthread_mutex_unlock(&store_mutex);
    if (val == NULL) {
        return send_msg(conn_fd, "$-1\r\n");
    }

    size_t len = strlen(val);
    char* msg = NULL;
    if (asprintf(&msg, "$%zu\r\n%s\r\n", strlen(val), val) == -1) {
        return send_msg(conn_fd, "-Error sending message\r\n");
    }

    int err = send_msg(conn_fd, msg);
    free(msg);
    return err;
}

int set_handler(const int conn_fd, resp req) {
    if (req.length < 3 && STRING(req.data.array[0]) && STRING(req.data.array[1])) {
        return send_msg(conn_fd, "-Invalid SET command\r\n");
    }

    resp key = req.data.array[1];
    resp val = req.data.array[2];
    char *val_str = strdup(val.data.string);
    if (val_str == NULL) {
        return send_msg(conn_fd, "-Could not execute SET command\r\n");
    }

    pthread_mutex_lock(&store_mutex);
    const char *err = ht_set(store, key.data.string, val_str);
    pthread_mutex_unlock(&store_mutex);
    if (err == NULL) {;
        return send_msg(conn_fd, "-Could not execute SET command\r\n");
    }

    return send_msg(conn_fd, "+OK\r\n");
}
