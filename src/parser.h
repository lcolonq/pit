#ifndef PIT_PARSER_H
#define PIT_PARSER_H

#include "lexer.h"
#include "runtime.h"

typedef struct {
    pit_lex_token token;
    i64 start, end;
    i64 line, column; /* for error reporting */
} pit_parser_token_info;

typedef struct {
    pit_lexer *lexer;
    pit_parser_token_info cur, next;
} pit_parser;

void pit_parser_from_lexer(pit_parser *ret, pit_lexer *lex);
pit_value pit_parse(pit_runtime *rt, pit_parser *st, bool *eof);

#endif
