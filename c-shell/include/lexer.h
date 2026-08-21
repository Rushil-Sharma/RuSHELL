#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    TOK_WORD,
    TOK_PIPE,  // |
    TOK_AMP,   // & 
    TOK_SEMI,  // ;  
    TOK_LT,    // <  
    TOK_GT,    // >  
    TOK_GTGT,  // >> 
    TOK_EOF // end of file token
} TokenType;

typedef struct Token {
    TokenType type;
    char *value;
    struct Token *next;
} Token;

typedef struct {
    Token *head;
    Token *tail;
    int count;
} TokenList;

void tokenlist_init(TokenList *list);
void tokenlist_free(TokenList *list);

int lex_line(const char *line, TokenList *out);

#endif /* LEXER_H */