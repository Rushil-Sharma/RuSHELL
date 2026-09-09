#include <stdio.h>
#include <stdlib.h>
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

#define STRING_SIZE 4096
#define MAX_STAGES 64
#define MAX_ARGS 100

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
    while(1){
    //Definations
        char *prompt = NULL;

    //Code
        //print prompt
        prompt = prompt_printer(); 
        printf("%s ",prompt); // prints the prompt 

        //Take command
        if(fgets(command, STRING_SIZE, stdin) == NULL){
            printf("\n");
            break;
        }
        int scan_val = strlen(command);
        if(scan_val == -1){
            printf("\n");
            break;
        }
        if(scan_val > 0 && command[scan_val - 1] == '\n'){
            command[scan_val - 1] = '\0'; // removes \n puts \0
        }
        if(strcmp(command,"exit()") == 0){
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

        // ---- split the tokens into pipeline stages on TOK_PIPE ----
        char *stage_argv[MAX_STAGES][MAX_ARGS];
        char *stage_input_files[MAX_STAGES][MAX_ARGS];
        int stage_input_count[MAX_STAGES];
        char *stage_output_files[MAX_STAGES][MAX_ARGS];
        int stage_append_flags[MAX_STAGES][MAX_ARGS];
        int stage_output_count[MAX_STAGES];
        int num_stages = 0;
        int pipeline_syntax_error = 0;

        {
            Token *cur = tokens.head;
            int s = 0;
            int argc = 0;
            stage_input_count[s] = 0;
            stage_output_count[s] = 0;

            while (cur != NULL && cur->type != TOK_EOF) {
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
                } else if (cur->type == TOK_SEMI || cur->type == TOK_AMP) {
                    break;
                } else {
                    // unreachable given parse_validate(), kept defensively
                    pipeline_syntax_error = 1;
                    break;
                }
            }

            if (!pipeline_syntax_error) {
                stage_argv[s][argc] = NULL;
                if (argc == 0) {
                    pipeline_syntax_error = 1;
                } else {
                    num_stages = s + 1;
                }
            }
        }

        if (pipeline_syntax_error) {
            printf("cshell: invalid syntax\n");
            tokenlist_free(&tokens);
            continue;
        }

        // single command, no "|" -> keep old built-in handling intact
        if (num_stages == 1) {
            char **argv = stage_argv[0];
            int argc = 0;
            while (argv[argc] != NULL) argc++;

            if(strcmp(argv[0],"hop") == 0) hop(argc, argv,home_directory);
            else if(strcmp(argv[0],"reveal") == 0) reveal_command(argv,argc,home_directory);
            else if(strcmp(argv[0], "locate") == 0) locate(argv,argc);
            else if(strcmp(argv[0],"peek") == 0) peek_command(argc,argv);
            else execute_with_redirection(argv, stage_input_files[0], stage_input_count[0],stage_output_files[0], stage_append_flags[0], stage_output_count[0]);
        } else {
            // pipeline
            command_stage stages[MAX_STAGES];
            for (int s = 0; s < num_stages; s++) {
                stages[s].args = stage_argv[s];
                stages[s].input_files = stage_input_files[s];
                stages[s].input_count = stage_input_count[s];
                stages[s].output_files = stage_output_files[s];
                stages[s].append_flags = stage_append_flags[s];
                stages[s].output_count = stage_output_count[s];
            }
            execute_pipeline(stages, num_stages);
        }

        tokenlist_free(&tokens);
    }
    return 0;
}