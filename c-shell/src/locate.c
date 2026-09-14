#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "locate.h"

int is_executable(const char* path){
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    if (access(path, X_OK) != 0) return 0;
    return 1;
}

void locate(char **args, int argc) {
    if (argc < 2) {
        printf("locate: invalid syntax\n");
        return;
    }

    char *path_env = getenv("PATH");
    if (path_env == NULL) path_env = "";

    for (int i = 1; i < argc; i++) {
        char *filename = args[i];
        int found = 0;
        // check cwd first
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            char full_path[2048];
            snprintf(full_path, sizeof(full_path), "%s/%s", cwd, filename);
            if (is_executable(full_path)) {
                printf("%s\n", full_path);
                found = 1;
            }
        }

        // then check each dir in PATH
        const char *p = path_env;
        while (1) {
            const char *colon = strchr(p, ':');
            char dir[1024];
            if (colon) {
                size_t len = colon - p;
                if (len >= sizeof(dir)) len = sizeof(dir) - 1;
                strncpy(dir, p, len);
                dir[len] = '\0';
            } else {
                strncpy(dir, p, sizeof(dir) - 1);
                dir[sizeof(dir) - 1] = '\0';
            }
            char full_path[4096];
            if (dir[0] == '\0') {
                snprintf(full_path, sizeof(full_path), "%s/%s", cwd, filename);
            } else if (dir[0] != '/') {
                snprintf(full_path, sizeof(full_path), "%s/%s/%s", cwd, dir, filename);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", dir, filename);
            }
            if (is_executable(full_path)) {
                printf("%s\n", full_path);
                found = 1;
            }
            if (!colon) break;
            p = colon + 1;
        }

        if (!found) {
            printf("locate: command not found (%s)\n", filename);
        }
    }
}