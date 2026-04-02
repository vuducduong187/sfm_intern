
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>

#define PID_FILE "/tmp/Sm.pid"
#define LOCK_FILE "/tmp/Sm.lock"

int lock_fd = -1;

void cleanup() {
    unlink(LOCK_FILE);
    unlink(PID_FILE);
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
    printf("Sm stopped\n");
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
        printf("Sm already running\n");
        if (lock_fd >= 0) close(lock_fd);
        exit(1);
    }

    FILE *f = fopen(PID_FILE, "w");
    if (f) { 
        fprintf(f, "%d\n", getpid()); 
        fclose(f); 
    }
    printf("Sm started (PID: %d)\n", getpid());
    while (1) {
        printf("Sm is running...\n");
        sleep(3);
    }
    cleanup();
    return 0;
}
