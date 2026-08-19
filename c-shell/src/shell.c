#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prompt.h"
#include "lexer.h"
#include "parser.h"

#define STRING_SIZE 4096

int main(){
    //INIT THE HOME DIR
    prompt_init();
    char command[STRING_SIZE];
    while(1){
    //Definations
        char *prompt = NULL;

    //Code
        //print prompt
        prompt = prompt_printer(); 
        printf("%s",prompt); // prints the prompt

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
            printf("Bye bye ...");
            return 0;
        }

        // Lexer
        TokenList tokens;
        if(lex_line(command, &tokens) != 0) continue; // goto alloc_fail

        // Parser
        if(!parse_validate(&tokens)){
            printf("cshell: invalid syntax\n");
        }

        tokenlist_free(&tokens);
    }
    // free(command);
    return 0;
}