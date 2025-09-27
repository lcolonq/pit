#ifndef LEXER_H
#define LEXER_H

#include "types.h"

typedef enum {
    PIT_LEX_TOKEN_ERROR=-1,
    PIT_LEX_TOKEN_EOF=0,
    PIT_LEX_TOKEN_LPAREN,
    PIT_LEX_TOKEN_RPAREN,
    PIT_LEX_TOKEN_DOT,
    PIT_LEX_TOKEN_QUOTE,
    PIT_LEX_TOKEN_INTEGER_LITERAL,
    PIT_LEX_TOKEN_STRING_LITERAL,
    PIT_LEX_TOKEN_SYMBOL,
    PIT_LEX_TOKEN__SENTINEL,
} pit_lex_token;

typedef struct {
    char *input;
    i64 len; // length of input
    i64 start, end; // bounds of the current token
    i64 line, column; // for error reporting only; current line and column
    i64 start_line, start_column; // for error reporting only; line and column of token start
    char *error;
} pit_lexer;

void pit_lex_bytes(pit_lexer *ret, char *buf, i64 len);
void pit_lex_file(pit_lexer *ret, char *path);
pit_lex_token pit_lex_next(pit_lexer *st);
const char *pit_lex_token_name(pit_lex_token t);

#endif
