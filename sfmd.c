// ================= sfmd.c =================
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include<sys/wait.h>
#include <sys/reboot.h>
#include <sys/stat.h>

#define PID_FILE "/tmp/sfmd.pid"
#define LOCK_FILE "/tmp/sfmd.lock"
#define STT_FILE "/home/intern/duong/sfm_read_write_file/sfm_cur_stt.conf"
#define CFG_FILE "/home/intern/duong/sfm_read_write_file/sfm_user_cfg.conf"
#define DEFAULT_FILE "/home/intern/duong/sfm_read_write_file/sfm_default_cfg.conf"
#define FIRST_START_SFMD "/tmp/FS.sfmd"

int lock_fd = -1;

typedef enum{
    SM = 1,
    PM
}module_type;
typedef struct{
    char name[32];
    int max_crashes;
    time_t time_window_sec;
    int crash_count;
    time_t first_start_time; 
    int fault_level;
    module_type type;
}PM_Module;

PM_Module pms[] = {
    {"Pm_1", 5, 0, 0, 0, 0, 0},
    {"Pm_2", 3, 0, 0, 0, 0, 0}
};

int num_pms = sizeof(pms) / sizeof(pms[0]);

int crash_count_sfmbkd = 0;
time_t first_start_time_sfmbkd = 0;

time_t first_start_time_sm = 0;

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
    printf("SFMD stopped\n");
}

void handle_signal(int sig){
    cleanup();
    exit(0);
}

void seconds_to_duration(time_t seconds, char *buf, size_t size) {
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

time_t convert_stime2second(char *s){
    int num = 0;
    time_t total = 0;
    for(int i = 0; s[i] != '\0'; i++){
        if(s[i] >= '0' && s[i] <= '9'){
            num = num*10 + (s[i]-'0');
        }
        else{
            if(s[i] == 'd')
                total += num * 86400;
            else if(s[i] == 'h')
                total += num * 3600;
            else if(s[i] == 'm')
                total += num * 60;
            else if(s[i] == 's')
                total += num;
            num = 0;
        }
    }
    return total;
}

void load_config(){
    char name[32], window[64];
    int max_crash;
    char tmp[10];
    FILE *f = fopen(CFG_FILE, "r");
    if (!f) {
        pid_t pid = fork();
        if(pid < 0){
            return;
        }
        if(pid == 0){
            execl("/usr/bin/touch", "touch", "sfm_default_cfg.conf", NULL);
            exit(1);
        }
        else{
            wait(NULL);
            FILE *f_default;
            f_default = fopen(DEFAULT_FILE, "a");
            for(int i = 1; i <= num_pms; i++){
                char tmp_name[32];
                memset(tmp_name, 0, sizeof(tmp_name));
                char buf[20];
                memset(buf, 0, sizeof(buf));
                int tmp = i;
                sprintf(buf, "%d",tmp);
                char Pm[10] = "Pm_";
                strcat(Pm,buf);
                strcpy(tmp_name, Pm);
                fprintf(f_default, "%s\n  %s %d %s %s\n", tmp_name, "crashed", 5, "time", "2h");
                fflush(f_default);
            }
            fclose(f_default);
            f_default = fopen(DEFAULT_FILE, "r");
            while(fscanf(f_default, "%s", name)!= -1){
                for (int i = 0; i < num_pms; i++) {
                    if (strcmp(pms[i].name, name) == 0) {
                        fscanf(f_default, "%s%d%s%s", tmp, &max_crash, tmp, window);
                        pms[i].max_crashes = max_crash;
                        pms[i].time_window_sec = convert_stime2second(window);
                        break;
                    }
                }
            }
            fclose(f_default);
        }
    }
    else{
        while(fscanf(f, "%s", name)!= -1){
            for (int i = 0; i < num_pms; i++) {
                if (strcmp(pms[i].name, name) == 0) {
                    fscanf(f, "%s%d%s%s", tmp, &max_crash, tmp, window);
                    pms[i].max_crashes = max_crash;
                    pms[i].time_window_sec = convert_stime2second(window);
                    break;
                }
            }
        }
        fclose(f);
    }
}

void load_state() {
    FILE *f = fopen(STT_FILE, "r");
    if (!f) return;

    char line[128];
    PM_Module *current = NULL;

    while (fgets(line, sizeof(line), f)) {

        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Check module name
        for (int i = 0; i < num_pms; i++) {
            if (strcmp(line, pms[i].name) == 0) {
                current = &pms[i];
                break;
            }
        }

        if (current == NULL) continue;

        // crashed line
        int crash;
        char time_str[32];
        if (sscanf(line, " crashed %d time %s", &crash, time_str) == 2) {
            current->crash_count = crash;
        }

        // fault line
        int fault;
        if (sscanf(line, " fault %d", &fault) == 1) {
            current->fault_level = fault;
        }
    }

    fclose(f);
}

void log_crash(const char *module, int crash_count, time_t uptime_sec, int fault_level){
    char duration[64];
    seconds_to_duration(uptime_sec, duration, sizeof(duration));

    FILE *f = fopen(STT_FILE, "a");
    if (f){
        if(module == "sfmbkd"){
            fprintf(f, "%s\n crashed %d time %s\n", module, crash_count, duration);
            fclose(f);
        }
        else if(module == "Sm"){
            fprintf(f, "%s\n crashed %d time %s\n", module, crash_count, duration);
            fclose(f);
        }
        else{
            if(fault_level == 0){
                fprintf(f, "%s\n crashed %d time %s\n", module, crash_count, duration);
                fclose(f);
            }
            else{
                fprintf(f, "%s\n crashed %d time %s\n", module, crash_count, duration);
                fprintf(f, "fault %d\n", fault_level);
                fclose(f);
            }
        }
    }
}

void reboot_device(){
    pid_t pid = fork();
    if(pid < 0) return;
    if(pid == 0){
        execl("/usr/bin/rm", "rm", STT_FILE, NULL);
        exit(1);
    }
    else{
        wait(NULL);
        sync();
        sleep(1);
        reboot(RB_AUTOBOOT);
    }
}

int is_alive(pid_t pid) {
    if (pid <= 0) return 0;
    if(kill(pid, 0) == 0) return 1;
    return 0;
}

pid_t read_pid(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) return -1;
    pid_t pid = -1;
    fscanf(f, "%d", &pid);
    fclose(f);
    return pid;
}

