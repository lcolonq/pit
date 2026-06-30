#include <lcq/pit/runtime/value/func.h>

static pit_value free_vars(pit_runtime *rt, pit_value initial_bound, pit_value body) {
    i64 expr_stack_reset = rt->expr_stack->next;
    pit_value ret = PIT_NIL;
    if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons(rt, initial_bound, body)) < 0) {
        pit_error(rt, "free variable search stack overflow");
        return PIT_NIL;
    }
    while (rt->expr_stack->next > expr_stack_reset) {
        pit_value boundscur, bound, cur;
        if (pit_vec_pop(pit_value)(rt->expr_stack, &boundscur) < 0) {
            pit_error(rt, "free variable search stack underflow");
            return PIT_NIL;
        }
        bound = pit_value_cons_car(rt, boundscur);
        cur = pit_value_cons_cdr(rt, boundscur);
        if (pit_value_is_cons(rt, cur)) {
            pit_value fsym = pit_value_cons_car(rt, cur);
            bool is_symbol = pit_value_is_symbol(rt, fsym);
            pit_value fargs = pit_value_cons_cdr(rt, cur);
            if (is_symbol && pit_symtab_symbol_name_match_cstr(rt, fsym, "lambda")) {
                pit_value new_bound = pit_value_list_append(rt, pit_value_cons_car(rt, fargs), bound);
                fargs = pit_value_cons_cdr(rt, fargs);
                while (fargs != PIT_NIL) {
                    if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons(rt, new_bound, pit_value_cons_car(rt, fargs))) < 0) {
                        pit_error(rt, "free variable search stack overflow");
                        return PIT_NIL;
                    }
                    fargs = pit_value_cons_cdr(rt, fargs);
                }
            } else if (is_symbol && pit_symtab_symbol_name_match_cstr(rt, fsym, "quote")) {
                /* don't look inside quote!
                   if we add other special forms, make sure to consider them here if necessary! */
            } else {
                while (fargs != PIT_NIL) {
                    if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons(rt, bound, pit_value_cons_car(rt, fargs))) < 0) {
                        pit_error(rt, "free variable search stack overflow");
                        return PIT_NIL;
                    }
                    fargs = pit_value_cons_cdr(rt, fargs);
                }
                if (!is_symbol) {
                    if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons(rt, bound, fsym)) < 0) {
                        pit_error(rt, "free variable search stack overflow");
                        return PIT_NIL;
                    }
                }
            }
        } else if (pit_value_is_symbol(rt, cur)) {
            if (pit_value_list_contains_eq(rt, cur, bound) == PIT_NIL) {
                ret = pit_value_cons(rt, cur, ret);
            }
        }
    }
    rt->expr_stack->next = expr_stack_reset;
    return ret;
}

