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
    //close(STDOUT_FILENO);
    //close(STDERR_FILENO);

    echo_socket();
}



int main() {
    forkd();
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
    char *buffer;
    int pos;
    int size = BUFFER_SIZE;
    
    if (!profile_dir) {
        perror("cannot open directory /etc/netctl/");
        return NULL;
    }

    buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "cannot allocate %i bytes", BUFFER_SIZE);
        return NULL;
    }

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
        buffer[count] = data[count++];
    }
    return count;
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

    while(1) {
        int done, n;
        t = sizeof(remote);
        if ((s2 = accept(s, (struct sockaddr *)&remote, &t)) == -1) {
            perror("failed to accept from socket");
            exit(1);
        }


        // recieve name (no longer than 100!)
        memset(str, 0, 1);
        n = recv(s2, str, 100, 0);
        if (n > 0) {
            str[n-1] = '\0'; 
        }
        if (strncmp(str, "list", 4) == 0) {
            char *example = "WPI-Wireless\ntin2";
            send(s2, example, strlen(example), 0);
            puts(get_list_of_interfaces());
        } else {

            sysctl_stop = generate_sysctl_stop();
            sysctl_start = generate_sysctl_start(str);
            if (sysctl_stop && sysctl_start) {
                puts(sysctl_start);
                system(sysctl_stop);
                system(sysctl_start);

                free(sysctl_stop);
                free(sysctl_start);
            } else if(sysctl_stop) {
                free(sysctl_stop);
            } else if(sysctl_start) {
                free(sysctl_start);
            }
        }



        // flush the remaining data and close the socket
        while(recv(s2, &trash, 1, 0));
        close(s2);
    }

    return 0;
}

