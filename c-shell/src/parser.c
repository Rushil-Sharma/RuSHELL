#include <stdio.h>
#include "parser.h"


// return value = 1(valid)/ 0(invalid)
int parse_validate(const TokenList *tokens){
    const Token *curr = tokens->head; // pointer to head first

    if(curr == NULL || curr->type  == TOK_EOF) return 1;
    if(curr->type != TOK_WORD) return 0; 

    curr = curr->next; // valid transition

    while(1){
        if(curr == NULL || curr->type  == TOK_EOF) return 1;

        if(curr->type == TOK_WORD) curr = curr->next;
        else if(curr->type == TOK_LT || curr->type == TOK_GT || curr->type == TOK_GTGT){
            // redirection <, > and >>.
            curr = curr->next;
            if(curr == NULL || curr->type != TOK_WORD) return 0;
            curr = curr->next; // as it is tok_word anyway.

        }
        else if(curr->type == TOK_PIPE || curr->type == TOK_SEMI){
            curr = curr->next;
            if(curr == NULL || curr->type != TOK_WORD) return 0;
            curr = curr->next;
        }
        else if(curr->type == TOK_AMP){
            // BG operator
            curr = curr->next;
            if(curr == NULL || curr->type == TOK_EOF) return 1;
            if(curr->type != TOK_WORD) return 0;
            curr = curr->next;
        }
        else return 0;

    }
}