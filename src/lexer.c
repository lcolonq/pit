#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <lcq/pit/utils.h>
#include <lcq/pit/lexer.h>
#include <lcq/pit/types.h>

const char *PIT_LEX_TOKEN_NAMES[PIT_LEX_TOKEN__SENTINEL] = {
    /* [PIT_LEX_TOKEN_EOF] = */ "eof",
    /* [PIT_LEX_TOKEN_LPAREN] = */ "lparen",
    /* [PIT_LEX_TOKEN_RPAREN] = */ "rparen",
    /* [PIT_LEX_TOKEN_LSQUARE] = */ "lsquare",
    /* [PIT_LEX_TOKEN_RSQUARE] = */ "rsquare",
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

static int is_hexdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
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
i64 pit_lex_file(pit_lexer *ret, char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) { return -1; }
    fseek(f, 0, SEEK_END);
    i64 len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = calloc((size_t) len, sizeof(char));
    if ((size_t) len != fread(buf, sizeof(char), (size_t) len, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    pit_lex_bytes(ret, buf, len);
    return 0;
}

pit_lex_token pit_lex_next(pit_lexer *st) {
restart:
    st->start = st->end;
    st->start_line = st->line;
    st->start_column = st->column;
    char c = advance(st);
    switch (c) {
    case 0: return PIT_LEX_TOKEN_EOF;
    case ';': while (is_more_input(st) && advance(st) != '\n'); goto restart;
    case '(': return PIT_LEX_TOKEN_LPAREN;
    case ')': return PIT_LEX_TOKEN_RPAREN;
    case '[': return PIT_LEX_TOKEN_LSQUARE;
    case ']': return PIT_LEX_TOKEN_RSQUARE;
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
            if (c == '0') {
                int next = peek(st);
                if (next != 'x' && next != 'o' && next != 'b') return PIT_LEX_TOKEN_INTEGER_LITERAL;
                advance(st); /* skip base specifier */
            }
            while (match(st, is_hexdigit)) {}
            return PIT_LEX_TOKEN_INTEGER_LITERAL;
        } else {
            while (match(st, is_symchar)) {}
            return PIT_LEX_TOKEN_SYMBOL;
        }
    }
}
