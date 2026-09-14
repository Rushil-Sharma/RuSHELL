#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#include "snoop.h"

#define MAX_SYSCALLS 512

typedef struct {
    long sys_no;
    long calls;
    double total_time;
    int first_order;
} syscall_stat;

static syscall_stat stats[MAX_SYSCALLS];
static int stat_count = 0;
static int order_counter = 0;

// minimal x86_64 syscall number -> name lookup table (common ones)
static const char *syscall_name(long num) {
    switch (num) {
        case 0: return "read";
        case 1: return "write";
        case 2: return "open";
        case 3: return "close";
        case 4: return "stat";
        case 5: return "fstat";
        case 6: return "lstat";
        case 8: return "lseek";
        case 9: return "mmap";
        case 10: return "mprotect";
        case 11: return "munmap";
        case 12: return "brk";
        case 13: return "rt_sigaction";
        case 14: return "rt_sigprocmask";
        case 21: return "access";
        case 22: return "pipe";
        case 32: return "dup";
        case 33: return "dup2";
        case 39: return "getpid";
        case 41: return "socket";
        case 56: return "clone";
        case 57: return "fork";
        case 59: return "execve";
        case 60: return "exit";
        case 61: return "wait4";
        case 62: return "kill";
        case 78: return "getdents";
        case 79: return "getcwd";
        case 89: return "readlink";
        case 97: return "getrlimit";
        case 102: return "getuid";
        case 158: return "arch_prctl";
        case 186: return "gettid";
        case 218: return "set_tid_address";
        case 231: return "exit_group";
        case 232: return "epoll_wait";
        case 257: return "openat";
        case 262: return "newfstatat";
        case 273: return "set_robust_list";
        case 302: return "prlimit64";
        case 318: return "getrandom";
        case 35:  return "nanosleep";
        case 230: return "clock_nanosleep";
        case 217: return "getdents64";
        case 334: return "rseq";
        case 17:  return "pread64";
        case 16:  return "ioctl";
        case 63:  return "uname";
        default: return NULL;
    }
}

static double timespec_diff(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

// finds or creates the stat slot for a given syscall number
static syscall_stat *get_stat_slot(long sys_no) {
    for (int i = 0; i < stat_count; i++) {
        if (stats[i].sys_no == sys_no) return &stats[i];
    }
    if (stat_count >= MAX_SYSCALLS) return NULL;
    stats[stat_count].sys_no = sys_no;
    stats[stat_count].calls = 0;
    stats[stat_count].total_time = 0.0;
    stats[stat_count].first_order = order_counter++;
    return &stats[stat_count++];
}

static int compare_stats(const void *a, const void *b) {
    const syscall_stat *sa = a, *sb = b;
    if (sa->calls != sb->calls) return (int)(sb->calls - sa->calls); // descending calls
    return sa->first_order - sb->first_order; // ties: first-occurrence order
}

// runs PTRACE_SYSCALL stepping loop on an already-stopped/traced pid,
// recording per-syscall entry/exit timing until the tracee exits
static void trace_loop(pid_t pid) {
    int in_syscall = 0; // toggles entry vs exit stop
    long cur_sys_no = -1;
    struct timespec entry_time;
    int status;
    int sig = 0;

    while (1) {
        if (ptrace(PTRACE_SYSCALL, pid, NULL, (void *)(long)sig) == -1) break;
        sig = 0;
        if (waitpid(pid, &status, 0) == -1) break;

        if (WIFEXITED(status) || WIFSIGNALED(status)) break;

        if (WIFSTOPPED(status)) {
            int stopsig = WSTOPSIG(status);
            if (stopsig == (SIGTRAP | 0x80)) {
                struct user_regs_struct regs;
                if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == -1) break;

                if (!in_syscall) {
                    // syscall entry
                    cur_sys_no = regs.orig_rax;
                    clock_gettime(CLOCK_MONOTONIC, &entry_time);
                    in_syscall = 1;
                } else {
                    // syscall exit
                    struct timespec exit_time;
                    clock_gettime(CLOCK_MONOTONIC, &exit_time);
                    syscall_stat *slot = get_stat_slot(cur_sys_no);
                    if (slot) {
                        slot->calls++;
                        slot->total_time += timespec_diff(&entry_time, &exit_time);
                    }
                    in_syscall = 0;
                }
            } else if (stopsig != SIGTRAP) {
                sig = stopsig;
            }
        }
    }
}

static void print_summary(void) {
    qsort(stats, stat_count, sizeof(syscall_stat), compare_stats);
    printf("syscall       calls   time\n");
    for (int i = 0; i < stat_count; i++) {
        const char *name = syscall_name(stats[i].sys_no);
        char namebuf[64];
        if (name == NULL) {
            snprintf(namebuf, sizeof(namebuf), "syscall_%ld", stats[i].sys_no);
            name = namebuf;
        }
        printf("%-12s  %-6ld  %.3fs\n", name, stats[i].calls, stats[i].total_time);
    }
}

static int is_valid_pid_str(const char *s) {
    if (s == NULL || *s == '\0') return 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

void snoop_command(char **args, int argc) {
    stat_count = 0;
    order_counter = 0;

    if (argc >= 3 && strcmp(args[1], "-p") == 0) {
        if (!is_valid_pid_str(args[2])) {
            printf("snoop: no such process\n");
            return;
        }
        pid_t pid = (pid_t)atoi(args[2]);

        char procpath[64];
        snprintf(procpath, sizeof(procpath), "/proc/%d", pid);
        struct stat st;
        if (stat(procpath, &st) != 0) {
            printf("snoop: no such process\n");
            return;
        }

        if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
            if (errno == EPERM) {
                printf("snoop: permission denied\n");
            } else {
                printf("snoop: no such process\n");
            }
            return;
        }

        int status;
        waitpid(pid, &status, 0); // wait for the attach-stop

        ptrace(PTRACE_SETOPTIONS, pid, NULL, (void *)(long)PTRACE_O_TRACESYSGOOD);

        trace_loop(pid);
        print_summary();
        return;
    }

    if (argc < 2) {
        printf("snoop: command not found\n");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        execvp(args[1], args + 1);
        // execvp only returns on failure
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0); // wait for the initial exec-stop (SIGTRAP)

        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
            printf("snoop: command not found\n");
            return;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            // process exited/died before or exactly at the trace point
            printf("snoop: command not found\n");
            return;
        }

        ptrace(PTRACE_SETOPTIONS, pid, NULL, (void *)(long)PTRACE_O_TRACESYSGOOD);

        trace_loop(pid);
        print_summary();
    } else {
        printf("snoop: command not found\n");
    }
}