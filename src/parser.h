#pragma once

// parser for the RESP protocol

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct resp resp;

#define STRING(el) (el.type == r_String || el.type == r_BulkString)

typedef enum {
    r_String = 1,
    r_Array,
    r_BulkString,
    r_Integer,
    r_Error,
} resp_type;

typedef union {
    const char *string;
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
void resp_destroy(resp *);

// print data to stdout
void resp_display(resp *, FILE *);
