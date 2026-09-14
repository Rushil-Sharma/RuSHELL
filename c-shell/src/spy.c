#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

#include "spy.h"

// converts a stat mode into lsof-style type strings
static const char *type_from_mode(mode_t mode) {
    if (S_ISREG(mode))  return "REG";
    if (S_ISDIR(mode))  return "DIR";
    if (S_ISCHR(mode))  return "CHR";
    if (S_ISBLK(mode))  return "BLK";
    if (S_ISFIFO(mode)) return "FIFO";
    if (S_ISLNK(mode))  return "LINK";
    if (S_ISSOCK(mode)) return "SOCK";
    return "unknown";
}

// resolves path via readlink(), fills into out (size outsize).
// returns 1 on success, 0 on failure.
static int resolve_link(const char *linkpath, char *out, size_t outsize) {
    ssize_t n = readlink(linkpath, out, outsize - 1);
    if (n < 0) return 0;
    out[n] = '\0';
    return 1;
}

// prints one row, deriving TYPE from stat() on linkpath or resolved path
static void print_entry(const char *pid_str, const char *fd_label, const char *path, const char *linkpath) {
    struct stat st;
    const char *type = "unknown";
    if (linkpath && stat(linkpath, &st) == 0) {
        type = type_from_mode(st.st_mode);
    } else if (stat(path, &st) == 0) {
        type = type_from_mode(st.st_mode);
    } else if (strncmp(path, "pipe:", 5) == 0) {
        type = "FIFO";
    } else if (strncmp(path, "socket:", 7) == 0) {
        type = "SOCK";
    } else if (strstr(path, "(deleted)")) {
        type = "REG";
    }
    printf("%s    %-3s  %s    %s\n", pid_str, fd_label, type, path);
}

// checks whether a pid corresponds to a running process by seeing if /proc/<pid> exists
static int pid_exists(const char *pid_str) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%s", pid_str);
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

// validates pid_str is a plain non-negative integer string
static int is_valid_pid_str(const char *s) {
    if (s == NULL || *s == '\0') return 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

void spy_command(char **args, int argc) {
    if (argc > 2) {
        printf("spy: invalid syntax\n");
        return;
    }

    char pid_str[32];
    if (argc == 2) {
        if (!is_valid_pid_str(args[1])) {
            printf("spy: invalid syntax\n");
            return;
        }
        strncpy(pid_str, args[1], sizeof(pid_str) - 1);
        pid_str[sizeof(pid_str) - 1] = '\0';
    } else {
        snprintf(pid_str, sizeof(pid_str), "%d", (int)getpid());
    }

    if (!pid_exists(pid_str)) {
        printf("spy: no such process\n");
        return;
    }

    printf("PID    FD    TYPE   PATH\n");

    char procdir[64];
    char linkpath[256];
    char resolved[1024];

    // cwd
    snprintf(procdir, sizeof(procdir), "/proc/%s", pid_str);
    snprintf(linkpath, sizeof(linkpath), "%s/cwd", procdir);
    if (resolve_link(linkpath, resolved, sizeof(resolved))) {
        print_entry(pid_str, "cwd", resolved, linkpath);
    }

    // txt (executable)
    snprintf(linkpath, sizeof(linkpath), "%s/exe", procdir);
    if (resolve_link(linkpath, resolved, sizeof(resolved))) {
        print_entry(pid_str, "txt", resolved, linkpath);
    }

    // mem: memory-mapped files, deduplicated, from /proc/<pid>/maps
    snprintf(linkpath, sizeof(linkpath), "%s/maps", procdir);
    FILE *maps = fopen(linkpath, "r");
    if (maps) {
        char line[1024];
        char **seen = NULL;
        int seen_count = 0, seen_cap = 0;

        while (fgets(line, sizeof(line), maps)) {
            // maps line format: addr perms offset dev inode pathname
            char *path_part = strchr(line, '/');
            if (!path_part) continue; // no associated file (anonymous mapping)

            // strip trailing newline
            size_t plen = strlen(path_part);
            while (plen > 0 && (path_part[plen - 1] == '\n' || path_part[plen - 1] == '\r')) {
                path_part[--plen] = '\0';
            }
            if (plen == 0) continue;

            // skip pseudo-paths like [heap], [stack], (deleted) already stripped by '/' check;
            // still guard against empty/duplicate entries
            int dup = 0;
            for (int i = 0; i < seen_count; i++) {
                if (strcmp(seen[i], path_part) == 0) { dup = 1; break; }
            }
            if (dup) continue;

            if (seen_count >= seen_cap) {
                seen_cap = (seen_cap == 0) ? 64 : seen_cap * 2;
                seen = realloc(seen, (size_t)seen_cap * sizeof(char *));
            }
            seen[seen_count++] = strdup(path_part);
            print_entry(pid_str, "mem", path_part, NULL);
        }
        for (int i = 0; i < seen_count; i++) free(seen[i]);
        free(seen);
        fclose(maps);
    }

    // numeric fds: /proc/<pid>/fd/*
    snprintf(linkpath, sizeof(linkpath), "%s/fd", procdir);
    DIR *fddir = opendir(linkpath);
    if (fddir) {
        struct dirent *ent;
        char fdnums[4096][16];
        int fdcount = 0;
        while ((ent = readdir(fddir)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            if (!is_valid_pid_str(ent->d_name)) continue;
            if (fdcount < 4096) {
                strncpy(fdnums[fdcount], ent->d_name, sizeof(fdnums[fdcount]) - 1);
                fdnums[fdcount][sizeof(fdnums[fdcount]) - 1] = '\0';
                fdcount++;
            }
        }
        closedir(fddir);

        // sort numerically for stable, readable output
        for (int i = 0; i < fdcount; i++) {
            for (int j = i + 1; j < fdcount; j++) {
                if (atoi(fdnums[i]) > atoi(fdnums[j])) {
                    char tmp[16];
                    strcpy(tmp, fdnums[i]);
                    strcpy(fdnums[i], fdnums[j]);
                    strcpy(fdnums[j], tmp);
                }
            }
        }

        for (int i = 0; i < fdcount; i++) {
            snprintf(linkpath, sizeof(linkpath), "%s/fd/%s", procdir, fdnums[i]);
            if (resolve_link(linkpath, resolved, sizeof(resolved))) {
                print_entry(pid_str, fdnums[i], resolved, linkpath);
            }
        }
    }
}