#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lcq/pit/lexer.h>
#include <lcq/pit/parser.h>
#include <lcq/pit/runtime.h>
#include <lcq/pit/library.h>

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

bool pit_runtime_print_error(pit_runtime *rt) {
    if (!pit_eq(rt->error, PIT_NIL)) {
        char buf[1024] = {0};
        i64 end = pit_dump(rt, buf, sizeof(buf) - 1, rt->error, false);
        buf[end] = 0;
        fprintf(stderr, "error at line %ld, column %ld: %s\n", rt->error_line, rt->error_column, buf);
        return true;
    }
    return false;
}

void pit_trace_(pit_runtime *rt, char *format, pit_value v) {
    char buf[1024] = {0};
    i64 end = pit_dump(rt, buf, sizeof(buf) - 1, v, true);
    buf[end] = 0;
    fprintf(stderr, format, buf);
}

pit_value pit_bytes_new_file(pit_runtime *rt, char *path) {
    if (rt->error != PIT_NIL) return PIT_NIL;
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        pit_error(rt, "failed to open file: %s", path);
        return PIT_NIL;
    }
    fseek(f, 0, SEEK_END);
    i64 len = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *dest = pit_arena_alloc_bulk(rt->heap, len);
    if (!dest) { pit_error(rt, "failed to allocate bytes"); fclose(f); return PIT_NIL; }
    if ((size_t) len != fread(dest, sizeof(char), (size_t) len, f)) {
        fclose(f);
        pit_error(rt, "failed to read file: %s", path);
        return PIT_NIL;
    }
    fclose(f);
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for bytes"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_BYTES;
    h->in.bytes.data = dest;
    h->in.bytes.len = len;
    return ret;
}

static void check_invariants(pit_runtime *rt) {
    if (rt->scratch->next != 0) {
        pit_error(rt, "leaked scratch memory! %ld", rt->scratch->next);
    }
    if (rt->scratch->next != 0) {
        pit_error(rt, "leaked scratch memory! %ld", rt->scratch->next);
    }
}
pit_value pit_load_file(pit_runtime *rt, char *path) {
    pit_lexer lex;
    pit_parser parse;
    bool eof = false;
    pit_value p = PIT_NIL;
    pit_value ret = PIT_NIL;
    if (pit_lex_file(&lex, path) < 0) {
        pit_error(rt, "failed to lex file: %s", path);
        return PIT_NIL;
    }
    pit_parser_from_lexer(&parse, &lex);
    while (p = pit_parse(rt, &parse, &eof), !eof) {
        check_invariants(rt); if (pit_runtime_print_error(rt)) return PIT_NIL;
        ret = pit_eval(rt, p);
        check_invariants(rt); if (pit_runtime_print_error(rt)) return PIT_NIL;
        pit_collect_garbage(rt);
        check_invariants(rt); if (pit_runtime_print_error(rt)) return PIT_NIL;
    }
    check_invariants(rt); if (pit_runtime_print_error(rt)) return PIT_NIL;
    return ret;
}

void pit_repl(pit_runtime *rt) {
    size_t bufcap = 8;
    char *buf = malloc(bufcap);
    i64 len = 0;
    pit_runtime_freeze(rt);
    check_invariants(rt); if (pit_runtime_print_error(rt)) exit(1);
    setbuf(stdout, NULL);
    printf("> ");
    while ((buf[len++] = (char) getchar()) != EOF) {
        if (len >= (i64) bufcap) {
            bufcap *= 2;
            buf = realloc(buf, bufcap);
        }
        pit_value res;
        pit_lexer lex;
        pit_parser parse;
        bool eof = false;
        pit_value p = PIT_NIL;
        i64 depth = 0;
        bool lex_error = false;
        pit_lex_token tok = PIT_LEX_TOKEN_EOF;
        if (buf[len - 1] != '\n') continue;
        pit_lex_bytes(&lex, buf, len);
        while (!lex_error && (tok = pit_lex_next(&lex)) != PIT_LEX_TOKEN_EOF) {
            switch (tok) {
            case PIT_LEX_TOKEN_ERROR: lex_error = true; break;
            case PIT_LEX_TOKEN_LPAREN: depth += 1; break;
            case PIT_LEX_TOKEN_RPAREN: depth -= 1; break;
            default: break;
            }
        }
        if (lex_error || depth > 0) continue;
        buf[len - 1] = 0;
        pit_lex_bytes(&lex, buf, len);
        pit_parser_from_lexer(&parse, &lex);
        while (p = pit_parse(rt, &parse, &eof), !eof) {
            check_invariants(rt);
            res = pit_eval(rt, p);
            check_invariants(rt);
        }
        if (pit_runtime_print_error(rt)) {
            rt->error = PIT_NIL;
            printf("> ");
        } else {
            char dumpbuf[1024] = {0};
            pit_dump(rt, dumpbuf, sizeof(dumpbuf) - 1, res, true);
            pit_collect_garbage(rt);
            printf("%s\n> ", dumpbuf);
        }
        len = 0;
    }
    if (len >= (i64) sizeof(buf)) {
        fprintf(stderr, "expression exceeded REPL buffer size\n");
    } else {
        printf("bye!\n");
    }
    free(buf);
}

