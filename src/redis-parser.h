#ifndef REDIS_PARSER_H
#define REDIS_PARSER_H

// parser for the RESP protocol

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    // '*' Array.
    // '$' Bulk.
    // '+' String.
    // ':' Integer, `raw` will contain parsed integer.
    // '-' Error.
    char t;
    int len;
    void** raw;
} resp;

bool resp_str_is(resp* data, char* msg);

void resp_display(resp* data);

void resp_destroy(resp* data);

resp* parse(const char* input);

#endif
