#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "jobs.h"

#define MAX_JOBS 4096
#define MAX_PENDING 256
#define MSG_LEN 256
#define MAX_PROCS 8192

typedef struct {
    int job_id;
    pid_t pid;
    char cmd_name[64];
    int active; // 1 = still running / not yet reported
} bg_job;

typedef struct {
    pid_t pgid;
    int job_id;
    int active; // group has at least one running member
    int stopped;
} proc_group;

typedef struct {
    pid_t pgid;
    pid_t pid;
    char cmd_name[64];
    int active;
} proc_entry;

static bg_job jobs[MAX_JOBS];
static volatile sig_atomic_t job_count = 0;
// volatile is just for asynchronously checking and also prevents compiler optimisations
static volatile sig_atomic_t next_job_number = 1;

static proc_group groups[MAX_JOBS];
static volatile sig_atomic_t group_count = 0;

static proc_entry procs[MAX_PROCS];
static volatile sig_atomic_t proc_count = 0;

static volatile sig_atomic_t fg_active = 0;
static volatile sig_atomic_t at_prompt = 0;
static pid_t shell_pgid;

static char pending_msgs[MAX_PENDING][MSG_LEN];
static volatile sig_atomic_t pending_count = 0;

static int find_job_index_by_pid(pid_t pid){
    for (int i=0;i<job_count;i++){
        if (jobs[i].pid == pid && jobs[i].active) return i;
    }
    return -1;
}

// mark a tracked process entry as no longer running, so activities stops listing it once it has exited
static void mark_proc_exited(pid_t pid){
    for (int i = 0; i < proc_count; i++){
        if (procs[i].pid == pid && procs[i].active){
            procs[i].active = 0;
            return;
        }
    }
}

static void sigchld_handler(int sig){// automatic
    (void)sig;
    int saved_errno = errno; // save for later
    int status;
    pid_t pid;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0){ // reap all children
        // status now stores the return status for child in waitpid
        mark_proc_exited(pid); // remove from activities regardless of tracking

        int idx = find_job_index_by_pid(pid);
        if (idx < 0) continue; // untracked helper process, just reap it

        jobs[idx].active = 0; // mark non active 

        char buf[MSG_LEN];
        int len = 0;
        if (WIFEXITED(status)) {
            len = snprintf(buf, sizeof(buf), "%s with pid %d exited normally\n",jobs[idx].cmd_name, (int)pid);
        } else if (WIFSIGNALED(status)) {
            len = snprintf(buf, sizeof(buf), "%s with pid %d exited abnormally\n",jobs[idx].cmd_name, (int)pid);
        }
        if (len <= 0) continue;

        if (fg_active) {
            if (pending_count < MAX_PENDING) {
                memcpy(pending_msgs[pending_count], buf, (size_t)len + 1); // low level and fast
                pending_count++;
            }
        } else {
            if (at_prompt) write(STDOUT_FILENO, "\n", 1);
            write(STDOUT_FILENO, buf, (size_t)len);
        }
    }
    errno = saved_errno;
}

void jobs_init(void){
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
}

// shared helper to block SIGCHLD while touching the job/group/proc tables from outside the handler, same pattern job_add already used
static sigset_t block_sigchld(void){
    sigset_t block, prev;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &prev);
    return prev;
}

int job_add(pid_t pid, const char *cmd_name){
    sigset_t block,prev;
    sigemptyset(&block); // blocks the child process while we are adding the job
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &prev);

    int job_id = -1;
    if(job_count < MAX_JOBS){
        int idx = job_count;
        jobs[idx].pid = pid;
        jobs[idx].active = 1;
        strncpy(jobs[idx].cmd_name, cmd_name, sizeof(jobs[idx].cmd_name) - 1);
        jobs[idx].cmd_name[sizeof(jobs[idx].cmd_name) - 1] = '\0';
        jobs[idx].job_id = next_job_number++;
        job_count++;
        job_id = jobs[idx].job_id;
    }

    sigprocmask(SIG_SETMASK, &prev, NULL);
    return job_id;
}

// register a new process group (job) using pgid as its identifying pid.
// job_id is taken as next_job_number so it lines up with whatever job_add assigns right after this is called for the same launch.
void group_add(pid_t pgid){
    sigset_t prev = block_sigchld();
    if (group_count < MAX_JOBS){
        groups[group_count].pgid = pgid;
        groups[group_count].job_id = next_job_number;
        groups[group_count].active = 1;
        groups[group_count].stopped = 0;
        group_count++;
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);
}

// register one process (pid) as belonging to group pgid, with its
// command name, so activities can list it under that group
void group_add_member(pid_t pgid, pid_t pid, const char *cmd_name){
    sigset_t prev = block_sigchld();
    if (proc_count < MAX_PROCS){
        procs[proc_count].pgid = pgid;
        procs[proc_count].pid = pid;
        strncpy(procs[proc_count].cmd_name, cmd_name, sizeof(procs[proc_count].cmd_name) - 1);
        procs[proc_count].cmd_name[sizeof(procs[proc_count].cmd_name) - 1] = '\0';
        procs[proc_count].active = 1;
        proc_count++;
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);
}

