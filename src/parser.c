#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "lexer.h"
#include "parser.h"
#include "runtime.h"

static pit_lex_token peek(pit_parser *st) {
    if (!st) return PIT_LEX_TOKEN_ERROR;
    return st->next.token;
}

static pit_lex_token advance(pit_parser *st) {
    if (!st) return PIT_LEX_TOKEN_ERROR;
    st->cur = st->next;
    st->next.token = pit_lex_next(st->lexer);
    st->next.start = st->lexer->start;
    st->next.end = st->lexer->end;
    return st->cur.token;
}

static bool match(pit_parser *st, pit_lex_token t) {
    if (peek(st) == t) {
        advance(st);
        return true;
    } else return false;
}

static void get_token_string(pit_parser *st, char *buf, i64 len) {
    i64 diff = st->cur.end - st->cur.start;
    i64 tlen = diff >= len ? len - 1 : diff;
    memcpy(buf, st->lexer->input + st->cur.start, tlen);
    buf[tlen] = 0;
}

pit_parser *pit_parser_from_lexer(pit_lexer *lex) {
    pit_parser *ret = malloc(sizeof(*ret));
    ret->lexer = lex;
    ret->cur.token = ret->next.token = PIT_LEX_TOKEN_ERROR;
    ret->cur.start = ret->next.start = 0;
    ret->cur.end = ret->next.end = 0;
    advance(ret);
    return ret;
}

// parse a single expression
pit_value pit_parse(pit_runtime *rt, pit_parser *st) {
    char buf[256] = {0};
    pit_lex_token t = advance(st);
    switch (t) {
    case PIT_LEX_TOKEN_ERROR:
        pit_error(rt, "encountered an error token while parsing");
        return PIT_NIL;
    case PIT_LEX_TOKEN_EOF:
        pit_error(rt, "end-of-file while parsing");
        return PIT_NIL;
    case PIT_LEX_TOKEN_LPAREN: {
        // to construct a cons-list, we need the arguments "backwards"
        // we could reverse or build up a temporary list
        // (or use non-tail recursion, which is basically the temporary list on the stack)
        // we choose to build a temporary list on the scratch arena
        i64 scratch_reset = rt->scratch->next;
        while (!match(st, PIT_LEX_TOKEN_RPAREN)) {
            pit_value *cell = pit_arena_alloc_bulk(rt->scratch, sizeof(pit_value));
            *cell = pit_parse(rt, st);
            if (rt->error != PIT_NIL) return PIT_NIL; // if we hit an error, stop!
        }
        pit_value ret = PIT_NIL;
        for (i64 i = rt->scratch->next - sizeof(pit_value); i >= scratch_reset; i -= sizeof(pit_value)) {
            pit_value *v = pit_arena_idx(rt->scratch, i);
            ret = pit_cons(rt, *v, ret);
        }
        rt->scratch->next = scratch_reset;
        return ret;
    }
    case PIT_LEX_TOKEN_QUOTE:
        return pit_list(rt, 2, pit_intern_cstr(rt, "quote"), pit_parse(rt, st));
    case PIT_LEX_TOKEN_INTEGER_LITERAL:
        get_token_string(st, buf, sizeof(buf));
        return pit_integer_new(rt, atoi(buf));
    case PIT_LEX_TOKEN_STRING_LITERAL:
        get_token_string(st, buf, sizeof(buf));
        i64 len = strlen(buf);
        i64 cur = 0;
        for (i64 i = 1; i < len; ++i) {
            if (buf[i] == '\\' && i + 1 < len) buf[cur++] = buf[++i];
            else if (buf[i] != '"') buf[cur++] = buf[i];
            else break;
        }
        return pit_bytes_new(rt, (u8 *) buf, cur);
    case PIT_LEX_TOKEN_SYMBOL:
        get_token_string(st, buf, sizeof(buf));
        return pit_intern_cstr(rt, buf);
    default:
        pit_error(rt, "unexpected token: %s", pit_lex_token_name(t));
        return PIT_NIL;
    }
}
