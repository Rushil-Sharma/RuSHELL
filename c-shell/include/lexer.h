#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    TOK_WORD,
    TOK_PIPE,  /* |  */
    TOK_AMP,   /* &  */
    TOK_SEMI,  /* ;  */
    TOK_LT,    /* <  */
    TOK_GT,    /* >  */
    TOK_GTGT,  /* >> */
    TOK_EOF
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

/*
 * Lexes a single input line into a TokenList (terminated internally by
 * a TOK_EOF sentinel appended as the tail).
 *
 * Returns 0 on success.
 * Returns -1 on a lexical error, in which case "cshell: invalid syntax"
 * has already been printed to stdout and *out has been freed/reset.
 */
int lex_line(const char *line, TokenList *out);

#endif /* LEXER_H */
