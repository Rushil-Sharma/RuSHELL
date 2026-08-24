#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#include "redirect.h"

// checks if a path is a runnable regular file
static int is_exec(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    if (access(path, X_OK) != 0) return 0;
    return 1;
}

// generates a full path out of the given command
static char *resolve_command(const char *cmd) {
    char full_path[2048];
    if (strchr(cmd, '/') != NULL) { // there exists '/' in our command
        if (is_exec(cmd)) return strdup(cmd); // duplicate and return
        return NULL;
    }
    int skip_cwd = 0;
    if (cmd[0] == '%') { // if % only search in PATH
        skip_cwd = 1;
        cmd++;
    }
    if (!skip_cwd) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(full_path, sizeof(full_path), "%s/%s", cwd, cmd);
            if (is_exec(full_path)) return strdup(full_path);
        }
    }
    char *path_env = getenv("PATH");
    if (path_env == NULL) return NULL;
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":"); // split it into tokens seperated by :
    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);
        if (is_exec(full_path)) {
            free(path_copy);
            return strdup(full_path);
        }
        dir = strtok(NULL, ":"); // next token before :
    }
    free(path_copy);
    return NULL;
}

// writes all n bytes in buf to fd, looping through partial writes
// returns 0 on success, -1 on error
static int write_all(int fd, const char *buf, int n) {
    int written = 0;
    while (written < n) {
        int w = write(fd, buf + written, n - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += (int)w;
    }
    return 0;
}

// opens every input file in order and streams contents into a pipe so that multiple "< f1 < f2" behave like one continuous stdin
// then returns read end of pipe, or -1 on error
static int setup_input_redirection(char **input_files, int input_count, int *writer_pid_out) {
    *writer_pid_out = -1;

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }

    int fds[input_count];
    for (int i = 0; i < input_count; i++) {
        fds[i] = open(input_files[i], O_RDONLY);
        if (fds[i] == -1) {
            printf("cshell: no such file or directory\n");
            for (int j = 0; j < i; j++) close(fds[j]);
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }
    }

    int pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        char buf[4096];
        for (int i = 0; i < input_count; i++) {
            int n;
            while ((n = read(fds[i], buf, sizeof(buf))) > 0) {
                write_all(pipefd[1], buf, (int)n);
            }
            close(fds[i]);
        }
        close(pipefd[1]);
        exit(0);
    }

    for (int i = 0; i < input_count; i++) close(fds[i]);
    close(pipefd[1]);

    *writer_pid_out = pid;
    return pipefd[0];
}

// opens every output file with the right mode (truncate or append).
// returns 0 on success (fds filled in out_fds), -1 on error (nothing left open)
static int open_output_files(char **output_files, int *append_flags, int output_count, int *out_fds) {
    for (int i = 0; i < output_count; i++) {
        int flags = O_WRONLY | O_CREAT | (append_flags[i] ? O_APPEND : O_TRUNC);
        out_fds[i] = open(output_files[i], flags, 0644);
        if (out_fds[i] == -1) {
            printf("cshell: unable to create file for writing\n");
            for (int j = 0; j < i; j++) close(out_fds[j]);
            return -1;
        }
    }
    return 0;
}

// reads everything the command writes from a pipe and dupes it into every output file
// runs as its own process so the command can just write to one fd without knowing about other files
static int setup_output_redirection(char **output_files, int *append_flags, int output_count, int *reader_pid_out) {
    *reader_pid_out = -1;

    int out_fds[output_count];
    if (open_output_files(output_files, append_flags, output_count, out_fds) != 0) {
        return -1;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        for (int i = 0; i < output_count; i++) close(out_fds[i]);
        return -1;
    }

    int pid = fork();
    if (pid == 0) {
        close(pipefd[1]); // reader doesn't need the write end
        char buf[4096];
        int n;
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
            for (int i = 0; i < output_count; i++) {
                write_all(out_fds[i], buf, (int)n);
            }
        }
        close(pipefd[0]);
        for (int i = 0; i < output_count; i++) close(out_fds[i]);
        exit(0);
    }

    // parent needs the write end of the pipe
    for (int i = 0; i < output_count; i++) close(out_fds[i]);
    close(pipefd[0]);

    *reader_pid_out = pid;
    return pipefd[1]; // command writes its stdout here
}

// runs args[0] with input files and output files
void execute_with_redirection(char **args,char **input_files, int input_count,char **output_files, int *append_flags, int output_count) {
    char *resolved = resolve_command(args[0]);
    if (resolved == NULL) {
        printf("cshell: command not found (%s)\n", args[0]);
        return;
    }

    int writer_pid = -1, reader_pid = -1;
    int in_fd = -1;
    if (input_count > 0) {
        in_fd = setup_input_redirection(input_files, input_count, &writer_pid);
        if (in_fd == -1) {
            free(resolved);
            return; // error already printed
        }
    }

    int out_fd = -1;
    if (output_count > 0) {
        out_fd = setup_output_redirection(output_files, append_flags, output_count, &reader_pid);
        if (out_fd == -1) {
            if (in_fd != -1) close(in_fd);
            if (writer_pid > 0) waitpid(writer_pid, NULL, 0);
            free(resolved);
            return; // error already printed
        }
    }

    int pid = fork();
    if (pid == 0) {
        if (in_fd != -1) {
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }
        if (out_fd != -1) {
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }
        execv(resolved, args);
        perror("execv");
        exit(1);
    } else if (pid > 0) {
        int status;
        if (in_fd != -1) close(in_fd);
        if (out_fd != -1) close(out_fd);
        waitpid(pid, &status, 0);
        // reap only the specific helper processes we spawned for this command
        if (writer_pid > 0) waitpid(writer_pid, NULL, 0);
        if (reader_pid > 0) waitpid(reader_pid, NULL, 0);
    } else {
        perror("fork");
        if (in_fd != -1) close(in_fd);
        if (out_fd != -1) close(out_fd);
        if (writer_pid > 0) waitpid(writer_pid, NULL, 0);
        if (reader_pid > 0) waitpid(reader_pid, NULL, 0);
    }

    free(resolved);
}