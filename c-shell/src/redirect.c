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
static int setup_input_redirection(char **input_files, int input_count,int *writer_pid_out,int (*pipefds)[2], int num_pipes) {
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
        // The helper should not keep any pipeline pipe open.
        for (int i = 0; i < num_pipes; i++) {
            close(pipefds[i][0]);
            close(pipefds[i][1]);
        }
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
static int setup_output_redirection(char **output_files, int *append_flags,int output_count, int *reader_pid_out,int (*pipefds)[2], int num_pipes) {
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
        close(pipefd[1]);
        for (int i = 0; i < num_pipes; i++) {
            close(pipefds[i][0]);
            close(pipefds[i][1]);
        }
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
        in_fd = setup_input_redirection(input_files, input_count,&writer_pid, NULL, 0);
        if (in_fd == -1) {
            free(resolved);
            return; // error already printed
        }
    }

    int out_fd = -1;
    if (output_count > 0) {
        out_fd = setup_output_redirection(output_files, append_flags,output_count, &reader_pid, NULL, 0);
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

// closes both ends of every pipe in pipefds[0..num_pipes-1]
static void close_all_pipes(int (*pipefds)[2], int num_pipes) {
    for (int i = 0; i < num_pipes; i++) {
        if (pipefds[i][0] != -1) close(pipefds[i][0]);
        if (pipefds[i][1] != -1) close(pipefds[i][1]);
    }
}

// runs a full pipeline of commands, wiring stdout of stage i to stdin of
// stage i+1 via pipe(), while still honoring any per-stage file redirection
// (e.g. "cmd1 < in.txt | cmd2 > out.txt").
void execute_pipeline(command_stage *stages, int num_stages) {
    if (num_stages <= 0) return;

    // a "pipeline" of one command is just a normal redirected command
    if (num_stages == 1) {
        execute_with_redirection(stages[0].args,
                                  stages[0].input_files, stages[0].input_count,
                                  stages[0].output_files, stages[0].append_flags, stages[0].output_count);
        return;
    }

    int num_pipes = num_stages - 1;
    int pipefds[num_pipes][2];
    for (int i = 0; i < num_pipes; i++) pipefds[i][0] = pipefds[i][1] = -1;

    // 1. create every pipe up front (requirement 1)
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipefds[i]) == -1) {
            perror("pipe");
            close_all_pipes(pipefds, num_pipes);
            return;
        }
    }

    char *resolved[num_stages];
    int writer_pid[num_stages], reader_pid[num_stages];
    int in_fd[num_stages], out_fd[num_stages];   // extra fds from file redirection, -1 if none
    int child_pid[num_stages];
    int aborted = 0;

    for (int i = 0; i < num_stages; i++) {
        resolved[i] = NULL;
        writer_pid[i] = reader_pid[i] = -1;
        in_fd[i] = out_fd[i] = -1;
        child_pid[i] = -1;
    }

    // 2. set up any file redirection each stage asks for, and resolve each
    //    command's path, before forking anything for the pipeline itself.
    //    Doing this in the parent means "command not found" / file errors
    //    print to the real terminal, never into a pipe.
    for (int i = 0; i < num_stages && !aborted; i++) {
        if (stages[i].input_count > 0) {
            in_fd[i] = setup_input_redirection(stages[i].input_files,stages[i].input_count,&writer_pid[i],pipefds, num_pipes);
            if (in_fd[i] == -1) { aborted = 1; break; } // error already printed
        }
        if (stages[i].output_count > 0) {
            out_fd[i] = setup_output_redirection(stages[i].output_files,stages[i].append_flags,stages[i].output_count,&reader_pid[i],pipefds, num_pipes);
            if (out_fd[i] == -1) { aborted = 1; break; } // error already printed
        }
        resolved[i] = resolve_command(stages[i].args[0]);
        if (resolved[i] == NULL) {
            // requirement 8: report this stage's failure, but keep going -
            // the rest of the pipeline still runs. We simply never fork a
            // process for this stage; its pipe fds get closed by the parent
            // below like normal, which sends EOF downstream correctly.
            printf("cshell: command not found (%s)\n", stages[i].args[0]);
        }
    }

    if (aborted) {
        for (int i = 0; i < num_stages; i++) {
            if (resolved[i]) free(resolved[i]);
            if (in_fd[i] != -1) close(in_fd[i]);
            if (out_fd[i] != -1) close(out_fd[i]);
            if (writer_pid[i] > 0) waitpid(writer_pid[i], NULL, 0);
            if (reader_pid[i] > 0) waitpid(reader_pid[i], NULL, 0);
        }
        close_all_pipes(pipefds, num_pipes);
        return;
    }

    // 3. fork one child per stage that resolved successfully (requirement 2)
    for (int i = 0; i < num_stages; i++) {
        if (resolved[i] == NULL) continue; // stage failed to resolve, skip it

        int pid = fork();
        if (pid == 0) {
            // stdin: file redirection > previous pipe > inherited stdin
            if (stages[i].input_count > 0) {
                dup2(in_fd[i], STDIN_FILENO);
            } else if (i > 0) {
                dup2(pipefds[i - 1][0], STDIN_FILENO); // requirement 4
            }

            // stdout: file redirection > next pipe > inherited stdout
            if (stages[i].output_count > 0) {
                dup2(out_fd[i], STDOUT_FILENO);
            } else if (i < num_stages - 1) {
                dup2(pipefds[i][1], STDOUT_FILENO); // requirement 3
            }

            // child closes every pipe fd it doesn't need anymore (requirement 6)
            close_all_pipes(pipefds, num_pipes);
            if (in_fd[i] != -1) close(in_fd[i]);
            if (out_fd[i] != -1) close(out_fd[i]);

            execv(resolved[i], stages[i].args);
            perror("execv");
            exit(1);
        } else if (pid > 0) {
            child_pid[i] = pid;
        } else {
            perror("fork");
        }
    }

    // 4. parent closes every pipe fd and file-redirection fd it holds
    //    (requirement 5), now that all children have their own copies
    close_all_pipes(pipefds, num_pipes);
    for (int i = 0; i < num_stages; i++) {
        if (in_fd[i] != -1) close(in_fd[i]);
        if (out_fd[i] != -1) close(out_fd[i]);
    }

    // 5. wait for every stage and every redirection helper (requirement 7)
    for (int i = 0; i < num_stages; i++) {
        if (child_pid[i] > 0) waitpid(child_pid[i], NULL, 0);
        if (writer_pid[i] > 0) waitpid(writer_pid[i], NULL, 0);
        if (reader_pid[i] > 0) waitpid(reader_pid[i], NULL, 0);
        if (resolved[i]) free(resolved[i]);
    }
}