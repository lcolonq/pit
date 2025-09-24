#include "runtime.h"

static pit_value impl_sf_quote(pit_runtime *rt, pit_value args) {
    pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
        .sort = EVAL_PROGRAM_ENTRY_LITERAL,
        .literal = pit_car(rt, args)
    });
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
    pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
        .sort = EVAL_PROGRAM_ENTRY_LITERAL,
        .literal = final,
    });
    return PIT_NIL;
}

static pit_value impl_sf_lambda(pit_runtime *rt, pit_value args) {
    pit_value as = pit_car(rt, args);
    pit_value body = pit_cdr(rt, args);
    pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
        .sort = EVAL_PROGRAM_ENTRY_LITERAL,
        .literal = pit_lambda(rt, as, body),
    });
    return PIT_NIL;
}

static pit_value impl_m_let(pit_runtime *rt, pit_value args) {
    pit_value lparams = PIT_NIL;
    pit_value largs = PIT_NIL;
    pit_value binds = pit_car(rt, args);
    pit_value bodyforms = pit_cdr(rt, args);
    while (binds != PIT_NIL) {
        pit_value bind = pit_car(rt, binds);
        pit_value sym = pit_car(rt, bind);
        pit_value expr = pit_car(rt, pit_cdr(rt, bind));
        lparams = pit_cons(rt, sym, lparams);
        largs = pit_cons(rt, expr, largs);
        binds = pit_cdr(rt, binds);
    }
    pit_value lambda = pit_cons(rt, pit_intern_cstr(rt, "lambda"), pit_cons(rt, lparams, bodyforms));
    pit_value application = pit_cons(rt, lambda, largs);
    return application;
}

static pit_value impl_m_and(pit_runtime *rt, pit_value args) {
    args = pit_reverse(rt, args);
    pit_value ret = PIT_NIL;
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

static pit_value impl_print(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    pit_trace(rt, x);
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
    pit_sfset(rt, pit_intern_cstr(rt, "quote"), pit_nativefunc_new(rt, impl_sf_quote));
    pit_sfset(rt, pit_intern_cstr(rt, "if"), pit_nativefunc_new(rt, impl_sf_if));
    pit_sfset(rt, pit_intern_cstr(rt, "progn"), pit_nativefunc_new(rt, impl_sf_progn));
    pit_sfset(rt, pit_intern_cstr(rt, "lambda"), pit_nativefunc_new(rt, impl_sf_lambda));

    pit_mset(rt, pit_intern_cstr(rt, "let"), pit_nativefunc_new(rt, impl_m_let));
    pit_mset(rt, pit_intern_cstr(rt, "and"), pit_nativefunc_new(rt, impl_m_and));

    pit_fset(rt, pit_intern_cstr(rt, "print"), pit_nativefunc_new(rt, impl_print));
    pit_fset(rt, pit_intern_cstr(rt, "set"), pit_nativefunc_new(rt, impl_set));
    pit_fset(rt, pit_intern_cstr(rt, "fset"), pit_nativefunc_new(rt, impl_fset));
    pit_fset(rt, pit_intern_cstr(rt, "+"), pit_nativefunc_new(rt, impl_add));
    pit_fset(rt, pit_intern_cstr(rt, "-"), pit_nativefunc_new(rt, impl_sub));
}
