#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include "netctld.h"

// every 10 profiles requires about 400B.
#define BUFFER_SIZE 2048


int write_sysctl(char *buffer, char *profile);
int write_chars_to_buffer(char *buffer, const char *src);
int echo_socket();
char *generate_sysctl_stop();
char *generate_sysctl_start(char *profile_name);
void chown_socket(void);

void forkd() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("can't fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    umask(0);
    pid_t sid = setsid();
    if (sid < 0) {
        perror("can't set sid");
        exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0) {
        perror("cant chdir to /");
        exit(EXIT_FAILURE);
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    echo_socket();
}



int main(int argc, char **argv) {
    int fork = 1;
    int verbose = 0;

    while(argc--) {
        if (!strncmp("--no-fork", argv[argc], 9)) {
            fork = 0;
        }
    }

    if (fork) {
        forkd();
    } else {
        echo_socket();
    }

    puts("finished");
    return 0;
}

void chown_socket(void) {
    char syscall[100] = {0};
    int pos = 0;

    pos += write_chars_to_buffer(syscall+pos, "chmod 777 " SOCK_PATH);
    system(syscall);
}

char *generate_sysctl_start(char *profile_name) {
    char *buffer = calloc(1, BUFFER_SIZE);
    int pos = 0;
    if (!buffer) {
        return NULL;
    }
    pos += write_chars_to_buffer(buffer+pos, "systemctl start ");
    pos += write_sysctl(buffer+pos, profile_name);
    buffer[pos] = '\0';
    return buffer;
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
                break;
        }
    }

    closedir(profile_dir);
    buffer[pos] = '\0';

    return buffer;
}

char *get_list_of_interfaces() {
    DIR *profile_dir = opendir("/etc/netctl/");
    struct dirent *entries;
    char *buffer = NULL;
    int pos = 0;
    int size = BUFFER_SIZE;

    if (!profile_dir) {
        perror("cannot open directory /etc/netctl/");
        return NULL;
    }

    buffer = calloc(1, size);
    if (!buffer) {
        fprintf(stderr, "cannot allocate %i bytes", BUFFER_SIZE);
        return NULL;
    }

    rewinddir(profile_dir);
    while (entries = readdir(profile_dir)) {
        switch(entries->d_type) { // so what if it's not POSIX?
            case DT_REG:
            case DT_LNK:
                pos += write_chars_to_buffer(buffer+pos, entries->d_name);
                pos += write_chars_to_buffer(buffer+pos, "\n");
                if (pos > size-15) {
                    void *n = realloc(buffer, size*=2);
                    if (n) {
                        buffer = n;
                    } else {
                        perror("cannot allocate");
                        free(buffer);
                        return NULL;
                    }
                }
                break;
        }
    }

    closedir(profile_dir);
    buffer[pos] = '\0';

    return buffer;

}

char *sanitize(char *profile) {
    char *newprof = calloc(1, BUFFER_SIZE), *prof=profile;
    int npl = 0;
    while(*prof) {
        switch(*prof) {
            case '-':
                newprof[npl++] = '\\';
                newprof[npl++] = '\\';
                newprof[npl++] = 'x';
                newprof[npl++] = '2';
                newprof[npl++] = 'd';
                break;
            default:
                newprof[npl++] = *prof;
                break;
        }
        prof++;
    }
    newprof[npl] = '\0';
    return newprof;
}



int write_sysctl(char *buffer, char *profile) {
    int place=0, sec=0;
    while(profile[sec]) {
        if (profile[sec] == ' ') {
            profile[sec] = '\0';
            break;
        }
        sec++;
    }
    profile = sanitize(profile);
    place += write_chars_to_buffer(buffer+place, "netctl@");
    place += write_chars_to_buffer(buffer+place, profile);
    place += write_chars_to_buffer(buffer+place, ".service ");
    free(profile);
    return place;
}

int write_chars_to_buffer(char *buffer, const char *data) {
    int count = 0; 
    while(data[count]) {
        buffer[count-1] = data[count++];
    }
    return count;
}



void list_to_socket(int socket) {

    char *interfaces = get_list_of_interfaces();
    size_t i = strlen(interfaces);
    send(socket, (char *)&i, sizeof(size_t), 0);
    send(socket, interfaces, strlen(interfaces), 0);
    send(socket, "\0", 1, 0);
    free(interfaces);

    return;
}

void do_disconnect(int socket, char *interface) {
    char *stop_cmd = generate_sysctl_stop();
    char *start_cmd = generate_sysctl_start(interface);

    if (!stop_cmd || !start_cmd) {
        send(socket, "error - could not allocate commands", 6, 0);
        if (stop_cmd) {
            free(stop_cmd);
        }
        if (start_cmd) {
            free(start_cmd);
        }
    } else {
        int stop_status = system(stop_cmd);
        int start_status;

        if (stop_status == 0) {
            start_status = system(start_cmd);

            if (start_status == 0) {
                send(socket, "success", 8, 0);
            } else {
                send(socket, "error - connection failed", 6, 0);
            }
        } else {
            send(socket, "error - disconnection failed", 6, 0);
        }
        free(stop_cmd);
        free(start_cmd);
    }

    return;
}

char *alloc_name(char *buffer){
    int ctr = 0;
    while(buffer[ctr]!=' ' && buffer[ctr]!='\n' && buffer[ctr]!='\0') {
        ctr++;
    }
    char *result = malloc(ctr+1);
    memcpy(result, buffer, ctr);
    result[ctr] = '\0';
    return result;
}

void sock_listener(int server_socket, struct sockaddr *remote) {
    int socket, status;
    char buffer[100], *req_iface;
    while(1) {
        if ((socket = accept(server_socket, remote, &status)) == -1) {
            exit(1);
        }
        memset(buffer, 0, 100);
        status = recv(socket, buffer, 100, 0);
        switch(buffer[0]){
            case 'l':
                list_to_socket(socket);
                break;
            case 's':
                req_iface = alloc_name(buffer+2);
                do_disconnect(socket, req_iface);
                free(req_iface);
                break;
        }
        close(socket);
    }
}



int echo_socket()
{
    int s, s2, t, len;
    struct sockaddr_un local, remote;
    char str[100], trash, *sysctl_stop, *sysctl_start;

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

    chown_socket();

    sock_listener(s, (struct sockaddr *) &remote);

    return 0;
}

