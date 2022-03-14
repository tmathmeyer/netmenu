#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "netctld.h"

#define BUFFER 2048

int main(void)
{
    int s, t, len;
    struct sockaddr_un remote;

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        exit(1);
    }

    if (send(s, "list", 5, 0) == -1) {
        exit(1);
    }

    size_t size = 0;
    if ((t=recv(s, (char *)&size, sizeof(size_t), 0)) > 0) {
        char *str = malloc(size+2);
        if ((t=recv(s, str, size, 0)) > 0) {
            str[t] = '\0';
            printf(str);
        }
        free(str);
        printf("+ new interface");
    }


    close(s);

    return 0;
}
