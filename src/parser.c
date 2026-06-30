#include <lcq/pit/types.h>
#include <lcq/pit/utils.h>
#include <lcq/pit/lexer.h>
#include <lcq/pit/parser.h>
#include <lcq/pit/runtime.h>

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
    st->next.line = st->lexer->start_line;
    st->next.column = st->lexer->start_column;
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
    pit_libc_string_memcpy((u8 *) buf, (u8 *) st->lexer->input + st->cur.start, (size_t) tlen);
    buf[tlen] = 0;
}

static i64 digit_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else {
        return 0;
    }
}

void pit_parser_from_lexer(pit_parser *ret, pit_lexer *lex) {
    ret->lexer = lex;
    ret->cur.token = ret->next.token = PIT_LEX_TOKEN_ERROR;
    ret->cur.start = ret->next.start = 0;
    ret->cur.end = ret->next.end = 0;
    ret->cur.line = ret->next.line = -1;
    ret->cur.column = ret->next.column = -1;
    advance(ret);
}

/* parse a single expression */
pit_value pit_parse(pit_runtime *rt, pit_parser *st, bool *eof) {
    if (rt == NULL || st == NULL) return PIT_NIL;
    pit_lex_token t = advance(st);
    rt->source_line = st->cur.line;
    rt->source_column = st->cur.column;
    switch (t) {
    case PIT_LEX_TOKEN_ERROR:
        pit_error(rt, "encountered an error while lexing: %s", st->lexer->error);
        return PIT_NIL;
    case PIT_LEX_TOKEN_EOF:
        if (eof != NULL) {
            *eof = true;
        } else {
            pit_error(rt, "end-of-file while parsing");
        }
        return PIT_NIL;
    case PIT_LEX_TOKEN_LPAREN: {
        pit_value ret = PIT_NIL;
        while (!match(st, PIT_LEX_TOKEN_RPAREN)) {
            if (match(st, PIT_LEX_TOKEN_DOT)) {
                ret = pit_parse(rt, st, eof);
                if (match(st, PIT_LEX_TOKEN_RPAREN)) {
                    break;
                } else {
                    pit_error(rt, "unterminated dotted list");
                    return PIT_NIL;
                }
            } else {
                ret = pit_value_cons(rt, pit_parse(rt, st, eof), ret);
            }
            if (rt->error != PIT_NIL || (eof != NULL && *eof)) {
                pit_error(rt, "unterminated list");
                return PIT_NIL; /* if we hit an error, stop!*/
            }
        }
        ret = pit_value_list_reverse(rt, ret);
        if (pit_value_sort(ret) == PIT_VALUE_SORT_REF) {
            pit_annotation a = {0};
            a.line = rt->source_line;
            a.column = rt->source_column;
            pit_annotation_set(rt, pit_value_as_ref(rt, ret), a);
        }
        return ret;
    }
    case PIT_LEX_TOKEN_LSQUARE: {
        pit_value ret = PIT_NIL;
        pit_value xs = PIT_NIL;
        i64 len = 0;
        while (!match(st, PIT_LEX_TOKEN_RSQUARE)) {
            pit_value x = pit_parse(rt, st, eof);
            xs = pit_value_cons(rt, x, xs);
            len += 1;
            if (rt->error != PIT_NIL || (eof != NULL && *eof)) {
                pit_error(rt, "unterminated array literal");
                return PIT_NIL;
            }
        }
        ret = pit_value_array_new(rt, len);
        while (xs != PIT_NIL) {
            pit_value_array_set(rt, ret, --len, pit_value_cons_car(rt, xs));
            xs = pit_value_cons_cdr(rt, xs);
        }
        return ret;
    }
    case PIT_LEX_TOKEN_QUOTE:
        return pit_value_list(rt, 2, pit_symtab_intern_cstr(rt, "quote"), pit_parse(rt, st, eof));
    case PIT_LEX_TOKEN_INTEGER_LITERAL: {
        i64 idx = st->cur.start;
        i64 base = 10;
        i64 total = 0;
        bool neg = false;
        char c = st->lexer->input[idx++];
        if (c == '-') {
            neg = true;
            if (idx < st->cur.end) {
                c = st->lexer->input[idx++];
            } else {
                pit_error(rt, "malformed negative integer literal"); return PIT_NIL;
            }
        }
        if (c == '0' && idx + 1 < st->cur.end) {
            switch (st->lexer->input[idx++]) {
            case 'b': base = 2; break;
            case 'o': base = 8; break;
            case 'x': base = 16; break;
            default: pit_error(rt, "unknown integer base"); return PIT_NIL;
            }
        } else { total = digit_value(c); }
        while (idx < st->cur.end) {
            total *= base;
            total += digit_value(st->lexer->input[idx++]);
            if (total > 0x1ffffffffffff) {
                pit_error(rt, "integer literal too large"); return PIT_NIL;
            }
        }
        return pit_value_integer_new(rt, neg ? -total : total);
    }
    case PIT_LEX_TOKEN_STRING_LITERAL: {
        char buf[256] = {0};
        get_token_string(st, buf, sizeof(buf));
        i64 len = (i64) pit_libc_string_strlen(buf);
        i64 cur = 0;
        for (i64 i = 1; i < len; ++i) {
            if (buf[i] == '\\' && i + 1 < len) buf[cur++] = buf[++i];
            else if (buf[i] != '"') buf[cur++] = buf[i];
            else break;
        }
        return pit_value_bytes_new(rt, (u8 *) buf, cur);
    }
    case PIT_LEX_TOKEN_SYMBOL: {
        char buf[256] = {0};
        get_token_string(st, buf, sizeof(buf));
        return pit_symtab_intern_cstr(rt, buf);
    }
    default:
        pit_error(rt, "unexpected token: %s", pit_lex_token_name(t));
        return PIT_NIL;
    }
}
