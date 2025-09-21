#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "utils.h"
#include "lexer.h"
#include "types.h"

const char *PIT_LEX_TOKEN_NAMES[PIT_LEX_TOKEN__SENTINEL] = {
    [PIT_LEX_TOKEN_EOF] = "eof",
    [PIT_LEX_TOKEN_LPAREN] = "lparen",
    [PIT_LEX_TOKEN_RPAREN] = "rparen",
    [PIT_LEX_TOKEN_DOT] = "dot",
    [PIT_LEX_TOKEN_QUOTE] = "quote",
    [PIT_LEX_TOKEN_INTEGER_LITERAL] = "integer_literal",
    [PIT_LEX_TOKEN_STRING_LITERAL] = "string_literal",
    [PIT_LEX_TOKEN_SYMBOL] = "symbol",
};

const char *pit_lex_token_name(pit_lex_token t) {
    return PIT_LEX_TOKEN_NAMES[t];
}

static bool is_more_input(pit_lexer *st) {
    return st && st->end < st->len;
}

static int is_symchar(int c) {
    return c != '(' && c != ')' && c != '.' && c != '\'' && c != '"' && isprint(c) && !isspace(c);
}

static char peek(pit_lexer *st) {
    if (is_more_input(st)) return st->input[st->end];
    else return 0;
}

static char advance(pit_lexer *st) {
    if (is_more_input(st)) return st->input[st->end++];
    else return 0;
}

static bool match(pit_lexer *st, int (*f)(int)) {
    if (f(peek(st))) {
        st->end += 1;
        return true;
    } else return false;
}

pit_lexer *pit_lex_file(char *path) {
    pit_lexer *ret = malloc(sizeof(*ret));
    FILE *f = fopen(path, "r");
    if (!f) pit_panic("failed to open file for lexing: %s", path);
    fseek(f, 0, SEEK_END);
    ret->len = ftell(f);
    fseek(f, 0, SEEK_SET);
    ret->input = calloc(ret->len, sizeof(char));
    fread(ret->input, sizeof(char), ret->len, f);
    ret->start = 0;
    ret->end = 0;
    return ret;
}

pit_lex_token pit_lex_next(pit_lexer *st) {
restart:
    st->start = st->end;
    char c = advance(st);
    switch (c) {
    case 0: return PIT_LEX_TOKEN_EOF;
    case ';': while (is_more_input(st) && advance(st) != '\n'); goto restart;
    case '(': return PIT_LEX_TOKEN_LPAREN;
    case ')': return PIT_LEX_TOKEN_RPAREN;
    case '.': return PIT_LEX_TOKEN_DOT;
    case '\'': return PIT_LEX_TOKEN_QUOTE;
    case '"':
        while (peek(st) != '"') {
            if (peek(st) == '\\') advance(st); // skip escaped characters
            if (!advance(st)) pit_panic("unterminated string starting at: %d", st->start);
        }
        advance(st);
        return PIT_LEX_TOKEN_STRING_LITERAL;
    default:
        if (isspace(c)) goto restart;
        if (isdigit(c)) { while (match(st, isdigit)); return PIT_LEX_TOKEN_INTEGER_LITERAL; }
        else { while (match(st, is_symchar)); return PIT_LEX_TOKEN_SYMBOL; }
    }
}
