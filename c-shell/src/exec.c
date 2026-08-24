#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "exec.h"

// checks if a path is a runnable regular file
int is_exec(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    if (access(path, X_OK) != 0) return 0;
    return 1;
}

// resolves cmd to a full path following the rules
// returns a malloc string on success, NULL if nothing found
char *resolve_command(const char *cmd) {
    char full_path[2048];
    // has a slash = treat as literal path, no searching
    if (strchr(cmd, '/') != NULL) {
        if (is_exec(cmd)) return strdup(cmd); // for duplication (so that the same string is not changed later)
        return NULL;
    }
    // %name -> skip cwd, go straight to PATH
    int skip_cwd = 0;
    if (cmd[0] == '%') {
        skip_cwd = 1;
        cmd++; // removes %
    }
    // check cwd first, unless skipping
    if (!skip_cwd) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(full_path, sizeof(full_path), "%s/%s", cwd, cmd);
            if (is_exec(full_path)) return strdup(full_path);
        }
    }

    // fall back to PATH
    char *path_env = getenv("PATH");
    if (path_env == NULL) return NULL;
    // printf("%s\n",path_env);
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":"); //splits on :
    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);
        if (is_exec(full_path)) {
            free(path_copy);
            return strdup(full_path);
        }
        dir = strtok(NULL, ":");
    }
    free(path_copy);
    return NULL;
}

// runs a command with its args (args[0] = command, NULL terminated)
void execute_command(char **args) {
    char *resolved = resolve_command(args[0]);
    if (resolved == NULL) {
        printf("cshell: command not found (%s)\n", args[0]);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execv(resolved, args);
        perror("execv");
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } else {
        perror("fork");
    }

    free(resolved);
}