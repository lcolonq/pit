#include <stdio.h>

#include "lexer.h"
#include "parser.h"
#include "runtime.h"
#include "library.h"

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
        pit_intern_cstr(rt, "fset"),
        pit_list(rt, 2, pit_intern_cstr(rt, "quote"), nm),
        pit_cons(rt, pit_intern_cstr(rt, "lambda"), pit_cons(rt, as, body))
    );
}

static pit_value impl_m_defmacro(pit_runtime *rt, pit_value args) {
    pit_value nm = pit_car(rt, args);
    return pit_list(rt, 3,
        pit_intern_cstr(rt, "progn"),
        pit_cons(rt, pit_intern_cstr(rt, "defun"), args),
        pit_list(rt, 2, pit_intern_cstr(rt, "set-symbol-macro"), nm)
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
        pit_intern_cstr(rt, "set"),
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
    pit_value f;
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
    pit_value x = pit_car(rt, args);
    return pit_eval(rt, x);
}

static pit_value impl_load(pit_runtime *rt, pit_value args) {
    pit_value path = pit_car(rt, args);
    char pathbuf[1024] = {0};
    i64 len = pit_as_bytes(rt, path, (u8 *) pathbuf, sizeof(pathbuf) - 1);
    pit_value bs, ret, p;
    pit_lexer lex;
    pit_parser parse;
    bool eof;
    if (len < 0) { pit_error(rt, "path was not a string"); return PIT_NIL; }
    pathbuf[len] = 0;
    bs = pit_bytes_new_file(rt, pathbuf);
    if (!pit_lexer_from_bytes(rt, &lex, bs)) {
        pit_error(rt, "failed to initialize lexer");
        return PIT_NIL;
    }
    pit_parser_from_lexer(&parse, &lex);
    ret = PIT_NIL;
    eof = false;
    p = PIT_NIL;
    while (p = pit_parse(rt, &parse, &eof), !eof) {
        ret = pit_eval(rt, p);
    }
    return ret;
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

static pit_value impl_add(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_integer_new(rt, x + y);
}

static pit_value impl_sub(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_integer_new(rt, x - y);
}

void pit_install_library_essential(pit_runtime *rt) {
    /* special forms */
    pit_sfset(rt, pit_intern_cstr(rt, "quote"), pit_nativefunc_new(rt, impl_sf_quote));
    pit_sfset(rt, pit_intern_cstr(rt, "if"), pit_nativefunc_new(rt, impl_sf_if));
    pit_sfset(rt, pit_intern_cstr(rt, "progn"), pit_nativefunc_new(rt, impl_sf_progn));
    pit_sfset(rt, pit_intern_cstr(rt, "lambda"), pit_nativefunc_new(rt, impl_sf_lambda));

    /* macros */
    pit_mset(rt, pit_intern_cstr(rt, "defun"), pit_nativefunc_new(rt, impl_m_defun));
    pit_mset(rt, pit_intern_cstr(rt, "defmacro"), pit_nativefunc_new(rt, impl_m_defmacro));
    pit_mset(rt, pit_intern_cstr(rt, "let"), pit_nativefunc_new(rt, impl_m_let));
    pit_mset(rt, pit_intern_cstr(rt, "and"), pit_nativefunc_new(rt, impl_m_and));
    pit_mset(rt, pit_intern_cstr(rt, "setq"), pit_nativefunc_new(rt, impl_m_setq));

    /* eval */
    pit_fset(rt, pit_intern_cstr(rt, "eval"), pit_nativefunc_new(rt, impl_eval));

    /* symbols */
    pit_fset(rt, pit_intern_cstr(rt, "set"), pit_nativefunc_new(rt, impl_set));
    pit_fset(rt, pit_intern_cstr(rt, "fset"), pit_nativefunc_new(rt, impl_fset));
    pit_fset(rt, pit_intern_cstr(rt, "symbol-is-macro"), pit_nativefunc_new(rt, impl_symbol_is_macro));
    pit_fset(rt, pit_intern_cstr(rt, "funcall"), pit_nativefunc_new(rt, impl_funcall));

    /* cons cells */
    pit_fset(rt, pit_intern_cstr(rt, "cons"), pit_nativefunc_new(rt, impl_cons));
    pit_fset(rt, pit_intern_cstr(rt, "car"), pit_nativefunc_new(rt, impl_car));
    pit_fset(rt, pit_intern_cstr(rt, "cdr"), pit_nativefunc_new(rt, impl_cdr));

    /* arithmetic */
    pit_fset(rt, pit_intern_cstr(rt, "+"), pit_nativefunc_new(rt, impl_add));
    pit_fset(rt, pit_intern_cstr(rt, "-"), pit_nativefunc_new(rt, impl_sub));

    /* stream IO */
    pit_fset(rt, pit_intern_cstr(rt, "print"), pit_nativefunc_new(rt, impl_print));
    pit_fset(rt, pit_intern_cstr(rt, "princ"), pit_nativefunc_new(rt, impl_princ));

    /* disk IO */
    pit_fset(rt, pit_intern_cstr(rt, "load"), pit_nativefunc_new(rt, impl_load));
}
