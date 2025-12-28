#include "parser.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *input;
    size_t length, cursor;
} parser;

// 0 on success
int _parse(parser *p, resp *elem);

resp parse(const char *input) {
    parser p = { input, strlen(input) };
    resp result = {0};
    if (_parse(&p, &result)) {
        resp_destroy(&result);
        return (resp){};
    }
    return result;
}

void resp_destroy(resp *elem) {
    switch (elem->type) {
        case r_Error:
        case r_String:
        case r_BulkString:
            return;

        case r_Array:
            for (int i = 0; i < elem->length; i++) {
                resp_destroy(&elem->data.array[i]);
            }
            free(elem->data.array);
            return;

        case r_Integer:
            return;
    }
}

// return [resp_type] of `p.input[p.cursor]`
static resp_type
get_resp_type(parser *p) {
    switch (p->input[p->cursor]) {
        case '+': return r_String;
        case '-': return r_Error;
        case '$': return r_BulkString;
        case '*': return r_Array;
        case ':': return r_Integer;
        default:
            die("invalid resp type: '%c' in input: %s",
                    p->input[p->cursor], p->input);
            return 0;
    }
}

// read (+|-|$|*|:)(...)/r/n
static int
read_start_control_sequence(parser *p, resp *elem) {
    resp_type type = get_resp_type(p);
    ++p->cursor; // skip type

    size_t i = p->cursor;
    for (; i + 1 < p->length; i++) {
        if (p->input[i] == '\r' && p->input[i + 1] == '\n')
            break;
    }

    // invalid [p.input]: string too short or missing terminator
    if (i + 1 >= p->length) {
        printf("invalid input: %s", p->input);
        return -1;
    }

    size_t str_length = i - p->cursor;
    *elem = (resp) {
        .type = type,
        .length = str_length,
        .data = { .string = p->input + p->cursor }
    };
    p->cursor = i + 2; // skip '\r\n'
    return 0;
}

int _parse(parser *p, resp *elem) {
    int err = read_start_control_sequence(p, elem);
    if (err) { return err; }

    char *endptr;
    const char *end = elem->data.string + elem->length;
    switch (elem->type) {
        case r_String:
            return 0;

        case r_Integer:
            elem->data.integer = strtol(elem->data.string, &endptr, 10);
            if (endptr != end || errno == ERANGE) {
                elem->data.string = NULL;
                return -1;
            }
            elem->length = 1;
            return 0;

        case r_Array:
            elem->length = strtol(elem->data.string, &endptr, 10);
            if (endptr != end || errno == ERANGE) {
                elem->data.array = NULL;
                return -1;
            }

            elem->data.array = malloc(elem->length * sizeof(resp));
            if (elem->data.array == NULL) {
                elem->length = 0;
                return -1;
            }

            resp *cur;
            for (int i = 0; i < elem->length; i++) {
                cur = &elem->data.array[i];
                int res = _parse(p, cur);
                if (cur->type == 0) {
                    // free previous elements
                    elem->length = i;
                    resp_destroy(elem);
                    elem->data.array = NULL;
                    return -1;
                }
            }
            return 0;

        case r_BulkString:
            elem->length = strtol(elem->data.string, &endptr, 10);
            if (endptr != end || errno == ERANGE) {
                elem->data.string = NULL;
                return -1;
            }

            if (elem->length == 0) {
                p->cursor += 2; // skip /r/n
                elem->data.string = NULL;
                return 0;
            }

            elem->data.string = p->input + p->cursor;
            p->cursor += elem->length + 2; // skip data and /r/n
            return 0;

        default:
            printf("RESP type %d not handled. cur: %zu '%s'",
                    elem->type, p->cursor, p->input);
            return -1;
    }
}

void _resp_display(resp *elem, FILE *s) {
    switch (elem->type) {
        case r_String:
        case r_Error:
            fprintf(s, "+%.*s", elem->length, elem->data.string);
            return;

        case r_BulkString:
            if (elem->length == 0)
                fprintf(s, "$(empty)");
            else
                fprintf(s, "$%.*s", elem->length, elem->data.string);
            return;

        case r_Array:
            fprintf(s, "*[");
            int last = elem->length - 1;
            for (size_t i = 0; i < last; i++) {
                _resp_display(&elem->data.array[i], s);
                fprintf(s, ", ");
            }
            if (last >= 0) {
                _resp_display(&elem->data.array[last], s);
            }
            fprintf(s, "]");
            return;

        case r_Integer:
            fprintf(s, ":%lld", elem->data.integer);
            return;

        default:
            die("RESP type %d not handled.", elem->type);
            return;
    }
}

void resp_display(resp *data, FILE *stream) {
    _resp_display(data, stream);
}
