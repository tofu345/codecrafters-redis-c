#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "redis.h"

int main() {
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

    Server* s = server_create(6379);
    if (s == NULL || server_listen(s) != 0) {
        return EXIT_FAILURE;
    }
	return EXIT_SUCCESS;
}
