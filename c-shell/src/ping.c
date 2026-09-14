#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

#include "jobs.h"
#include "ping.h"

// validates that s is a non-empty string of only decimal digits
static int is_valid_nonneg_int(const char *s) {
    if (s == NULL || *s == '\0') return 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

// ping <target> <signal_number>
void ping_command(char **args, int argc) {
    if (argc != 3) {
        printf("ping: invalid syntax\n");
        return;
    }

    const char *target = args[1];
    const char *sig_str = args[2];

    if (!is_valid_nonneg_int(sig_str)) {
        printf("ping: invalid syntax\n");
        return;
    }
    long sig_val = strtol(sig_str, NULL, 10);
    int actual_sig = (int)(sig_val % 64);

    int is_job = (target[0] == '%');
    const char *num_part = is_job ? target + 1 : target;

    // target itself must be a valid non-negative integer, optionally
    // prefixed with '%' for a job number
    if (!is_valid_nonneg_int(num_part)) {
        printf("ping: invalid syntax\n");
        return;
    }

    if (is_job) {
        int job_id = (int)strtol(num_part, NULL, 10);
        pid_t pgid;
        int stopped;
        char cmd_name[64];
        if (!group_lookup(job_id, &pgid, &stopped, cmd_name, sizeof(cmd_name))) {
            printf("ping: no such process found\n");
            return;
        }
        if (kill(-pgid, actual_sig) != 0) {
            printf("ping: no such process found\n");
            return;
        }
        printf("Sent signal %s to %s\n", sig_str, target);
    } else {
        pid_t pid = (pid_t)strtol(num_part, NULL, 10);
        if (!pid_is_tracked(pid)) {
            printf("ping: no such process found\n");
            return;
        }
        if (kill(pid, actual_sig) != 0) {
            printf("ping: no such process found\n");
            return;
        }
        printf("Sent signal %s to %s\n", sig_str, target);
    }
}