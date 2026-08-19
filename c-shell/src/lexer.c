#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

// Check functions
static int is_special(char c){
    if(c == '|' || c == '&' || c == '>' || c == '<' || c == ';') return 1;
    return 0;
}
static int is_space(char c){
    if(c == ' ' || c == '\t' || c == '\n' || c == '\r') return 1;
    else return 0;
}

// LL helpers
void tokenlist_init(TokenList *list){
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}
void tokenlist_free(TokenList *list){
    Token *cur = list->head;
    while (cur != NULL){
        Token *next = cur->next;
        free(cur->value);
        free(cur);
        cur = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}
static int tokenlist_append(TokenList *list, TokenType type, char *value){
    Token *tok = malloc(sizeof(Token));
    if(tok == NULL){
        return -1;
    }
    tok->type = type;
    tok->value = value;
    tok->next = NULL;

    if(list->tail == NULL){
        list->head = tok;
        list->tail = tok;
    } else {
        list->tail->next = tok;
        list->tail = tok;
    }
    list->count++;
    return 0;
}

// Error message
static void lex_error(void){
    printf("cshell: invalid syntax\n");
}


// variable word buffer
typedef struct {
    char *data;
    int len;
    int cap;
} Buf;
static void buf_init(Buf *b){
    b->cap = 32;
    b->len = 0;
    b->data = malloc(b->cap);
    if(b->data != NULL){
        b->data[0] = '\0';
    }
}
static int buf_push(Buf *b, char c){
    if(b->data == NULL){
        return -1;
    }
    if(b->len + 1 >= b->cap){
        int new_cap = b->cap * 2;
        char *tmp = realloc(b->data, new_cap);
        if(tmp == NULL){
            return -1;
        }
        b->data = tmp;
        b->cap = new_cap;
    }
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
    return 0;
}

// word reading
    // 0 = good, -1 = bad
static int read_word(const char* line, int n, int *i,Buf *buf){
    while(*i < n){
        char c = line[*i]; 

        if(is_space(c) || is_special(c)) break; // the word has ended
        
        if(c == '\\'){
            if(*i >= n-1){
                // case: it occurs in the last
                return -1; // invalid string
            }
            if(buf_push(buf, line[*i + 1]) != 0){
                return -1;
            }
            *i += 2; // to ignore the space character
            continue;
        }        
        
        if(c == '"'){
            (*i)++; // opening double-quote
            while (*i < n && line[*i] != '"'){
                if(line[*i] == '\\'){
                    if(*i + 1 >= n){
                        return -1; // unclosed double-quote
                    }
                    char nc = line[*i + 1]; // next char
                    if(nc == '"' || nc == '\\'){
                        // \" or \\ case 
                        if(buf_push(buf, nc) != 0){
                            return -1;
                        }
                    } else {
                        // \{char} case 
                        if(buf_push(buf, '\\') != 0){
                            return -1; 
                        }
                        if(buf_push(buf, nc) != 0){
                            return -1;
                        }
                    }
                    *i += 2;
                } else {
                    // normal case (inside double-quotes)
                    if(buf_push(buf, line[*i]) != 0){
                        return -1;
                    }
                    (*i)++;
                }
            }
            if(*i >= n){
                return -1; // unclosed
            }
            (*i)++; // closed = happy
            continue;
        }

        if(c == '\''){
            (*i) ++; // open quote
            while(*i < n && line[*i] != '\''){
                if(buf_push(buf, line[*i]) != 0) return -1;
                (*i) ++;
            }
            if(*i >= n) return -1; //sentence ends without closing
            (*i) ++;
            continue;
        }

        if(buf_push(buf, line[*i]) != 0) return -1;
        (*i) ++;
    }

    return 0; // valid sentence
}

int lex_line(const char* line, TokenList* out){
    tokenlist_init(out);

    int n = strlen(line);
    int i = 0;

    while(i<n){
        char c = line[i];

        if(is_space(c)){
            i ++;
            continue;
        }

        if(c == '|'){
            if(tokenlist_append(out, TOK_PIPE, NULL) != 0) goto alloc_fail;
            i++;
            continue;
        }
        if(c == '&'){
            if(tokenlist_append(out, TOK_AMP, NULL) != 0) goto alloc_fail;
            i++;
            continue;
        }
        if(c == ';'){
            if(tokenlist_append(out, TOK_SEMI, NULL) != 0) goto alloc_fail;
            i++;
            continue;
        }
        if(c == '<'){
            if(tokenlist_append(out, TOK_LT, NULL) != 0) goto alloc_fail;
            i++;
            continue;
        }
        if(c == '>'){
            if(i + 1 < n && line[i + 1] == '>'){
                if(tokenlist_append(out, TOK_GTGT, NULL) != 0) goto alloc_fail;
                i += 2;
            } else {
                if(tokenlist_append(out, TOK_GT, NULL) != 0) goto alloc_fail;
                i += 1;
            }
            continue;
        }
        //start word
        Buf buf;
        buf_init(&buf);
        if(buf.data == NULL) {
            goto alloc_fail;
        }
        if(read_word(line, n, &i, &buf) != 0) {
            free(buf.data);
            tokenlist_free(out);
            lex_error();
            return -1;
        }
        if(tokenlist_append(out, TOK_WORD, buf.data) != 0) {
            free(buf.data);
            goto alloc_fail;
        }
    }

    if (tokenlist_append(out, TOK_EOF, NULL) != 0) {
        goto alloc_fail;
    }   

    return 0;

alloc_fail:
    tokenlist_free(out);
    fprintf(stderr, "RuSHELL: internal error: out of memory\n");
    return -1;
}

// int main(){
    
//     return 0;
// }