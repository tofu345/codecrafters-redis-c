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

resp _parse(parser *p);

resp
parse(const char *input) {
    parser p = { input, strlen(input) };
    return _parse(&p);
}

void
resp_destroy(resp elem) {
    switch (elem.type) {
        case r_Error:
        case r_String:
            free(elem.data.string);
            return;

        case r_BulkString:
            if (elem.length > 0)
                free(elem.data.string);
            return;

        case r_Array:
            for (int i = 0; i < elem.length; i++) {
                resp_destroy(elem.data.array[i]);
            }
            free(elem.data.array);
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
        case ':': return r_Integer; // TODO: NOT IMPLEMENTED
        default:
            die("invalid resp type: '%c' in input: %s",
                    p->input[p->cursor], p->input);
            return 0;
    }
}

// malloc string of size [len] from `p.input[from]`
static char*
string_from(parser *p, size_t from, size_t len) {
    char *str = malloc((len + 1) * sizeof(char));
    if (str == NULL) die("malloc");
    strncpy(str, p->input + from, len);
    str[len] = '\0';
    return str;
}

static resp
read_until_terminator(parser *p) {
    size_t i = p->cursor + 1;
    for (; i + 1 < p->length; i++) {
        if (p->input[i] == '\r' && p->input[i + 1] == '\n')
            break;
    }

    // invalid [p.input]: string too short or missing terminator
    if (i + 1 > p->length) {
        printf("invalid input: %s", p->input);
        return (resp){};
    }

    size_t str_length = i - p->cursor - 1;
    resp elem = {
        .type = get_resp_type(p),
        .length = str_length,
        .data = { .string = string_from(p, p->cursor + 1, str_length) }
    };
    p->cursor += str_length + 3; // skip type, '\r\n'
    return elem;
}

resp
_parse(parser *p) {
    resp elem = read_until_terminator(p);
    if (elem.type == 0) return (resp){};

    switch (elem.type) {
        case r_String: return elem;

        case r_Integer:
            elem.data.integer = strtol(elem.data.string, NULL, 10);
            if (errno == ERANGE) {
                resp_destroy(elem);
                return (resp){};
            }
            free(elem.data.string);
            elem.length = 1;
            return elem;

        case r_Array:
            elem.length = strtol(elem.data.string, NULL, 10);
            if (errno == ERANGE) {
                resp_destroy(elem);
                return (resp){};
            }
            free(elem.data.string);

            elem.data.array = malloc(elem.length * sizeof(resp));
            for (int i = 0; i < elem.length; i++) {
                elem.data.array[i] = _parse(p);
                if (elem.data.array[i].type == 0) {
                    elem.length = i + 1;
                    resp_destroy(elem);
                    return (resp){};
                }
            }
            return elem;

        case r_BulkString:
            elem.length = strtol(elem.data.string, NULL, 10);
            if (errno == ERANGE) {
                resp_destroy(elem);
                return (resp){};
            }
            free(elem.data.string);

            if (elem.length == 0) {
                p->cursor += 2;
                elem.data.string = NULL;
                return elem;
            }

            elem.data.string = string_from(p, p->cursor, elem.length);
            p->cursor += elem.length + 2;
            return elem;

        default:
            printf("RESP type %d not handled. cur: %zu '%s'",
                    elem.type, p->cursor, p->input);
            return (resp){};
    }
}

void
_resp_display(resp elem) {
    switch (elem.type) {
        case r_String:
        case r_Error:
            printf("+%s", elem.data.string);
            return;

        case r_BulkString:
            if (elem.length == 0)
                printf("$(empty)");
            else
                printf("$%.*s", elem.length, elem.data.string);
            return;

        case r_Array:
            printf("*[");
            for (size_t i = 0; i < elem.length - 1; i++) {
                _resp_display(elem.data.array[i]);
                printf(", ");
            }
            if (elem.length >= 1)
                _resp_display(elem.data.array[elem.length - 1]);
            printf("]");
            return;

        case r_Integer:
            printf("%lld", elem.data.integer);
            return;

        default:
            die("RESP type %d not handled.", elem.type);
            return;
    }
}

void
resp_display(resp data) {
    _resp_display(data);
    puts("");
}