static pit_value impl_diagnostics(pit_runtime *rt, pit_value args) {
    (void) args;
    fprintf(stderr, "value allocs: %ld\n", rt->heap->next);
    return PIT_NIL;
}
static pit_value impl_print(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    char buf[1024] = {0};
    pit_dump(rt, buf, sizeof(buf), x, true);
    buf[1023] = 0;
    puts(buf);
    return x;
}
static pit_value impl_princ(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    char buf[1024] = {0};
    pit_dump(rt, buf, sizeof(buf), x, false);
    buf[1023] = 0;
    puts(buf);
    return x;
}
static pit_value impl_load(pit_runtime *rt, pit_value args) {
    pit_value path = pit_car(rt, args);
    char pathbuf[1024] = {0};
    i64 len = pit_as_bytes(rt, path, (u8 *) pathbuf, sizeof(pathbuf) - 1);
    if (len < 0) { pit_error(rt, "path was not a string"); return PIT_NIL; }
    pathbuf[len] = 0;
    return pit_load_file(rt, pathbuf);
}
void pit_install_library_io(pit_runtime *rt) {
    /* diagnostics */
    pit_fset(rt, pit_intern_cstr(rt, "diagnostics!"), pit_nativefunc_new(rt, impl_diagnostics));
    /* stream IO */
    pit_fset(rt, pit_intern_cstr(rt, "print!"), pit_nativefunc_new(rt, impl_print));
    pit_fset(rt, pit_intern_cstr(rt, "princ!"), pit_nativefunc_new(rt, impl_princ));
    /* disk IO */
    pit_fset(rt, pit_intern_cstr(rt, "load!"), pit_nativefunc_new(rt, impl_load));
}

struct bytestring {
    i64 len, cap;
    u8 *data;
};
static pit_value impl_bs_new(pit_runtime *rt, pit_value args) {
    (void) args;
    i64 cap = 256;
    struct bytestring *bs = malloc(sizeof(struct bytestring));
    bs->len = 0;
    bs->cap = cap;
    bs->data = calloc((size_t) cap, 1);
    return pit_nativedata_new(rt, pit_intern_cstr(rt, "bs"), (void *) bs);
}
static pit_value impl_bs_delete(pit_runtime *rt, pit_value args) {
    pit_value v = pit_car(rt, args);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_NATIVEDATA) {
        pit_error(rt, "invalid use of value as bytestring nativedata");
        return PIT_NIL;
    }
    if (!pit_eq(h->in.nativedata.tag, pit_intern_cstr(rt, "bs"))) {
        pit_error(rt, "native value is not a bytestring");
        return PIT_NIL;
    }
    if (!h->in.nativedata.data) {
        pit_error(rt, "bytestring was already freed");
        return PIT_NIL;
    }
    struct bytestring *bs = h->in.nativedata.data;
    if (bs->data) free(bs->data);
    bs->data = NULL;
    free(bs);
    h->in.nativedata.data = NULL;
    return PIT_T;
}
static pit_value impl_bs_grow(pit_runtime *rt, pit_value args) {
    pit_value vsz = pit_car(rt, args);
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    struct bytestring *bs = pit_nativedata_get(rt, pit_intern_cstr(rt, "bs"), v);
    if (!bs) return PIT_NIL;
    i64 sz = pit_as_integer(rt, vsz);
    if (sz > bs->len) {
        if (sz > bs->cap) {
            while (bs->cap < sz) bs->cap <<= 1;
            bs->data = realloc(bs->data, (size_t) bs->cap);
        }
        bs->len = sz;
    }
    return v;
}
static pit_value impl_bs_spit(pit_runtime *rt, pit_value args) {
    pit_value path = pit_car(rt, args);
    char pathbuf[1024] = {0};
    i64 len = pit_as_bytes(rt, path, (u8 *) pathbuf, sizeof(pathbuf) - 1);
    if (len < 0) { pit_error(rt, "path was not a string"); return PIT_NIL; }
    pathbuf[len] = 0;
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    struct bytestring *bs = pit_nativedata_get(rt, pit_intern_cstr(rt, "bs"), v);
    if (!bs) return PIT_NIL;
    FILE *f = fopen(pathbuf, "w+");
    if (!f) { pit_error(rt, "failed to open file: %s", pathbuf); return PIT_NIL; }
    size_t written = fwrite(bs->data, 1, (size_t) bs->len, f);
    fclose(f);
    if (written != (size_t) bs->len) {
        pit_error(rt, "failed to write bytestring to file: %s", pathbuf);
        return PIT_NIL;
    }
    return v;
}
static pit_value impl_bs_write8(pit_runtime *rt, pit_value args) {
    pit_value v = pit_car(rt, args);
    pit_value vidx = pit_car(rt, pit_cdr(rt, args));
    pit_value vx = pit_car(rt, pit_cdr(rt, pit_cdr(rt, args)));
    struct bytestring *bs = pit_nativedata_get(rt, pit_intern_cstr(rt, "bs"), v);
    if (!bs) return PIT_NIL;
    i64 idx = pit_as_integer(rt, vidx);
    u8 x = (u8) pit_as_integer(rt, vx);
    if (idx >= bs->len) {
        pit_error(rt, "index %d out of bounds in bytestring (length %d)", idx, bs->len);
        return PIT_NIL;
    }
    bs->data[idx] = x;
    return v;
}
void pit_install_library_bytestring(pit_runtime *rt) {
    /* bytestrings */
    pit_fset(rt, pit_intern_cstr(rt, "bs/new!"), pit_nativefunc_new(rt, impl_bs_new));
    pit_fset(rt, pit_intern_cstr(rt, "bs/delete!"), pit_nativefunc_new(rt, impl_bs_delete));
    pit_fset(rt, pit_intern_cstr(rt, "bs/grow!"), pit_nativefunc_new(rt, impl_bs_grow));
    pit_fset(rt, pit_intern_cstr(rt, "bs/spit!"), pit_nativefunc_new(rt, impl_bs_spit));
    pit_fset(rt, pit_intern_cstr(rt, "bs/write8!"), pit_nativefunc_new(rt, impl_bs_write8));
}
