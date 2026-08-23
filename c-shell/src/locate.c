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
        char *path_copy = strdup(path_env);
        char *dir = strtok(path_copy, ":");
        while (dir != NULL) {
            char full_path[2048];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, filename);
            if (is_executable(full_path)) {
                printf("%s\n", full_path);
                found = 1;
            }
            dir = strtok(NULL, ":");
        }
        free(path_copy);

        if (!found) {
            printf("locate: command not found (%s)\n", filename);
        }
    }
}