#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "redis-server.h"
#include "redis-parser.h"

int main() {
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

    // resp* data = parse("*1\r\n$4\r\nPING\r\n");
    // resp_display(data);
    // resp_destroy(data);

    server* s = server_create(6379);
    if (s == NULL) {
        return EXIT_FAILURE;
    }
    server_listen(s);
    server_destroy(s);

	return EXIT_SUCCESS;
}
