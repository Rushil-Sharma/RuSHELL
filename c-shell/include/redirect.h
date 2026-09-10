#ifndef REDIRECT_H
#define REDIRECT_H

int execute_with_redirection(char **args,char **input_files, int input_count,char **output_files, int *append_flags, int output_count);

typedef struct {
    char **args;          // argv-style command, NULL-terminated
    char **input_files;   // "< file" targets for this stage (usually only stage 0)
    int input_count;
    char **output_files;  // "> file" / ">> file" targets (usually only last stage)
    int *append_flags;    // parallel to output_files: 1 = append, 0 = truncate
    int output_count;
} command_stage;
 
int execute_pipeline(command_stage *stages, int num_stages);
int execute_background(command_stage *stages, int num_stages);

#endif
