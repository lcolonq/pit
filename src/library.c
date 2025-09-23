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
        .bind = final,
    });
    return PIT_NIL;
}

static pit_value impl_sf_let(pit_runtime *rt, pit_value args) {
    pit_value binds = pit_car(rt, args);
    pit_value unbinds = PIT_NIL;
    while (binds != PIT_NIL) {
        pit_value bind = pit_car(rt, binds);
        pit_value sym = pit_car(rt, bind);
        pit_value expr = pit_car(rt, pit_cdr(rt, bind));
        pit_value v = pit_eval(rt, expr);
        pit_bind(rt, sym, v);
        binds = pit_cdr(rt, binds);
        unbinds = pit_cons(rt, bind, unbinds);
    }
    impl_sf_progn(rt, pit_cdr(rt, args));
    while (unbinds != PIT_NIL) {
        pit_value unbind = pit_car(rt, unbinds);
        pit_value sym = pit_car(rt, unbind);
        pit_unbind(rt, sym);
        unbinds = pit_cdr(rt, unbinds);
    }
    return PIT_NIL;
}

static pit_value impl_set(pit_runtime *rt, pit_value args) {
    pit_value sym = pit_car(rt, args);
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    pit_set(rt, sym, v);
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
    pit_sfset(rt, pit_intern_cstr(rt, "let"), pit_nativefunc_new(rt, impl_sf_let));

    pit_fset(rt, pit_intern_cstr(rt, "print"), pit_nativefunc_new(rt, impl_print));
    pit_fset(rt, pit_intern_cstr(rt, "set"), pit_nativefunc_new(rt, impl_set));
    pit_fset(rt, pit_intern_cstr(rt, "+"), pit_nativefunc_new(rt, impl_add));
    pit_fset(rt, pit_intern_cstr(rt, "-"), pit_nativefunc_new(rt, impl_sub));
}
