#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "hash-table/ht.h"
#include "server.h"
#include "parser.h"
#include "utils.h"

int handler(struct worker *, const char *data);
int echo(struct worker *, resp);
int set(struct worker *, resp);
int get(struct worker *, resp);

void cleanup(void);

pthread_mutex_t store_mutex = PTHREAD_MUTEX_INITIALIZER;
ht *store;

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // resp data = parse("*4\r\n$4\r\nECHO\r\n$3\r\nhey\r\n+hi\r\n$3\r\nhey\r\n");
    // resp_display(&data);
    // resp_destroy(&data);
    // return 0;

    store = ht_create();

    if (listen_and_serve(6379, &handler, &cleanup) != 0) {
        fprintf(stderr, "something bad happened!");
        return 1;
    }
    return EXIT_SUCCESS;
}

#define STR_IS(msg, r) strncasecmp(msg, r.data.string, r.length) == 0

int handler(struct worker *w, const char *data) {
    resp req = parse(data);
    if (req.type == 0 || req.type != r_Array || !string(req.data.array[0])) {
        return send_msg(w, "-Invalid command\r\n");
    }

    resp cmd = req.data.array[0];

    // char *buf; size_t len;
    // FILE *s = open_memstream(&buf, &len);
    // if (s == NULL) { die("open_memstream:"); }
    // resp_display(&req, s);
    // fclose(s);
    // printf("recieved: %s\n", buf);
    // free(buf);

    int err;
    if (STR_IS("PING", cmd)) {
        err = send_msg(w, "+PONG\r\n");

    } else if (STR_IS("ECHO", cmd)) {
        err = echo(w, req);

    } else if (STR_IS("GET", cmd)) {
        err = get(w, req);

    } else if (STR_IS("SET", cmd)) {
        err = set(w, req);

    } else {
        err = send_msg(w, "-Command not handled\r\n");
    }

    resp_destroy(&req);
    return err;
}

int echo(struct worker *w, resp req) {
    if (req.length != 2 || !string(req.data.array[1])) {
        return send_msg(w, "-Invalid ECHO command\r\n");
    }

    resp echo = req.data.array[1];
    char* msg = NULL;
    if (asprintf(&msg, "$%d\r\n%s\r\n", echo.length, echo.data.string) == -1) {
        return send_msg(w, "-Error creating response\r\n");
    }

    int err = send_msg(w, msg);
    free(msg);
    return err;
}

int get(struct worker *w, resp req) {
    if (req.length != 2 || !string(req.data.array[1])) {
        return send_msg(w, "-Invalid GET command\r\n");
    }

    resp key = req.data.array[1];
    uint64_t hash = hash_fnv1a_(key.data.string, key.length);

    pthread_mutex_lock(&store_mutex);
    char* val = ht_get_hash(store, hash);
    pthread_mutex_unlock(&store_mutex);

    if (errno == ENOKEY) {
        return send_msg(w, "$-1\r\n");
    }

    size_t len = strlen(val);
    char* msg = NULL;
    if (asprintf(&msg, "$%zu\r\n%s\r\n", strlen(val), val) == -1) {
        return send_msg(w, "-Error creating response\r\n");
    }

    int err = send_msg(w, msg);
    free(msg);
    return err;
}

int set(struct worker *w, resp req) {
    if (req.length != 3
            || !string(req.data.array[1])
            || !string(req.data.array[2])) {
        return send_msg(w, "-Invalid SET command\r\n");
    }

    resp key = req.data.array[1];
    resp val = req.data.array[2];
    char *val_str = strndup(val.data.string, val.length);
    if (val_str == NULL) {
        return send_msg(w, "-Could not execute SET command\r\n");
    }

    uint64_t hash = hash_fnv1a_(key.data.string, key.length);

    pthread_mutex_lock(&store_mutex);
    const char *res = ht_set_hash(store, (void *) key.data.string, val_str, hash);
    pthread_mutex_unlock(&store_mutex);

    if (errno == ENOMEM) {
        return send_msg(w, "-Could not execute SET command\r\n");
    } else if (res != val_str) {
        free((char *) res); // previous value
    }

    return send_msg(w, "+OK\r\n");
}

void cleanup(void) {
    hti it = ht_iterator(store);
    while(ht_next(&it)) {
        free(it.current->value);
    }
    ht_destroy(store);
}
