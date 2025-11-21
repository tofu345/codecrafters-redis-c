#include "utils.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void die(const char* format, ...) {
    va_list ap;
    int _errno;
    _errno = errno;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    if (format[0] && format[strlen(format)-1] == ':')
        fprintf(stderr, " %s", strerror(_errno));
    fputc('\n', stderr);

    exit(1);
}