void set_fg_active(int active) { fg_active = active; }
void set_at_prompt(int flag)   { at_prompt = flag; }

void flush_pending_bg_messages(void) {
    if (pending_count == 0) return;
    write(STDOUT_FILENO, "\n", 1);
    for (int i = 0; i < pending_count; i++) {
        write(STDOUT_FILENO, pending_msgs[i], strlen(pending_msgs[i]));
    }
    pending_count = 0;
}

// reads /proc/<pid>/stat to get the process's current state character ('R' running, 'S' sleeping, 'D' disk wait, 'T' stopped, 'Z' zombie...).
// returns 0 if the process no longer exists (already exited).
static char proc_state(pid_t pid){
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    fclose(f);
    char *rparen = strrchr(line, ')');
    if (!rparen || rparen[1] == '\0' || rparen[2] == '\0') return 0;
    return rparen[2];
}

// implements the activities builtin prints every group the shell
// has spawned (oldest first), and every still-running process under it dropping anything that has already exited.
void activities_print(void){
    sigset_t prev = block_sigchld();

    for (int g = 0; g < group_count; g++){
        if (!groups[g].active) continue;

        // does this group still have any live member?
        int has_live = 0;
        for (int i = 0; i < proc_count; i++){
            if (procs[i].pgid == groups[g].pgid && procs[i].active &&
                proc_state(procs[i].pid) != 0){
                has_live = 1;
                break;
            }
        }
        if (!has_live) { groups[g].active = 0; continue; }

        printf("[%d] pgid %d\n", groups[g].job_id, (int)groups[g].pgid);

        for (int i = 0; i < proc_count; i++){
            if (procs[i].pgid != groups[g].pgid || !procs[i].active) continue;

            char st = proc_state(procs[i].pid);
            if (st == 0) { procs[i].active = 0; continue; } // exited, drop it

            const char *state_str = "Running";
            if (st == 'T' || groups[g].stopped) state_str = "Stopped";

            printf("  %d %s %s\n", (int)procs[i].pid, procs[i].cmd_name, state_str);
        }
    }

    sigprocmask(SIG_SETMASK, &prev, NULL);
}

static void sigint_handler(int sig) { 
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}
static void sigtstp_handler(int sig) { (void)sig; }

void terminal_init(void) {
    shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    struct sigaction sa_int, sa_tstp;

    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    sa_tstp.sa_handler = sigtstp_handler;
    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    signal(SIGTTOU, SIG_IGN);
}

void give_terminal_to(pid_t pgid) {
    tcsetpgrp(STDIN_FILENO, pgid);
}

void reclaim_terminal(void) {
    tcsetpgrp(STDIN_FILENO, shell_pgid);
}

int mark_group_stopped(pid_t pgid) {
    sigset_t prev = block_sigchld();
    int job_id = -1;
    for (int g = 0; g < group_count; g++) {
        if (groups[g].pgid == pgid && groups[g].active) {
            groups[g].stopped = 1;
            job_id = groups[g].job_id;
            break;
        }
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);
    return job_id;
}

int has_stopped_jobs(void) {
    sigset_t prev = block_sigchld();
    int found = 0;
    for (int g = 0; g < group_count; g++) {
        if (groups[g].active && groups[g].stopped) { found = 1; break; }
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);
    return found;
}

void hangup_all_jobs(void) {
    sigset_t prev = block_sigchld();
    for (int g = 0; g < group_count; g++) {
        if (groups[g].active) {
            kill(-groups[g].pgid, SIGHUP);
        }
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);
}

int group_lookup(int job_id, pid_t *pgid, int *stopped, char *cmd_name, size_t cmd_name_len) {
    sigset_t prev = block_sigchld();
    int found = 0;
    for (int g = 0; g < group_count; g++) {
        if (groups[g].active && groups[g].job_id == job_id) {
            *pgid = groups[g].pgid;
            *stopped = groups[g].stopped;
            found = 1;
            // grab the leader's (first-registered member's) cmd_name for printing
            cmd_name[0] = '\0';
            for (int i = 0; i < proc_count; i++) {
                if (procs[i].pgid == groups[g].pgid) {
                    strncpy(cmd_name, procs[i].cmd_name, cmd_name_len - 1);
                    cmd_name[cmd_name_len - 1] = '\0';
                    break;
                }
            }
            break;
        }
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);
    return found;
}

void mark_group_running(pid_t pgid) {
    sigset_t prev = block_sigchld();
    for (int g = 0; g < group_count; g++) {
        if (groups[g].pgid == pgid && groups[g].active) {
            groups[g].stopped = 0;
            break;
        }
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);
}

void group_remove(pid_t pgid) {
    sigset_t prev = block_sigchld();
    for (int g = 0; g < group_count; g++) {
        if (groups[g].pgid == pgid && groups[g].active) {
            groups[g].active = 0;
            break;
        }
    }
    for (int i = 0; i < proc_count; i++) {
        if (procs[i].pgid == pgid) procs[i].active = 0;
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);
}