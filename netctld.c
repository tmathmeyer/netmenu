#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// every 10 profiles requires about 400B.
#define BUFFER_SIZE 2048
#define SOCK_PATH "/tmp/netctl.d.socket"

int write_sysctl(char *buffer, const char *profile);
int write_chars_to_buffer(char *buffer, const char *src);
char *generate_sysctl_stop();
int echo_socket();


int main() {
    echo_socket();

    return 0;
}


char *generate_sysctl_stop() {
    DIR *profile_dir = opendir("/etc/netctl/");
    struct dirent *entries;
    char *buffer;
    int pos;
    if (!profile_dir) {
        perror("cannot open directory /etc/netctl/");
        return NULL;
    }

    buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        fprintf(stderr, "cannot allocate %i bytes", BUFFER_SIZE);
        return NULL;
    }

    pos = write_chars_to_buffer(buffer, "systemctl stop ");
    while (entries = readdir(profile_dir)) {
        switch(entries->d_type) { // so what if it's not POSIX?
            case DT_REG:
            case DT_LNK:
                pos += write_sysctl(buffer+pos, entries->d_name);
                pos += write_chars_to_buffer(buffer+pos, "\n");
                break;
        }
    }

    closedir(profile_dir);
    buffer[pos] = '\0';

    return buffer;
}


int write_sysctl(char *buffer, const char *profile) {
    int place  = 0;
    place += write_chars_to_buffer(buffer+place, "netctl@");
    place += write_chars_to_buffer(buffer+place, profile);
    place += write_chars_to_buffer(buffer+place, ".service");
    return place;
}

int write_chars_to_buffer(char *buffer, const char *data) {
    int count = 0; 
    while(data[count]) {
        buffer[count] = data[count++];
    }
    return count;
}

int echo_socket()
{
    int s, s2, t, len;
    struct sockaddr_un local, remote;
    char str[100], trash, *sysctl_stop;

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket cant be opened");
        exit(1);
    }

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, SOCK_PATH);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(s, (struct sockaddr *)&local, len) == -1) {
        perror("socket cant be bound");
        exit(1);
    }

    if (listen(s, 5) == -1) {
        perror("socket can't be listened to");
        exit(1);
    }

    while(1) {
        int done, n;
        t = sizeof(remote);
        if ((s2 = accept(s, (struct sockaddr *)&remote, &t)) == -1) {
            perror("failed to accept from socket");
            exit(1);
        }


        // recieve name (no longer than 100!)
        n = recv(s2, str, 100, 0);
        sysctl_stop = generate_sysctl_stop();
        if (sysctl_stop) { // for now just print, later do better things
            puts(sysctl_stop);
            free(sysctl_stop);
        } 


        

        // flush the remaining data and close the socket
        while(recv(s2, &trash, 1, 0));
        close(s2);
    }

    return 0;
}

