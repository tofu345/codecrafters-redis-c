#pragma once

// parser for the RESP protocol

#include <stddef.h>
#include <stdbool.h>

typedef struct resp resp;

typedef enum {
    r_String = 1,
    r_Array,
    r_BulkString,
    r_Integer,
    r_Error,
} resp_type;

typedef union {
    char *string;
    resp *array;
    long long integer;
} resp_data;

struct resp {
    resp_type type;
    int length; // if r_String, r_BulkString, r_Error: length of string.
                // if r_Array: number of elements.
                // otherwise: 1.
    resp_data data;
};

// Returns a [resp] with [resp.type] = 0 on err.
resp parse(const char *input);
void resp_destroy(resp data);

// print data to stdout
void resp_display(resp data);
