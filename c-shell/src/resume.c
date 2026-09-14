#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#include "jobs.h"
#include "resume.h"

static volatile sig_atomic_t timed_out = 0;

static void alarm_handler(int sig) {
    (void)sig;
    timed_out = 1;
}

// parses "%<number>" -> job id, returns -1 on bad format
static int parse_job_arg(const char *s) {
    if (s == NULL || s[0] != '%') return -1;
    const char *num = s + 1;
    if (*num == '\0') return -1;
    char *end;
    long val = strtol(num, &end, 10);
    if (*end != '\0' || val <= 0) return -1;
    return (int)val;
}

// resume %N (fg [--timeout <seconds>] | bg)
void resume_command(char **args, int argc) {
    // args[0] = "resume"
    if (argc < 3) {
        printf("resume: invalid syntax\n");
        return;
    }

    int job_id = parse_job_arg(args[1]);
    if (job_id == -1) {
        printf("resume: invalid syntax\n");
        return;
    }

    int is_fg;
    if (strcmp(args[2], "fg") == 0) {
        is_fg = 1;
    } else if (strcmp(args[2], "bg") == 0) {
        is_fg = 0;
    } else {
        printf("resume: invalid syntax\n");
        return;
    }

    int timeout_secs = 0;
    int has_timeout = 0;

    if (argc > 3) {
        if (!is_fg) {
            // --timeout only valid with fg
            printf("resume: invalid syntax\n");
            return;
        }
        if (strcmp(args[3], "--timeout") != 0 || argc < 5) {
            printf("resume: invalid syntax\n");
            return;
        }
        char *end;
        long val = strtol(args[4], &end, 10);
        if (*end != '\0' || val <= 0) {
            printf("resume: invalid syntax\n");
            return;
        }
        timeout_secs = (int)val;
        has_timeout = 1;

        if (argc > 5) {
            printf("resume: invalid syntax\n");
            return;
        }
    }

    pid_t pgid;
    int stopped;
    char cmd_name[64];
    if (!group_lookup(job_id, &pgid, &stopped, cmd_name, sizeof(cmd_name))) {
        printf("resume: no such job\n");
        return;
    }

    // Send SIGCONT to the whole process group either way
    kill(-pgid, SIGCONT);
    mark_group_running(pgid);

    if (!is_fg) {
        // bg: mark running, print, return to prompt immediately
        printf("[%d] + Running\t%s\n", job_id, cmd_name);
        fflush(stdout);
        return;
    }

    // fg: print command line, give terminal, wait
    printf("%s\n", cmd_name);
    fflush(stdout);

    set_fg_active(1);
    give_terminal_to(pgid);

    timed_out = 0;
    struct sigaction sa, old_sa;
    if (has_timeout) {
        sa.sa_handler = alarm_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGALRM, &sa, &old_sa);
        alarm((unsigned int)timeout_secs);
    }

    int status = 0;
    int any_stopped = 0;
    pid_t wpid;
    while (1) {
        wpid = waitpid(-pgid, &status, WUNTRACED);
        if (wpid > 0) {
            if (WIFSTOPPED(status)) {
                any_stopped = 1;
                break;
            }
            continue;
        }
        if (wpid == -1 && errno == EINTR) {
            if (has_timeout && timed_out) break;
            continue;
        }
        if (wpid == -1) break; // ECHILD or similar
    }

    if (has_timeout) {
        if (!timed_out) {
            alarm(0); // cancel pending timer: job finished/stopped on its own
        }
        sigaction(SIGALRM, &old_sa, NULL);
    }

    if (has_timeout && timed_out) {
        kill(-pgid, SIGTERM);
        kill(-pgid, SIGCONT);
        while (waitpid(-pgid, &status, 0) > 0);
        printf("resume: job timed out\n");
        fflush(stdout);
        reclaim_terminal();
        set_fg_active(0);
        group_remove(pgid); // terminated -> remove from job list entirely
        flush_pending_bg_messages();
        return;
    }

    reclaim_terminal();
    set_fg_active(0);

    if (any_stopped || WIFSTOPPED(status)) {
        int jid = mark_group_stopped(pgid);
        printf("[%d] + Stopped\t%s\n", jid, cmd_name);
    } else {
        group_remove(pgid);
    }
    // if it exited normally, the SIGCHLD handler already/will mark it
    // inactive and print the exit message via the normal async path
    flush_pending_bg_messages();
}