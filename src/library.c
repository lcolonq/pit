#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcq/pit/lexer.h>
#include <lcq/pit/parser.h>
#include <lcq/pit/runtime.h>
#include <lcq/pit/library.h>

static pit_value impl_sf_quote(pit_runtime *rt, pit_value args) {
    pit_runtime_eval_program_push_literal(rt, rt->program, pit_car(rt, args));
    return PIT_NIL;
}
static pit_value impl_sf_if(pit_runtime *rt, pit_value args) {
    pit_value c = pit_car(rt, args);
    if (pit_eval(rt, c) != PIT_NIL) {
        pit_values_push(rt, rt->expr_stack, pit_car(rt, pit_cdr(rt, args)));
    } else {
        pit_values_push(rt, rt->expr_stack, pit_car(rt, pit_cdr(rt, pit_cdr(rt, args))));
    }
    return PIT_NIL;
}
static pit_value impl_sf_progn(pit_runtime *rt, pit_value args) {
    pit_value bodyforms = args;
    pit_value final = PIT_NIL;
    while (bodyforms != PIT_NIL) {
        final = pit_eval(rt, pit_car(rt, bodyforms));
        bodyforms = pit_cdr(rt, bodyforms);
    }
    pit_runtime_eval_program_push_literal(rt, rt->program, final);
    return PIT_NIL;
}
static pit_value impl_sf_lambda(pit_runtime *rt, pit_value args) {
    pit_value as = pit_car(rt, args);
    pit_value body = pit_cdr(rt, args);
    pit_runtime_eval_program_push_literal(rt, rt->program, pit_lambda(rt, as, body));
    return PIT_NIL;
}
static pit_value impl_m_defun(pit_runtime *rt, pit_value args) {
    pit_value nm = pit_car(rt, args);
    pit_value as = pit_car(rt, pit_cdr(rt, args));
    pit_value body = pit_cdr(rt, pit_cdr(rt, args));
    return pit_list(rt, 3,
        pit_intern_cstr(rt, "fset!"),
        pit_list(rt, 2, pit_intern_cstr(rt, "quote"), nm),
        pit_cons(rt, pit_intern_cstr(rt, "lambda"), pit_cons(rt, as, body))
    );
}
static pit_value impl_m_defmacro(pit_runtime *rt, pit_value args) {
    pit_value nm = pit_car(rt, args);
    return pit_list(rt, 3,
        pit_intern_cstr(rt, "progn"),
        pit_cons(rt, pit_intern_cstr(rt, "defun!"), args),
        pit_list(rt, 2, pit_intern_cstr(rt, "set-symbol-macro!"), nm)
    );
}
static pit_value impl_m_let(pit_runtime *rt, pit_value args) {
    pit_value lparams = PIT_NIL;
    pit_value largs = PIT_NIL;
    pit_value binds = pit_car(rt, args);
    pit_value bodyforms = pit_cdr(rt, args);
    pit_value lambda, application;
    while (binds != PIT_NIL) {
        pit_value bind = pit_car(rt, binds);
        pit_value sym = pit_car(rt, bind);
        pit_value expr = pit_car(rt, pit_cdr(rt, bind));
        lparams = pit_cons(rt, sym, lparams);
        largs = pit_cons(rt, expr, largs);
        binds = pit_cdr(rt, binds);
    }
    lambda = pit_cons(rt, pit_intern_cstr(rt, "lambda"), pit_cons(rt, lparams, bodyforms));
    application = pit_cons(rt, lambda, largs);
    return application;
}
static pit_value impl_m_and(pit_runtime *rt, pit_value args) {
    pit_value ret = PIT_NIL;
    args = pit_reverse(rt, args);
    if (args != PIT_NIL) {
        ret = pit_car(rt, args);
        args = pit_cdr(rt, args);
    }
    while (args != PIT_NIL) {
        ret = pit_list(rt, 3, pit_intern_cstr(rt, "if"), pit_car(rt, args), ret, PIT_NIL);
        args = pit_cdr(rt, args);
    }
    return ret;
}
static pit_value impl_m_setq(pit_runtime *rt, pit_value args) {
    pit_value sym = pit_car(rt, args);
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    return pit_list(rt, 3,
        pit_intern_cstr(rt, "set!"),
        pit_list(rt, 2, pit_intern_cstr(rt, "quote"), sym),
        v
    );
}
static pit_value impl_set(pit_runtime *rt, pit_value args) {
    pit_value sym = pit_car(rt, args);
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    pit_set(rt, sym, v);
    return v;
}
static pit_value impl_fset(pit_runtime *rt, pit_value args) {
    pit_value sym = pit_car(rt, args);
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    pit_fset(rt, sym, v);
    return v;
}
static pit_value impl_symbol_is_macro(pit_runtime *rt, pit_value args) {
    pit_value sym = pit_car(rt, args);
    pit_symbol_is_macro(rt, sym);
    return PIT_NIL;
}
static pit_value impl_funcall(pit_runtime *rt, pit_value args) {
    pit_value fsym = pit_car(rt, args);
    pit_value f = PIT_NIL;
    if (pit_is_symbol(rt, fsym)) {
        f = pit_fget(rt, fsym);
    } else {
        /* if f is not a symbol, assume it is a func or nativefunc
           most commonly, this happens when you funcall a variable
           with a function in the value cell, e.g. passing a lambda to a function */
        f = fsym;
    }
    return pit_apply(rt, f, pit_cdr(rt, args));
}
static pit_value impl_eval(pit_runtime *rt, pit_value args) {
    return pit_eval(rt, pit_car(rt, args));
}
static pit_value impl_eq_p(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    pit_value y = pit_car(rt, pit_cdr(rt, args));
    return pit_eq(x, y);
}
static pit_value impl_equal_p(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    pit_value y = pit_car(rt, pit_cdr(rt, args));
    return pit_equal(rt, x, y);
}
static pit_value impl_integer_p(pit_runtime *rt, pit_value args) {
    return pit_is_integer(rt, pit_car(rt, args));
}
static pit_value impl_double_p(pit_runtime *rt, pit_value args) {
    return pit_is_double(rt, pit_car(rt, args));
}
static pit_value impl_symbol_p(pit_runtime *rt, pit_value args) {
    return pit_is_symbol(rt, pit_car(rt, args));
}
static pit_value impl_cons_p(pit_runtime *rt, pit_value args) {
    return pit_is_cons(rt, pit_car(rt, args));
}
static pit_value impl_array_p(pit_runtime *rt, pit_value args) {
    return pit_is_array(rt, pit_car(rt, args));
}
static pit_value impl_bytes_p(pit_runtime *rt, pit_value args) {
    return pit_is_bytes(rt, pit_car(rt, args));
}
static pit_value impl_function_p(pit_runtime *rt, pit_value args) {
    return pit_is_bytes(rt, pit_car(rt, args));
}
static pit_value impl_cons(pit_runtime *rt, pit_value args) {
    return pit_cons(rt, pit_car(rt, args), pit_car(rt, pit_cdr(rt, args)));
}
static pit_value impl_car(pit_runtime *rt, pit_value args) {
    return pit_car(rt, pit_car(rt, args));
}
static pit_value impl_cdr(pit_runtime *rt, pit_value args) {
    return pit_cdr(rt, pit_car(rt, args));
}
static pit_value impl_setcar(pit_runtime *rt, pit_value args) {
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    pit_setcar(rt, pit_car(rt, args), v);
    return v;
}
static pit_value impl_setcdr(pit_runtime *rt, pit_value args) {
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    pit_setcdr(rt, pit_car(rt, args), v);
    return v;
}
static pit_value impl_list(pit_runtime *rt, pit_value args) {
    (void) rt;
    return args;
}
static pit_value impl_list_map(pit_runtime *rt, pit_value args) {
    pit_value func = pit_car(rt, args);
    pit_value xs = pit_car(rt, pit_cdr(rt, args));
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL) {
        pit_value y = pit_apply(rt, func, pit_cons(rt, pit_car(rt, xs), PIT_NIL));
        ret = pit_cons(rt, y, ret);
        xs = pit_cdr(rt, xs);
    }
    return pit_reverse(rt, ret);
}
static pit_value impl_add(pit_runtime *rt, pit_value args) {
    i64 total = 0;
    while (args != PIT_NIL) {
        total += pit_as_integer(rt, pit_car(rt, args));
        args = pit_cdr(rt, args);
    }
    return pit_integer_new(rt, total);
}
static pit_value impl_sub(pit_runtime *rt, pit_value args) {
    i64 total = pit_as_integer(rt, pit_car(rt, args));
    args = pit_cdr(rt, args);
    while (args != PIT_NIL) {
        total -= pit_as_integer(rt, pit_car(rt, args));
        args = pit_cdr(rt, args);
    }
    return pit_integer_new(rt, total);
}
static pit_value impl_mul(pit_runtime *rt, pit_value args) {
    i64 total = 1;
    while (args != PIT_NIL) {
        total *= pit_as_integer(rt, pit_car(rt, args));
        args = pit_cdr(rt, args);
    }
    return pit_integer_new(rt, total);
}
static pit_value impl_div(pit_runtime *rt, pit_value args) {
    i64 total = pit_as_integer(rt, pit_car(rt, args));
    args = pit_cdr(rt, args);
    while (args != PIT_NIL) {
        i64 denom = pit_as_integer(rt, pit_car(rt, args));
        if (denom == 0) {
            pit_error(rt, "divide by zero");
            return PIT_NIL;
        }
        total /= denom;
        args = pit_cdr(rt, args);
    }
    return pit_integer_new(rt, total);
}
void pit_install_library_essential(pit_runtime *rt) {
    /* special forms */
    pit_sfset(rt, pit_intern_cstr(rt, "quote"), pit_nativefunc_new(rt, impl_sf_quote));
    pit_sfset(rt, pit_intern_cstr(rt, "if"), pit_nativefunc_new(rt, impl_sf_if));
    pit_sfset(rt, pit_intern_cstr(rt, "progn"), pit_nativefunc_new(rt, impl_sf_progn));
    pit_sfset(rt, pit_intern_cstr(rt, "lambda"), pit_nativefunc_new(rt, impl_sf_lambda));
    /* macros */
    pit_mset(rt, pit_intern_cstr(rt, "defun!"), pit_nativefunc_new(rt, impl_m_defun));
    pit_mset(rt, pit_intern_cstr(rt, "defmacro!"), pit_nativefunc_new(rt, impl_m_defmacro));
    pit_mset(rt, pit_intern_cstr(rt, "let"), pit_nativefunc_new(rt, impl_m_let));
    pit_mset(rt, pit_intern_cstr(rt, "and"), pit_nativefunc_new(rt, impl_m_and));
    pit_mset(rt, pit_intern_cstr(rt, "setq!"), pit_nativefunc_new(rt, impl_m_setq));
    /* eval */
    pit_fset(rt, pit_intern_cstr(rt, "eval!"), pit_nativefunc_new(rt, impl_eval));
    /* predicates */
    pit_fset(rt, pit_intern_cstr(rt, "eq?"), pit_nativefunc_new(rt, impl_eq_p));
    pit_fset(rt, pit_intern_cstr(rt, "equal?"), pit_nativefunc_new(rt, impl_equal_p));
    pit_fset(rt, pit_intern_cstr(rt, "integer?"), pit_nativefunc_new(rt, impl_integer_p));
    pit_fset(rt, pit_intern_cstr(rt, "double?"), pit_nativefunc_new(rt, impl_double_p));
    pit_fset(rt, pit_intern_cstr(rt, "symbol?"), pit_nativefunc_new(rt, impl_symbol_p));
    pit_fset(rt, pit_intern_cstr(rt, "cons?"), pit_nativefunc_new(rt, impl_cons_p));
    pit_fset(rt, pit_intern_cstr(rt, "array?"), pit_nativefunc_new(rt, impl_array_p));
    pit_fset(rt, pit_intern_cstr(rt, "bytes?"), pit_nativefunc_new(rt, impl_bytes_p));
    pit_fset(rt, pit_intern_cstr(rt, "function?"), pit_nativefunc_new(rt, impl_function_p));
    /* symbols */
    pit_fset(rt, pit_intern_cstr(rt, "set!"), pit_nativefunc_new(rt, impl_set));
    pit_fset(rt, pit_intern_cstr(rt, "fset!"), pit_nativefunc_new(rt, impl_fset));
    pit_fset(rt, pit_intern_cstr(rt, "symbol-is-macro!"), pit_nativefunc_new(rt, impl_symbol_is_macro));
    pit_fset(rt, pit_intern_cstr(rt, "funcall"), pit_nativefunc_new(rt, impl_funcall));
    /* cons cells */
    pit_fset(rt, pit_intern_cstr(rt, "cons"), pit_nativefunc_new(rt, impl_cons));
    pit_fset(rt, pit_intern_cstr(rt, "car"), pit_nativefunc_new(rt, impl_car));
    pit_fset(rt, pit_intern_cstr(rt, "cdr"), pit_nativefunc_new(rt, impl_cdr));
    pit_fset(rt, pit_intern_cstr(rt, "setcar!"), pit_nativefunc_new(rt, impl_setcar));
    pit_fset(rt, pit_intern_cstr(rt, "setcdr!"), pit_nativefunc_new(rt, impl_setcdr));
    /* cons lists*/
    pit_fset(rt, pit_intern_cstr(rt, "list"), pit_nativefunc_new(rt, impl_list));
    pit_fset(rt, pit_intern_cstr(rt, "list/map"), pit_nativefunc_new(rt, impl_list_map));
    /* arithmetic */
    pit_fset(rt, pit_intern_cstr(rt, "+"), pit_nativefunc_new(rt, impl_add));
    pit_fset(rt, pit_intern_cstr(rt, "-"), pit_nativefunc_new(rt, impl_sub));
    pit_fset(rt, pit_intern_cstr(rt, "*"), pit_nativefunc_new(rt, impl_mul));
    pit_fset(rt, pit_intern_cstr(rt, "/"), pit_nativefunc_new(rt, impl_div));
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
    pit_value bs = pit_bytes_new_file(rt, pathbuf);
    pit_lexer lex = {0};
    if (!pit_lexer_from_bytes(rt, &lex, bs)) {
        pit_error(rt, "failed to initialize lexer");
        return PIT_NIL;
    }
    pit_parser parse = {0};
    pit_parser_from_lexer(&parse, &lex);
    pit_value ret = PIT_NIL;
    bool eof = false;
    pit_value p = PIT_NIL;
    while (p = pit_parse(rt, &parse, &eof), !eof) {
        ret = pit_eval(rt, p);
    }
    return ret;
}
void pit_install_library_io(pit_runtime *rt) {
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
static struct bytestring *bytestring_get(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "value was not a reference (to a bytestring)");
        return NULL;
    }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return NULL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_NATIVEDATA) {
        pit_error(rt, "invalid use of value as bytestring nativedata");
        return NULL;
    }
    if (!pit_eq(h->in.nativedata.tag, pit_intern_cstr(rt, "bs"))) {
        pit_error(rt, "native value is not a bytestring");
        return NULL;
    }
    if (!h->in.nativedata.data) {
        pit_error(rt, "bytestring was already freed");
        return NULL;
    }
    return h->in.nativedata.data;
}
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
    if (bs->data) free(bs->data); bs->data = NULL;
    free(bs); h->in.nativedata.data = NULL;
    return PIT_T;
}
static pit_value impl_bs_grow(pit_runtime *rt, pit_value args) {
    pit_value vsz = pit_car(rt, args);
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    struct bytestring *bs = bytestring_get(rt, v);
    if (!bs) return PIT_NIL;
    i64 sz = pit_as_integer(rt, vsz);
    if (sz > bs->len) {
        if (sz > bs->cap) {
            while (bs->cap < sz) bs->cap <<= 1;
            bs->data = realloc(bs->data, (size_t) bs->cap);
        }
        bs->len = sz;
    }
}
static pit_value impl_bs_spit(pit_runtime *rt, pit_value args) {
    pit_value path = pit_car(rt, args);
    char pathbuf[1024] = {0};
    i64 len = pit_as_bytes(rt, path, (u8 *) pathbuf, sizeof(pathbuf) - 1);
    if (len < 0) { pit_error(rt, "path was not a string"); return PIT_NIL; }
    pathbuf[len] = 0;
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    struct bytestring *bs = bytestring_get(rt, v);
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
    struct bytestring *bs = bytestring_get(rt, v);
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
