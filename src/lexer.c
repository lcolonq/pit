#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "utils.h"
#include "lexer.h"
#include "types.h"

const char *PIT_LEX_TOKEN_NAMES[PIT_LEX_TOKEN__SENTINEL] = {
    /* [PIT_LEX_TOKEN_EOF] = */ "eof",
    /* [PIT_LEX_TOKEN_LPAREN] = */ "lparen",
    /* [PIT_LEX_TOKEN_RPAREN] = */ "rparen",
    /* [PIT_LEX_TOKEN_DOT] = */ "dot",
    /* [PIT_LEX_TOKEN_QUOTE] = */ "quote",
    /* [PIT_LEX_TOKEN_INTEGER_LITERAL] = */ "integer_literal",
    /* [PIT_LEX_TOKEN_STRING_LITERAL] = */ "string_literal",
    /* [PIT_LEX_TOKEN_SYMBOL] = */ "symbol",
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
    if (is_more_input(st)) {
        char ret = st->input[st->end++];
        if (ret == '\n') {
            st->line += 1;
            st->column = 0;
        } else {
            st->column += 1;
        }
        return ret;
    }
    else return 0;
}

static bool match(pit_lexer *st, int (*f)(int)) {
    if (f(peek(st))) {
        advance(st);
        return true;
    } else return false;
}

void pit_lex_cstr(pit_lexer *ret, char *buf) {
    ret->input = buf;
    ret->len = (i64) strlen(buf);
    ret->start = 0;
    ret->end = 0;
    ret->line = ret->start_line = 1;
    ret->column = ret->start_column = 0;
    ret->error = NULL;
}

void pit_lex_bytes(pit_lexer *ret, char *buf, i64 len) {
    ret->len = len;
    ret->input = buf;
    ret->start = 0;
    ret->end = 0;
    ret->line = ret->start_line = 1;
    ret->column = ret->start_column = 0;
    ret->error = NULL;
}
void pit_lex_file(pit_lexer *ret, char *path) {
    FILE *f = fopen(path, "r");
    i64 len = 0;
    char *buf = NULL;
    if (f == NULL) {
        pit_panic("failed to open file for lexing: %s", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = calloc((size_t) ret->len, sizeof(char));
    if ((size_t) ret->len != fread(ret->input, sizeof(char), (size_t) ret->len, f)) {
        pit_panic("failed to read file for lexing: %s", path);
        return;
    }
    fclose(f);
    pit_lex_bytes(ret, buf, len);
}

pit_lex_token pit_lex_next(pit_lexer *st) {
    char c = 0;
restart:
    st->start = st->end;
    st->start_line = st->line;
    st->start_column = st->column;
    c = advance(st);
    switch (c) {
    case 0: return PIT_LEX_TOKEN_EOF;
    case ';': while (is_more_input(st) && advance(st) != '\n'); goto restart;
    case '(': return PIT_LEX_TOKEN_LPAREN;
    case ')': return PIT_LEX_TOKEN_RPAREN;
    case '.': return PIT_LEX_TOKEN_DOT;
    case '\'': return PIT_LEX_TOKEN_QUOTE;
    case '"':
        while (peek(st) != '"') {
            if (peek(st) == '\\') advance(st); /* skip escaped characters */
            if (!advance(st)) {
                st->error = "unterminated string";
                return PIT_LEX_TOKEN_ERROR;
            }
        }
        advance(st);
        return PIT_LEX_TOKEN_STRING_LITERAL;
    default:
        if (isspace(c)) goto restart;
        if (isdigit(c)) {
            while (match(st, isdigit)) {}
            return PIT_LEX_TOKEN_INTEGER_LITERAL;
        }
        else {
            while (match(st, is_symchar)) {}
            return PIT_LEX_TOKEN_SYMBOL;
        }
    }
}
