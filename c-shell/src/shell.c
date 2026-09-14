#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "prompt.h"
#include "lexer.h"
#include "parser.h"
#include "hop.h"
#include "reveal.h"
#include "locate.h"
#include "exec.h"
#include "redirect.h"
#include "peek.h"
#include "jobs.h"
#include "resume.h"
#include "ping.h"
#include "history.h"
#include "spy.h"

#define STRING_SIZE 4096
#define MAX_STAGES 64
#define MAX_ARGS 100

static int is_builtin(const char *name) {
    if(strcmp(name,"hop") == 0 || strcmp(name,"reveal") == 0 || strcmp(name,"locate") == 0 || strcmp(name,"peek") == 0 || strcmp(name,"activities") == 0 || strcmp(name,"resume") == 0 || strcmp(name,"ping") == 0 || strcmp(name,"spy")) return 1;
    return 0;
}
static int prev_ctrld = 0;

int main(){
    // Print the welcome text
    printf("\x1b[34m"
        "┌────────────────────────────────────────────────────────────────┐\n"
        "│__________       _________ ___ ______________.____    .____     │\n"
        "│\\______   \\__ __/   _____//   |   \\_   _____/|    |   |    |    │\n"
        "│ |       _/  |  \\_____  \\/    ~    \\    __)_ |    |   |    |    │\n"
        "│ |    |   \\  |  /        \\    Y    /        \\|    |___|    |___ │\n"
        "│ |____|_  /____/_______  /\\___|_  /_______  /|_______ \\_______ \\│\n"
        "│        \\/             \\/       \\/        \\/         \\/       \\/│\n"
        "└────────────────────────────────────────────────────────────────┘\n"
    "\x1b[0m");
    //INIT THE HOME DIR
    char* home_directory;
    home_directory = prompt_init();
    char command[STRING_SIZE];
    // Clear hop_history & pre_dir.txt;
    FILE *file_ptr = fopen("./hop_history.txt", "w");
    fclose(file_ptr);
    FILE *file_ptr2 = fopen("./prev_dir.txt", "w");
    fclose(file_ptr2);
    
    jobs_init(); // initializing SIGCHLD handler for background jobs
    terminal_init();
    history_init();
    while(1){
    //Definations
        char *prompt = NULL;

    //Code
        //print prompt
        prompt = prompt_printer(); 
        printf("%s ",prompt); // prints the prompt 
        fflush(stdout);
        set_at_prompt(1); // shell is at prompt, waiting for user

        //Take command
        if(history_read_line(command, STRING_SIZE, prompt) == NULL){
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            set_at_prompt(0);
            if (has_stopped_jobs() && !prev_ctrld) {
                printf("\ncshell: there are stopped jobs\n");
                prev_ctrld = 1;
                clearerr(stdin);
                continue;
            }
            hangup_all_jobs();
            printf("\n");
            break;
        }
        prev_ctrld = 0;
        int scan_val = strlen(command);
        if(scan_val == -1){
            printf("\n");
            break;
        }
        if(scan_val > 0 && command[scan_val - 1] == '\n'){
            command[scan_val - 1] = '\0'; // removes \n puts \0
        }
        if(strcmp(command,"exit()") == 0){
            hangup_all_jobs();
            printf("Bye bye ...\n");
            return 0;
        }

        // Lexer
        TokenList tokens;
        if(lex_line(command, &tokens) != 0) continue; // goto alloc_fail

        // Parser
        if(!parse_validate(&tokens)){
            printf("cshell: invalid syntax\n");
            tokenlist_free(&tokens);
            continue;
        }
        if(strlen(command) == 0){
            tokenlist_free(&tokens);
            continue;
        }


        Token *seg_start = tokens.head;

        while (seg_start != NULL && seg_start->type != TOK_EOF) {

            char *stage_argv[MAX_STAGES][MAX_ARGS];
            char *stage_input_files[MAX_STAGES][MAX_ARGS];
            int stage_input_count[MAX_STAGES];
            char *stage_output_files[MAX_STAGES][MAX_ARGS];
            int stage_append_flags[MAX_STAGES][MAX_ARGS];
            int stage_output_count[MAX_STAGES];
            int num_stages = 0;
            int pipeline_syntax_error = 0;

            Token *cur = seg_start;
            int s = 0;
            int argc = 0;
            stage_input_count[s] = 0;
            stage_output_count[s] = 0;

            // parse tokens for THIS segment only: stop at TOK_SEMI/TOK_AMP/TOK_EOF
            while (cur != NULL && cur->type != TOK_EOF &&
                   cur->type != TOK_SEMI && cur->type != TOK_AMP) {

                if (cur->type == TOK_PIPE) {
                    stage_argv[s][argc] = NULL;
                    if (argc == 0) { pipeline_syntax_error = 1; break; }
                    s++;
                    if (s >= MAX_STAGES) { pipeline_syntax_error = 1; break; }
                    argc = 0;
                    stage_input_count[s] = 0;
                    stage_output_count[s] = 0;
                    cur = cur->next;
                } else if (cur->type == TOK_LT) {
                    cur = cur->next; // parser guarantees this is TOK_WORD
                    if (stage_input_count[s] < MAX_ARGS - 1) {
                        stage_input_files[s][stage_input_count[s]++] = cur->value;
                    }
                    cur = cur->next;
                } else if (cur->type == TOK_GT) {
                    cur = cur->next; // parser guarantees this is TOK_WORD
                    if (stage_output_count[s] < MAX_ARGS - 1) {
                        stage_output_files[s][stage_output_count[s]] = cur->value;
                        stage_append_flags[s][stage_output_count[s]] = 0; // truncate
                        stage_output_count[s]++;
                    }
                    cur = cur->next;
                } else if (cur->type == TOK_GTGT) {
                    cur = cur->next; // parser guarantees this is TOK_WORD
                    if (stage_output_count[s] < MAX_ARGS - 1) {
                        stage_output_files[s][stage_output_count[s]] = cur->value;
                        stage_append_flags[s][stage_output_count[s]] = 1; // append
                        stage_output_count[s]++;
                    }
                    cur = cur->next;
                } else if (cur->type == TOK_WORD) {
                    if (argc < MAX_ARGS - 1) {
                        stage_argv[s][argc++] = cur->value;
                    }
                    cur = cur->next;
                } else {
                    // unreachable given parse_validate(), kept defensively
                    pipeline_syntax_error = 1;
                    break;
                }
            }

            if (!pipeline_syntax_error) {
                stage_argv[s][argc] = NULL;
                if (argc == 0) {
                    // e.g. trailing ";" with nothing before EOF - just stop quietly
                    num_stages = 0;
                } else {
                    num_stages = s + 1;
                }
            }

            if (pipeline_syntax_error) {
                printf("cshell: invalid syntax\n");
                break; // stop the whole line
            }
            int is_bg = (cur != NULL && cur->type == TOK_AMP); // checks if the ending is & (background process)

            if (num_stages > 0) {
                command_stage stages[MAX_STAGES];
                for (int i = 0; i < num_stages; i++) {
                    stages[i].args = stage_argv[i];
                    stages[i].input_files = stage_input_files[i];
                    stages[i].input_count = stage_input_count[i];
                    stages[i].output_files = stage_output_files[i];
                    stages[i].append_flags = stage_append_flags[i];
                    stages[i].output_count = stage_output_count[i];
                }

                char **argv0 = stage_argv[0];
                // fprintf(stderr, "DEBUG: is_bg=%d num_stages=%d argv0=%s\n", is_bg, num_stages, argv0[0]);
                if (is_bg && !(num_stages == 1 && is_builtin(argv0[0]))) {
                    // background path
                    execute_background(stages, num_stages);
                } else {
                    set_fg_active(1);
                    int exec_status = 0;

                    if (num_stages == 1) {
                        int cmd_argc = 0;
                        while (argv0[cmd_argc] != NULL) cmd_argc++;

                        if(strcmp(argv0[0],"hop") == 0) hop(cmd_argc, argv0, home_directory);
                        else if(strcmp(argv0[0],"reveal") == 0) reveal_command(argv0, cmd_argc, home_directory);
                        else if(strcmp(argv0[0], "locate") == 0) locate(argv0, cmd_argc);
                        else if(strcmp(argv0[0],"peek") == 0) peek_command(cmd_argc, argv0);
                        else if(strcmp(argv0[0], "activities") == 0) activities_print();
                        else if(strcmp(argv0[0],"resume") == 0) resume_command(argv0,cmd_argc);
                        else if(strcmp(argv0[0],"ping") == 0) ping_command(argv0, cmd_argc);
                        else if(strcmp(argv0[0],"spy") == 0) spy_command(argv0, cmd_argc);
                        else exec_status = execute_with_redirection(argv0, stage_input_files[0],stage_input_count[0], stage_output_files[0],stage_append_flags[0], stage_output_count[0]);
                    } else {
                        exec_status = execute_pipeline(stages, num_stages);
                    }

                    set_fg_active(0);
                    flush_pending_bg_messages();

                    if (exec_status < 0) break; //stop the rest of the sequence
                }
            }

            if (cur != NULL && (cur->type == TOK_SEMI || cur->type == TOK_AMP)) {
                seg_start = cur->next;
            } else {
                seg_start = NULL;
            }
        }

        tokenlist_free(&tokens);
    }

    return 0;
}