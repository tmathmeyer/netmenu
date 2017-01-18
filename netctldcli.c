#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "netctld.h"

int main(void)
{
    int s, t, len;
    struct sockaddr_un remote;
    char str[100];

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }

    printf("Connected ...\n");
    fgets(str, 100, stdin);
    if (!strncmp(str, "+ new interface", 15)) {
        char *argv[] = { "urxvt", "-e", "sudo", "wifi-menu", 0 };
        int status = execvp(argv[0], argv);
        printf("%i\n", status);
        if (status == -1) {
            printf("%s\n", strerror(errno));
        }
        return status;
    }
    send(s, "s ", 2, 0); 
    if (send(s, str, strlen(str), 0) == -1) {
        perror("send");
        exit(1);
    }

    memset(str, 0, 100);
    recv(s, str, 100, 0);
    puts(str);

    close(s);

    return 0;
}
