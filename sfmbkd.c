// ================= sfmbkd.c =================
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include<string.h>
#include <sys/stat.h>

#define PID_FILE "/tmp/sfmbkd.pid"
#define LOCK_FILE "/tmp/sfmbkd.lock"
#define STT_FILE "/home/intern/duong/sfm_read_write_file/sfm_cur_stt.conf"
#define FIRST_START_SFMD "/tmp/FS.sfmd"

int lock_fd = -1;

int crash_count_sfmd = 0;
time_t first_start_time_sfmd = 0;

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

void cleanup(){
    unlink(LOCK_FILE);
    unlink(PID_FILE);
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
    printf("SFMBKD stopped\n");
}

void handle_signal(int sig){
    cleanup();
    exit(0);
}

int is_alive(pid_t pid){
    if (pid <= 0) return 0;
    return (kill(pid, 0) == 0) ? 1 : 0;
}

pid_t read_pid(const char *file){
    FILE *f = fopen(file, "r");
    if (!f) return -1;
    pid_t pid = -1;
    fscanf(f, "%d", &pid);
    fclose(f);
    return pid;
}

void seconds_to_duration(time_t seconds, char *buf, size_t size){
    int d = seconds / 86400; 
    seconds %= 86400;
    int h = seconds / 3600;  
    seconds %= 3600;
    int m = seconds / 60;    
    seconds %= 60;
    int s = seconds;

    char temp[128] = "";
    if (d > 0) snprintf(temp + strlen(temp), sizeof(temp) - strlen(temp), "%dd", d);
    if (h > 0) snprintf(temp + strlen(temp), sizeof(temp) - strlen(temp), "%dh", h);
    if (m > 0) snprintf(temp + strlen(temp), sizeof(temp) - strlen(temp), "%dm", m);
    if (s > 0 || strlen(temp) == 0) snprintf(temp + strlen(temp), sizeof(temp) - strlen(temp), "%ds", s);

    strncpy(buf, temp, size - 1);
    buf[size - 1] = '\0';
}

void log_crash(const char *module, int crash_count, time_t uptime_sec){
    char duration[64];
    seconds_to_duration(uptime_sec, duration, sizeof(duration));
    FILE *f = fopen(STT_FILE, "a");
    if (f){
        fprintf(f, "%s\n crashed %d time %s\n", module, crash_count, duration);
        fclose(f);
    }
}

int main(){
    daemonize();
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        printf("SFMBKD already running\n");
        if (lock_fd >= 0) close(lock_fd);
        exit(1);
    }

    FILE *f = fopen(PID_FILE, "w");
    if (f) { 
        fprintf(f, "%d\n", getpid()); 
        fclose(f); 
    
    }

    printf("SFMBKD started (PID: %d)\n", getpid());

    while (1){
        time_t now = time(NULL);
        pid_t sfmd_pid = read_pid("/tmp/sfmd.pid");
        if (sfmd_pid <= 0 || !is_alive(sfmd_pid)) {
            FILE *fs_sfmd = fopen(FIRST_START_SFMD, "r");
            fscanf(fs_sfmd, "%ld", &first_start_time_sfmd);
            printf("TIME_START_SFMD: %ld\n", first_start_time_sfmd);
            crash_count_sfmd++;
            time_t uptime_sfmd = now - first_start_time_sfmd;
            log_crash("sfmd", crash_count_sfmd, uptime_sfmd);
            printf("SFMD crashed -> restarting by SFMBKD...\n");
            //unlink(STT_FILE);        // Reset toàn bộ khi SFMBKD khởi động lại SFMD
            system("/home/intern/duong/sfm_read_write_file/sfmd &");
            sleep(1);
        }
        sleep(2);
    }

    cleanup();
    return 0;
}
/*
// ================= sfmbkd.c =================
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>
#include <errno.h>

#define PID_FILE "/tmp/sfmbkd.pid"
#define LOCK_FILE "/tmp/sfmbkd.lock"

int lock_fd = -1;

void cleanup() {
    unlink(PID_FILE);
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
    printf("SFMBKD stopped\n");
}

void handle_signal(int sig) {
    cleanup();
    exit(0);
}

int is_alive(pid_t pid) {
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    return (errno != ESRCH) ? 1 : 0;  // Chỉ trả 0 khi rõ ràng không tồn tại
}

pid_t read_pid(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) return -1;
    pid_t pid = -1;
    if (fscanf(f, "%d", &pid) != 1) pid = -1;
    fclose(f);
    return pid;
}

int main() {
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        printf("SFMBKD already running\n");
        if (lock_fd >= 0) close(lock_fd);
        exit(1);
    }

    FILE *f = fopen(PID_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }

    printf("SFMBKD started (PID: %d)\n", getpid());

    while (1) {
        pid_t sfmd_pid = read_pid("/tmp/sfmd.pid");
        if (sfmd_pid <= 0 || !is_alive(sfmd_pid)) {
            printf("SFMD crashed -> restarting...\n");
            system("./sfmd &");
            sleep(1);
        }
        sleep(2);
    }

    cleanup();
    return 0;
}*/