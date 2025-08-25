#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "redis.h"

int main() {
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

    Server* s = server_create(6379);
    if (s == NULL) {
        return EXIT_FAILURE;
    }
    server_listen(s);
    server_destroy(s);
	return EXIT_SUCCESS;
}
