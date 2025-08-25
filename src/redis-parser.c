#include "redis-parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// pointer to current char in original input under inspection
typedef struct {
    const char* input;
} parser;

resp* _parse(parser* p);
void _resp_display(resp* data);

resp*
parse(const char* input) {
    parser p = { input };
    return _parse(&p);
}

void
resp_display(resp* data) {
    _resp_display(data);
    puts("");
}

void
resp_destroy(resp* data) {
    switch (data->t) {
        case '+':
        case '$':
            free(data->raw);
            break;
        case '*':
            for (int i = 0; i < data->len; i++) {
                resp_destroy(data->raw[i]);
            }
            free(data->raw);
            break;
    }
    free(data);
}

// return current char and increment pointer
static char
get_char(parser* p) {
    return *(p->input++);
}

static char*
get_str(parser* p, size_t len) {
    char* raw = malloc((len + 1) * sizeof(char));
    if (raw == NULL) return NULL;
    strncpy(raw, p->input, len);
    raw[len] = '\0';
    p->input += len;
    return raw;
}

// read `[elem.t][elem.raw]\r\n` and return pointer to end
static resp*
parse_simple(parser* p) {
    size_t i = 1;
    for (; p->input[i] != '\0'; i++) {
        if (p->input[i] == '\r' && p->input[i + 1] == '\n')
            break;
    }
    if (p->input[i] == '\0')
        return NULL;
    resp* elem = malloc(sizeof(resp));
    elem->t = get_char(p);
    elem->len = 1;
    elem->raw = (void**)get_str(p, i - 1); // till before \r\n
    p->input += 2; // skip \r\n
    return elem;
}

resp*
_parse(parser* p) {
    resp* data = parse_simple(p);
    if (data == NULL)
        return NULL;

    switch (data->t) {
        case '*': // array
            data->len = atoi((char*)data->raw);
            free(data->raw);
            if (data->len == 0)
                return NULL;

            data->raw = calloc(data->len, sizeof(resp*));
            for (int i = 0; i < data->len; i++) {
                data->raw[i] = _parse(p);
            }
            break;

        case '$':
            data->len = atoi((char*)data->raw);
            free(data->raw);
            if (data->len == 0)
                return NULL;

            data->raw = (void**)get_str(p, data->len);
            p->input += 2; // skip \r\n

        case '+':
            break;

        default:
            printf("RESP type %c not handled", data->t);
            return NULL;
    }

    return data;
}

void
_resp_display(resp* data) {
    switch (data->t) {
        case '+':
            printf("+%s", (char*)data->raw);
            break;
        case '$':
            printf("$%.*s", data->len, (char*)data->raw);
            break;
        case '*':
            printf("*[");
            for (int i = 0; i < data->len - 1; i++) {
                _resp_display(data->raw[i]);
                printf(", ");
            }
            if (data->len >= 1)
                _resp_display(data->raw[data->len - 1]);
            printf("]");
            break;
    }
}
