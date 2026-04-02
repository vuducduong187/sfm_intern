// ================= pm_1.c =================
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>
#include <sys/stat.h>

#define PID_FILE "/tmp/Pm_1.pid"
#define LOCK_FILE "/tmp/Pm_1.lock"

int lock_fd = -1;

void daemonize(){
    pid_t pid = fork();
    if(pid < 0) exit(1);
    if(pid > 0) exit(0);

    setsid();

    pid = fork();
    if(pid < 0) exit(1);
    if(pid > 0) exit(0);

    umask(0);
    chdir("/");

    long max_fd = sysconf(_SC_OPEN_MAX);
    for (int i = 0; i < max_fd; i++)
        close(i);
    
    int fd0 = open("/dev/null", O_RDWR);
    int fd1 = dup(0);
    int fd2 = dup(0);
}

void cleanup() {
    unlink(LOCK_FILE);
    unlink(PID_FILE);
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
    printf("Pm_1 stopped\n");
}

void handle_signal(int sig) {
    cleanup();
    exit(0);
}

int main() {
    daemonize();
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        printf("Pm_1 already running\n");
        if (lock_fd >= 0) close(lock_fd);
        exit(1);
    }

    FILE *f = fopen(PID_FILE, "w");
    if (f) { 
        fprintf(f, "%d\n", getpid()); 
        fclose(f); 
    }

    printf("Pm_1 started (PID: %d)\n", getpid());

    while (1) {
        printf("Pm_1 is running...\n");
        sleep(3);
    }

    cleanup();
    return 0;
}
/*
// ================= pm_1.c =================
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>

#define PID_FILE "/tmp/pm_1.pid"
#define LOCK_FILE "/tmp/pm_1.lock"

int lock_fd = -1;

void cleanup() {
    unlink(PID_FILE);
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
    printf("PM_1 stopped\n");
}

void handle_signal(int sig) {
    cleanup();
    exit(0);
}

int main() {
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        printf("PM_1 already running\n");
        if (lock_fd >= 0) close(lock_fd);
        exit(1);
    }

    FILE *f = fopen(PID_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }

    printf("PM_1 started (PID: %d)\n", getpid());

    while (1) {
        printf("PM_1 running...\n");
        sleep(3);
    }

    cleanup();
    return 0;
}*/