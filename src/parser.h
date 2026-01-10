#pragma once

// parser for the RESP protocol

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    // +OK\r\n
    r_String = 1,

    // -Error message\r\n
    r_Error,

    // :[<+|->]<value>\r\n
    r_Integer,

    // $<length>\r\n<data>\r\n
    r_BulkString,

    // *<number-of-elements>\r\n<element-1>...<element-n>\r\n
    r_Array,
} resp_type;

typedef union {
    const char *string;
    struct resp *array;
    int64_t integer;
} resp_data;

typedef struct resp {
    // 0 if invalid.
    resp_type type;

    // String, String, r_Error - length of string.
    // Array - the number of elements.
    // otherwise 1.
    int length;

    resp_data data;
} resp;

// parse `resp` from [input]. [resp.type] is 0 on err.
resp parse(const char *input);

// free `resp`.
void resp_destroy(resp *);

// print `resp` into [stream].
void resp_display(resp *, FILE *stream);

static inline bool string(resp r) {
    return r.type == r_String || r.type == r_BulkString;
}
