#ifndef PIT_PARSER_H
#define PIT_PARSER_H

#include "lexer.h"
#include "runtime.h"

typedef struct {
    pit_lex_token token;
    i64 start, end;
} pit_parser_token_info;

typedef struct {
    pit_lexer *lexer;
    pit_parser_token_info cur, next;
} pit_parser;

pit_parser *pit_parser_from_lexer(pit_lexer *lex);
pit_value pit_parse(pit_runtime *rt, pit_parser *st);

#endif
