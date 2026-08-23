#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prompt.h"
#include "lexer.h"
#include "parser.h"
#include "hop.h"
#include "reveal.h"
#include "locate.h"

#define STRING_SIZE 4096

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
        }
        if(strlen(command) == 0) continue;
        // Hop
        char *argv[100];
        int argc = 0;
        char *word = strtok(command, " \t");
        while (word != NULL && argc < 99) {
            argv[argc++] = word;
            word = strtok(NULL, " \t");
        }
        argv[argc] = NULL;
        if(strcmp(argv[0],"hop") == 0) hop(argc, argv,home_directory);
        else if(strcmp(argv[0],"reveal") == 0) reveal_command(argv,argc,home_directory);
        else if(strcmp(argv[0], "locate") == 0) locate(argv,argc);

        tokenlist_free(&tokens);
    }
    // free(command);
    free(home_directory);
    return 0;
}