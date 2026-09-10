#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include "jobs.h"

#define MAX_JOBS 4096
#define MAX_PENDING 256
#define MSG_LEN 256

typedef struct {
    int job_id;
    pid_t pid;
    char cmd_name[64];
    int active; // 1 = still running / not yet reported
} bg_job;

static bg_job jobs[MAX_JOBS];
static volatile sig_atomic_t job_count = 0;
// volatile is just for asynchronously checking and also prevents compiler optimisations
static volatile sig_atomic_t next_job_number = 1;

static volatile sig_atomic_t fg_active = 0;
static volatile sig_atomic_t at_prompt = 0;

static char pending_msgs[MAX_PENDING][MSG_LEN];
static volatile sig_atomic_t pending_count = 0;

static int find_job_index_by_pid(pid_t pid){
    for (int i=0;i<job_count;i++){
        if (jobs[i].pid == pid && jobs[i].active) return i;
    }
    return -1;
}

static void sigchld_handler(int sig){// automatic
    (void)sig;
    int saved_errno = errno; // save for later
    int status;
    pid_t pid;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0){ // reap all children
        // status now stores the return status for child in waitpid
        int idx = find_job_index_by_pid(pid);
        if (idx < 0) continue; // untracked helper process - just reap it

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