int main() {
    daemonize();
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        printf("SFMD already running\n");
        if (lock_fd >= 0) close(lock_fd);
        exit(1);
    }

    FILE *fpid = fopen(PID_FILE, "w");
    if (fpid){ 
        fprintf(fpid, "%d\n", getpid()); 
        fclose(fpid); 
    }

    FILE *fs_sfmd = fopen(FIRST_START_SFMD, "r");
    if (fs_sfmd == NULL) {
        first_start_time_sfmd = time(NULL);
        printf("First start SFMD: %ld\n", first_start_time_sfmd);
        fs_sfmd = fopen(FIRST_START_SFMD, "w");
        if (fs_sfmd) {
            fprintf(fs_sfmd, "%ld", first_start_time_sfmd);
            fclose(fs_sfmd);
        }
    } else {
        fscanf(fs_sfmd, "%ld", &first_start_time_sfmd);
        fclose(fs_sfmd);
        printf("Read start time from file: %ld\n", first_start_time_sfmd);
    }

    load_config();
    load_state();

    while (1) {
        time_t now = time(NULL);

  /*      for (int i = 0; i < num_pms; i++) {
            if (pms[i].fault_level >= 2) {
                continue; 
            }
            char pidfile[64];
            sprintf(pidfile, "/tmp/%s.pid", pms[i].name);

            pid_t pid = read_pid(pidfile);

            if (pid <= 0 || !is_alive(pid)) {
                if (pms[i].first_start_time == 0) {
                    pms[i].first_start_time = now;
                }
                time_t uptime = now - pms[i].first_start_time;
                if(pms[i].crash_count <= pms[i].max_crashes && (uptime > pms[i].time_window_sec)){
                    pms[i].crash_count = 0;
                    //xoa trang thai cap nhat trong file sfm_cur_stt.conf
                }
                pms[i].crash_count++;
                if(pms[i].crash_count <= pms[i].max_crashes && (uptime <= pms[i].time_window_sec)){
                    log_crash(pms[i].name, pms[i].crash_count, uptime, pms[i].fault_level);
                    char cmd[100];
                    sprintf(cmd, "/home/intern/duong/sfm_read_write_file/%s &", pms[i].name);
                    system(cmd);
                    sleep(1);
                    //cap nhat crash_cnt va crash_time vao file sfm_cur_stt.conf
                }

                if(pms[i].crash_count > pms[i].max_crashes && (uptime <= pms[i].time_window_sec)){
                    pms[i].fault_level++;
                    log_crash(pms[i].name, pms[i].crash_count, uptime, pms[i].fault_level);
                    //cap nhat fault_level vao file sfm_cur_stt.conf
                    pms[i].crash_count = 0;
                    if(pms[i].fault_level < 2){
                        printf("Module %s Fault level 1\n", pms[i].name);
                        char cmd[100];
                        sprintf(cmd, "/home/intern/duong/sfm_read_write_file/%s &", pms[i].name);
                        system(cmd);
                        sleep(1);
                    }
                    else{
                        printf("Fault = 2 khong khoi dong lai nua\n");
                    }
                }
            }
        }
*/
        for (int i = 0; i < num_pms; i++) {

            if (pms[i].fault_level >= 2) {
                continue;
            }

            char pidfile[64];
            sprintf(pidfile, "/tmp/%s.pid", pms[i].name);

            pid_t pid = read_pid(pidfile);

            if (pid <= 0 || !is_alive(pid)) {

                if (pms[i].first_start_time == 0){ //cach tot nhat de giu first_start_time khong bị reset
                    pms[i].first_start_time = now; //khi sfmd restart la luu vao file, roi moi lan sfmd restart
                }                                  //thi doc file roi gan lai first_start_time bang gia tri luu trong file    

                time_t uptime = now - pms[i].first_start_time;
                if (uptime > pms[i].time_window_sec) {
                    pms[i].crash_count = 0;
                    pms[i].first_start_time = now;
                }

                pms[i].crash_count++;

                if (pms[i].crash_count > pms[i].max_crashes) {
                    pms[i].fault_level++;
                    log_crash(pms[i].name, pms[i].crash_count, uptime, pms[i].fault_level);
                    pms[i].crash_count = 0;
                    if (pms[i].fault_level >= 2) {
                        printf("Fault = 2 khong khoi dong lai nua\n");
                        continue; 
                    }
                    printf("Module %s Fault level 1\n", pms[i].name);
                }
                if (pms[i].fault_level < 2) {
                    log_crash(pms[i].name, pms[i].crash_count, uptime, pms[i].fault_level);
                    printf("Module %s restart\n", pms[i].name);
                    char cmd[100];
                    sprintf(cmd, "/home/intern/duong/sfm_read_write_file/%s &", pms[i].name);
                    system(cmd);
                    sleep(1);
                }
            }
        }

        pid_t bk_pid = read_pid("/tmp/sfmbkd.pid");
        if (bk_pid <= 0 || !is_alive(bk_pid)) {
            if(first_start_time_sfmbkd == 0){
                first_start_time_sfmbkd = now;
            }
            int uptime_sfmbkd = now - first_start_time_sfmbkd;
            crash_count_sfmbkd++;
            log_crash("sfmbkd", crash_count_sfmbkd, uptime_sfmbkd, 0);
            system("/home/intern/duong/sfm_read_write_file/sfmbkd &");
            sleep(1);
        }

        /*pid_t Sm_pid = read_pid("/tmp/Sm.pid");
        if (Sm_pid <= 0 || !is_alive(Sm_pid)) {
            if(first_start_time_sm == 0){
                first_start_time_sm = now;
            }
            int uptime_sm = now - first_start_time_sm;
            log_crash("Sm", 1, uptime_sm, 0);
            reboot_device();
            sleep(1);
        }*/

        sleep(2);
    }

    cleanup();
    return 0;
}
