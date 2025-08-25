#ifndef REDIS_PARSER_H
#define REDIS_PARSER_H

// parser for the RESP protocol

#include <stddef.h>

// '*' = Array
// '$' = Bulk
// '+' = String
// ':' = Integer
// '-' = Error

typedef struct {
    char t; // if ':' (Integer) `raw` will contain parsed integer
    int len;
    void** raw;
} resp;

void resp_display(resp* data);

void resp_destroy(resp* data);

resp* parse(const char* input);

#endif