bool pit_value_is_func(pit_runtime *rt, pit_value a) {
    return pit_value_is_ref_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_FUNC);
}
bool pit_value_is_nativefunc(pit_runtime *rt, pit_value a) {
    return pit_value_is_ref_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_NATIVEFUNC);
}
pit_value pit_value_func_lambda(pit_runtime *rt, pit_value args, pit_value body) {
    pit_value ret = pit_value_ref_heavy_new(rt);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for lambda"); return PIT_NIL; }
    pit_value expanded = pit_macroexpand(rt, pit_value_cons(rt, pit_symtab_intern_cstr(rt, "progn"), body));
    pit_value freevars = free_vars(rt, args, expanded);
    pit_value env = PIT_NIL;
    while (freevars != PIT_NIL) {
        pit_value sym = pit_value_cons_car(rt, freevars);
        pit_value cell = pit_symtab_get_value_cell(rt, sym);
        env = pit_value_cons(rt, pit_value_cons(rt, sym, cell), env);
        freevars = pit_value_cons_cdr(rt, freevars);
    }
    h->hsort = PIT_VALUE_HEAVY_SORT_FUNC;
    pit_value arg_cells = PIT_NIL;
    pit_value arg_rest_nm = PIT_NIL;
    pit_value separator = pit_symtab_intern_cstr(rt, "&");
    while (args != PIT_NIL) {
        pit_value nm = pit_value_cons_car(rt, args);
        if (pit_value_eq(nm, separator)) {
            pit_value next_nm = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
            if (next_nm == PIT_NIL) { pit_error(rt, "invalid & in lambda list"); return PIT_NIL; }
            arg_rest_nm = next_nm;
            arg_cells = pit_value_cons(rt, next_nm, arg_cells);
            break;
        } else {
            arg_cells = pit_value_cons(rt, nm, arg_cells);
            args = pit_value_cons_cdr(rt, args);
        }
    }
    arg_cells = pit_value_list_reverse(rt, arg_cells);
    h->in.func.args = arg_cells;
    h->in.func.arg_rest_nm = arg_rest_nm;
    h->in.func.env = env;
    h->in.func.body = expanded;
    return ret;
}
pit_value pit_value_nativefunc_new_with_data(pit_runtime *rt, pit_nativefunc f, void *data) {
    pit_value ret = pit_value_ref_heavy_new(rt);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for nativefunc"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_NATIVEFUNC;
    h->in.nativefunc.f = f;
    h->in.nativefunc.data = data;
    return ret;
}
pit_value pit_value_nativefunc_new(pit_runtime *rt, pit_nativefunc f) {
    return pit_value_nativefunc_new_with_data(rt, f, NULL);
}
pit_value pit_value_apply(pit_runtime *rt, pit_value f, pit_value args) {
    char buf[256] = {0};
    if (pit_value_is_symbol(rt, f)) {
        f = pit_symtab_fget(rt, f);
    }
    /* if f is not a symbol, assume it is a func or nativefunc
       most commonly, this happens when you funcall a variable
       with a function in the value cell, e.g. passing a lambda to a function */
    switch (pit_value_sort(f)) {
    case PIT_VALUE_SORT_REF: {
        pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, f));
        if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
        if (h->hsort == PIT_VALUE_HEAVY_SORT_FUNC) {
            /* calling a Lisp function is simple! */
            pit_value bound = PIT_NIL;
            pit_value env = h->in.func.env;
            while (env != PIT_NIL) { /* first, bind all entries in the closure */
                pit_value b = pit_value_cons_car(rt, env);
                pit_value nm = pit_value_cons_car(rt, b);
                pit_symtab_bind(rt, nm, pit_value_cons_cdr(rt, b));
                bound = pit_value_cons(rt, nm, bound);
                env = pit_value_cons_cdr(rt, env);
            }
            pit_value anames = h->in.func.args;
            while (anames != PIT_NIL) { /* bind all argument names to their values */
                pit_value nm = pit_value_cons_car(rt, anames);
                pit_value cell = pit_value_cell_new(rt, PIT_NIL);
                if (h->in.func.arg_rest_nm != PIT_NIL && pit_value_eq(nm, h->in.func.arg_rest_nm)) {
                    pit_value_cell_set(rt, cell, args, nm);
                    pit_symtab_bind(rt, nm, cell);
                    break;
                } else {
                    pit_value_cell_set(rt, cell, pit_value_cons_car(rt, args), nm);
                    pit_symtab_bind(rt, nm, cell);
                }
                bound = pit_value_cons(rt, nm, bound);
                args = pit_value_cons_cdr(rt, args);
                anames = pit_value_cons_cdr(rt, anames);
            }
            pit_value ret = pit_eval(rt, h->in.func.body); /* evaluate the body */
            while (bound != PIT_NIL) { /* unbind everything we bound earlier, in reverse */
                pit_symtab_unbind(rt, pit_value_cons_car(rt, bound));
                bound = pit_value_cons_cdr(rt, bound);
            }
            return ret;
        } else if (h->hsort == PIT_VALUE_HEAVY_SORT_NATIVEFUNC) {
            /* calling native functions is even simpler */
            return h->in.nativefunc.f(rt, args, h->in.nativefunc.data);
        } else {
            i64 end = pit_dump(rt, buf, sizeof(buf) - 1, f, true);
            buf[end] = 0;
            pit_error(rt, "attempted to apply non-function ref: %s", buf);
            return PIT_NIL;
        }
    }
    default: {
        i64 end = pit_dump(rt, buf, sizeof(buf) - 1, f, true);
        buf[end] = 0;
        pit_error(rt, "attempted to apply non-function value: %s", buf);
        return PIT_NIL;
    }
    }
}
