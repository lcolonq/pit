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
static pit_value impl_sf_cond(pit_runtime *rt, pit_value args) {
    while (args != PIT_NIL) {
        pit_value clause = pit_car(rt, args);
        pit_value cond = pit_car(rt, clause);
        if (pit_eval(rt, cond) != PIT_NIL) {
            pit_values_push(rt, rt->expr_stack,
                pit_cons(rt, pit_intern_cstr(rt, "progn"), pit_cdr(rt, clause))
            );
            return PIT_NIL;
        }
        args = pit_cdr(rt, args);
    }
    pit_values_push(rt, rt->expr_stack, PIT_NIL);
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
static pit_value impl_sf_or(pit_runtime *rt, pit_value args) {
    pit_value bodyforms = args;
    pit_value final = PIT_NIL;
    while (bodyforms != PIT_NIL) {
        final = pit_eval(rt, pit_car(rt, bodyforms));
        if (final != PIT_NIL) break;
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
static pit_value impl_m_defstruct(pit_runtime *rt, pit_value args) {
    pit_value ret = PIT_NIL;
    pit_value df = PIT_NIL;
    pit_value aargs = PIT_NIL;
    char nm_str[128];
    char field_str[128];
    char buf[512];
    pit_value nm = pit_car(rt, args);
    pit_value fields = pit_cdr(rt, args);
    i64 field_idx = 0;
    i64 nm_len = pit_as_bytes(rt, pit_symbol_name(rt, nm), (u8 *) nm_str, sizeof(nm_str) - 1);
    if (nm_len < 0) return PIT_NIL;
    nm_str[nm_len] = 0;
    /* constructor */
    snprintf(buf, sizeof(buf), ":%s", nm_str);
    aargs = pit_cons(rt, pit_intern_cstr(rt, buf), pit_cons(rt, pit_intern_cstr(rt, "array"), PIT_NIL));
    fields = pit_cdr(rt, args);
    while (fields != PIT_NIL) {
        i64 field_len = pit_as_bytes(rt,
            pit_symbol_name(rt, pit_car(rt, fields)),
            (u8 *) field_str, sizeof(field_str) - 1
        );
        if (field_len < 0) return PIT_NIL;
        field_str[field_len] = 0;
        snprintf(buf, sizeof(buf), ":%s", field_str);
        aargs = pit_cons(rt,
            pit_list(rt, 3, pit_intern_cstr(rt, "plist/get"), pit_intern_cstr(rt, buf), pit_intern_cstr(rt, "kwargs")),
            aargs
        );
        fields = pit_cdr(rt, fields);
    }
    snprintf(buf, sizeof(buf), "%s/new", nm_str);
    df = pit_list(rt, 4,
        pit_intern_cstr(rt, "defun!"),
        pit_intern_cstr(rt, buf),
        pit_list(rt, 2, pit_intern_cstr(rt, "&"), pit_intern_cstr(rt, "kwargs")),
        pit_reverse(rt, aargs)
    );
    ret = pit_cons(rt, df, ret);
    /* getters and setters */
    fields = pit_cdr(rt, args);
    field_idx = 0;
    while (fields != PIT_NIL) {
        i64 field_len = pit_as_bytes(rt,
            pit_symbol_name(rt, pit_car(rt, fields)),
            (u8 *) field_str, sizeof(field_str) - 1
        );
        if (field_len < 0) return PIT_NIL;
        field_str[field_len] = 0;
        /* getter */
        snprintf(buf, sizeof(buf), "%s/get-%s", nm_str, field_str);
        df = pit_list(rt, 4,
            pit_intern_cstr(rt, "defun!"),
            pit_intern_cstr(rt, buf),
            pit_list(rt, 1, pit_intern_cstr(rt, "v")),
            pit_list(rt, 3,
                pit_intern_cstr(rt, "array/get"),
                pit_integer_new(rt, field_idx + 1),
                pit_intern_cstr(rt, "v")
            )
        );
        ret = pit_cons(rt, df, ret);
        /* setter */
        snprintf(buf, sizeof(buf), "%s/set-%s!", nm_str, field_str);
        df = pit_list(rt, 4,
            pit_intern_cstr(rt, "defun!"),
            pit_intern_cstr(rt, buf),
            pit_list(rt, 2, pit_intern_cstr(rt, "v"), pit_intern_cstr(rt, "x")),
            pit_list(rt, 4,
                pit_intern_cstr(rt, "array/set!"),
                pit_integer_new(rt, field_idx + 1),
                pit_intern_cstr(rt, "x"),
                pit_intern_cstr(rt, "v")
            )
        );
        ret = pit_cons(rt, df, ret);
        fields = pit_cdr(rt, fields);
        field_idx += 1;
    }
    // (defstruct foo x y z)
    // (defun foo/new (kwargs) ...)
    // (defun foo/get-x (f) ...)
    // (defun foo/set-x! (f v) ...)
    // pit_trace(rt, ret);
    return pit_cons(rt, pit_intern_cstr(rt, "progn"), ret);
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

// (case x (y 'foo) (z 'bar))
// (cond ((eq x 'y) 'foo) ((eq x 'z) 'bar))
static pit_value impl_m_case(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    pit_value cases = pit_cdr(rt, args);
    pit_value clauses = PIT_NIL;
    pit_value xvar = pit_intern_cstr(rt, "(internal case)");
    while (cases != PIT_NIL) {
        pit_value c = pit_car(rt, cases);
        clauses = pit_cons(rt,
            pit_list(rt, 2,
                pit_list(rt, 3, pit_intern_cstr(rt, "equal?"),
                    xvar,
                    pit_list(rt, 2, pit_intern_cstr(rt, "quote"), pit_car(rt, c))
                ),
                pit_car(rt, pit_cdr(rt, c))
            ),
            clauses
        );
        cases = pit_cdr(rt, cases);
    }
    return pit_list(rt, 3,
        pit_intern_cstr(rt, "let"),
        pit_list(rt, 1, pit_list(rt, 2, xvar, x)),
        pit_cons(rt, pit_intern_cstr(rt, "cond"), pit_reverse(rt, clauses))
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
    pit_value f = pit_car(rt, args);
    return pit_apply(rt, f, pit_cdr(rt, args));
}
static pit_value impl_error(pit_runtime *rt, pit_value args) {
    rt->error = PIT_T;
    rt->error = pit_car(rt, args);
    rt->error_line = rt->source_line;
    rt->error_column = rt->source_column;
    return PIT_NIL;
}
static pit_value impl_eval(pit_runtime *rt, pit_value args) {
    return pit_eval(rt, pit_car(rt, args));
}
static pit_value impl_eq_p(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    pit_value y = pit_car(rt, pit_cdr(rt, args));
    return pit_bool_new(rt, pit_eq(x, y));
}
static pit_value impl_equal_p(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    pit_value y = pit_car(rt, pit_cdr(rt, args));
    return pit_bool_new(rt, pit_equal(rt, x, y));
}
static pit_value impl_integer_p(pit_runtime *rt, pit_value args) {
    return pit_bool_new(rt, pit_is_integer(rt, pit_car(rt, args)));
}
static pit_value impl_double_p(pit_runtime *rt, pit_value args) {
    return pit_bool_new(rt, pit_is_double(rt, pit_car(rt, args)));
}
static pit_value impl_symbol_p(pit_runtime *rt, pit_value args) {
    return pit_bool_new(rt, pit_is_symbol(rt, pit_car(rt, args)));
}
static pit_value impl_cons_p(pit_runtime *rt, pit_value args) {
    return pit_bool_new(rt, pit_is_cons(rt, pit_car(rt, args)));
}
static pit_value impl_array_p(pit_runtime *rt, pit_value args) {
    return pit_bool_new(rt, pit_is_array(rt, pit_car(rt, args)));
}
static pit_value impl_bytes_p(pit_runtime *rt, pit_value args) {
    return pit_bool_new(rt, pit_is_bytes(rt, pit_car(rt, args)));
}
static pit_value impl_function_p(pit_runtime *rt, pit_value args) {
    pit_value a = pit_car(rt, args);
    bool b = (pit_is_symbol(rt, a) && pit_fget(rt, a) != PIT_NIL)
        || pit_is_func(rt, a)
        || pit_is_nativefunc(rt, a);
    return pit_bool_new(rt, b);
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
static pit_value impl_list_len(pit_runtime *rt, pit_value args) {
    pit_value arr = pit_car(rt, args);
    return pit_integer_new(rt, pit_list_len(rt, arr));
}
static pit_value impl_list_reverse(pit_runtime *rt, pit_value args) {
    return pit_reverse(rt, pit_car(rt, args));
}
static pit_value impl_list_uniq(pit_runtime *rt, pit_value args) {
    pit_value xs = pit_car(rt, args);
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL) {
        pit_value x = pit_car(rt, xs);
        if (pit_contains_equal(rt, x, ret) == PIT_NIL) {
            ret = pit_cons(rt, x, ret);
        }
        xs = pit_cdr(rt, xs);
    }
    return pit_reverse(rt, ret);
}
static pit_value impl_list_append(pit_runtime *rt, pit_value args) {
    args = pit_reverse(rt, args);
    pit_value ret = pit_car(rt, args);
    pit_value ls = pit_cdr(rt, args);
    while (ls != PIT_NIL) {
        pit_value xs = pit_reverse(rt, pit_car(rt, ls));
        while (xs != PIT_NIL) {
            ret = pit_cons(rt, pit_car(rt, xs), ret);
            xs = pit_cdr(rt, xs);
        }
        ls = pit_cdr(rt, ls);
    }
    return ret;
}
static pit_value impl_list_concat(pit_runtime *rt, pit_value args) {
    return impl_list_append(rt, pit_car(rt, args));
}
static pit_value impl_list_take(pit_runtime *rt, pit_value args) {
    i64 num = pit_as_integer(rt, pit_car(rt, args));
    pit_value arr = pit_car(rt, pit_cdr(rt, args));
    pit_value ret = PIT_NIL;
    while (num > 0 && arr != PIT_NIL) {
        ret = pit_cons(rt, pit_car(rt, arr), ret);
        arr = pit_cdr(rt, arr);
        num -= 1;
    }
    return pit_reverse(rt, ret);
}
static pit_value impl_list_drop(pit_runtime *rt, pit_value args) {
    i64 num = pit_as_integer(rt, pit_car(rt, args));
    pit_value arr = pit_car(rt, pit_cdr(rt, args));
    while (num > 0 && arr != PIT_NIL) {
        arr = pit_cdr(rt, arr);
        num -= 1;
    }
    return arr;
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
static pit_value impl_list_foldl(pit_runtime *rt, pit_value args) {
    pit_value func = pit_car(rt, args);
    pit_value acc = pit_car(rt, pit_cdr(rt, args));
    pit_value xs = pit_car(rt, pit_cdr(rt, pit_cdr(rt, args)));
    while (xs != PIT_NIL) {
        acc = pit_apply(rt, func, pit_list(rt, 2, pit_car(rt, xs), acc));
        xs = pit_cdr(rt, xs);
    }
    return acc;
}
static pit_value impl_list_filter(pit_runtime *rt, pit_value args) {
    pit_value func = pit_car(rt, args);
    pit_value xs = pit_car(rt, pit_cdr(rt, args));
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL) {
        pit_value x = pit_car(rt, xs);
        pit_value y = pit_apply(rt, func, pit_cons(rt, x, PIT_NIL));
        if (y != PIT_NIL) {
            ret = pit_cons(rt, x, ret);
        }
        xs = pit_cdr(rt, xs);
    }
    return pit_reverse(rt, ret);
}
static pit_value impl_list_find(pit_runtime *rt, pit_value args) {
    pit_value func = pit_car(rt, args);
    pit_value xs = pit_car(rt, pit_cdr(rt, args));
    while (xs != PIT_NIL) {
        pit_value x = pit_car(rt, xs);
        pit_value y = pit_apply(rt, func, pit_cons(rt, x, PIT_NIL));
        if (y != PIT_NIL) {
            return x;
        }
        xs = pit_cdr(rt, xs);
    }
    return PIT_NIL;
}
static pit_value impl_list_contains_p(pit_runtime *rt, pit_value args) {
    pit_value needle = pit_car(rt, args);
    pit_value haystack = pit_car(rt, pit_cdr(rt, args));
    while (haystack != PIT_NIL) {
        if (pit_equal(rt, needle, pit_car(rt, haystack))) return PIT_T;
        haystack = pit_cdr(rt, haystack);
    }
    return PIT_NIL;
}
static pit_value impl_list_all_p(pit_runtime *rt, pit_value args) {
    pit_value f = pit_car(rt, args);
    pit_value xs = pit_car(rt, pit_cdr(rt, args));
    while (xs != PIT_NIL) {
        pit_value x = pit_car(rt, xs);
        if (pit_apply(rt, f, pit_cons(rt, x, PIT_NIL)) == PIT_NIL) {
            return PIT_NIL;
        }
        xs = pit_cdr(rt, xs);
    }
    return PIT_T;
}
static pit_value impl_list_zip_with(pit_runtime *rt, pit_value args) {
    pit_value f = pit_car(rt, args);
    pit_value xs = pit_car(rt, pit_cdr(rt, args));
    pit_value ys = pit_car(rt, pit_cdr(rt, pit_cdr(rt, args)));
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL && ys != PIT_NIL) {
        pit_value z = pit_apply(rt, f, pit_list(rt, 2, pit_car(rt, xs), pit_car(rt, ys)));
        ret = pit_cons(rt, z, ret);
        xs = pit_cdr(rt, xs); ys = pit_cdr(rt, ys);
    }
    return pit_reverse(rt, ret);
}
static pit_value impl_bytes_len(pit_runtime *rt, pit_value args) {
    pit_value v = pit_car(rt, args);
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "value is not a ref");
        return PIT_NIL;
    }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) { pit_error(rt, "ref is not bytes"); return PIT_NIL; }
    return pit_integer_new(rt, h->in.bytes.len);
}
static pit_value impl_bytes_range(pit_runtime *rt, pit_value args) {
    i64 start = pit_as_integer(rt, pit_car(rt, args));
    i64 end = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    pit_value v = pit_car(rt, pit_cdr(rt, pit_cdr(rt, args)));
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "value is not a ref");
        return PIT_NIL;
    }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) { pit_error(rt, "ref is not bytes"); return PIT_NIL; }
    if (start < 0 || start >= h->in.bytes.len) {
        pit_error(rt, "bytes range start index out of bounds: %d", start);
        return PIT_NIL;
    }
    if (end < start || end < 0 || end > h->in.bytes.len) {
        pit_error(rt, "bytes range end index out of bounds: %d", end);
        return PIT_NIL;
    }
    return pit_bytes_new(rt, h->in.bytes.data + start, end - start);
}
static pit_value impl_array(pit_runtime *rt, pit_value args) {
    i64 scratch_reset = rt->scratch->next;
    i64 len = 0;
    while (args != PIT_NIL) {
        pit_value *cell = pit_arena_alloc_bulk(rt->scratch, sizeof(pit_value));
        *cell = pit_car(rt, args);
        len += 1;
        args = pit_cdr(rt, args);
    }
    rt->scratch->next = scratch_reset;
    return pit_array_from_buf(rt, pit_arena_idx(rt->scratch, (i32) scratch_reset), len);
}
static pit_value impl_array_to_list(pit_runtime *rt, pit_value args) {
    pit_value arr = pit_car(rt, args);
    i64 ilen = pit_array_len(rt, arr);
    pit_value ret = PIT_NIL;
    i64 i = 0;
    for (; i < ilen; ++i) {
        ret = pit_cons(rt, pit_array_get(rt, arr, i), ret);
    }
    return pit_reverse(rt, ret);
}
static pit_value impl_array_from_list(pit_runtime *rt, pit_value args) {
    i64 i = 0;
    pit_value xs = pit_car(rt, args);
    i64 ilen = pit_list_len(rt, xs);
    pit_value ret = pit_array_new(rt, ilen);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to deref heavy value for array"); return PIT_NIL; }
    while (xs != PIT_NIL) {
        h->in.array.data[i] = pit_car(rt, xs);
        xs = pit_cdr(rt, xs);
        i += 1;
    }
    return ret;
}
static pit_value impl_array_repeat(pit_runtime *rt, pit_value args) {
    i64 i = 0;
    pit_value v = pit_car(rt, args);
    pit_value len = pit_car(rt, pit_cdr(rt, args));
    i64 ilen = pit_as_integer(rt, len);
    pit_value ret = pit_array_new(rt, ilen);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to deref heavy value for array"); return PIT_NIL; }
    for (; i < ilen; ++i) {
        h->in.array.data[i] = v;
    }
    return ret;
}
static pit_value impl_array_len(pit_runtime *rt, pit_value args) {
    pit_value arr = pit_car(rt, args);
    return pit_integer_new(rt, pit_array_len(rt, arr));
}
static pit_value impl_array_get(pit_runtime *rt, pit_value args) {
    pit_value idx = pit_car(rt, args);
    pit_value arr = pit_car(rt, pit_cdr(rt, args));
    return pit_array_get(rt, arr, pit_as_integer(rt, idx));
}
static pit_value impl_array_set(pit_runtime *rt, pit_value args) {
    pit_value idx = pit_car(rt, args);
    pit_value v = pit_car(rt, pit_cdr(rt, args));
    pit_value arr = pit_car(rt, pit_cdr(rt, pit_cdr(rt, args)));
    return pit_array_set(rt, arr, pit_as_integer(rt, idx), v);
}
static pit_value impl_array_map(pit_runtime *rt, pit_value args) {
    pit_value func = pit_car(rt, args);
    pit_value arr = pit_car(rt, pit_cdr(rt, args));
    i64 len = pit_array_len(rt, arr);
    pit_value ret = pit_array_new(rt, len);
    i64 i = 0;
    for (i = 0; i < len; ++i) {
        pit_value y = pit_apply(rt, func, pit_cons(rt, pit_array_get(rt, arr, i), PIT_NIL));
        pit_array_set(rt, ret, i, y);
    }
    return ret;
}
static pit_value impl_array_map_mut(pit_runtime *rt, pit_value args) {
    pit_value func = pit_car(rt, args);
    pit_value arr = pit_car(rt, pit_cdr(rt, args));
    i64 len = pit_array_len(rt, arr);
    i64 i = 0;
    for (i = 0; i < len; ++i) {
        pit_value y = pit_apply(rt, func, pit_cons(rt, pit_array_get(rt, arr, i), PIT_NIL));
        pit_array_set(rt, arr, i, y);
    }
    return arr;
}
static pit_value impl_abs(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    if (x < 0) return pit_integer_new(rt, -x);
    return pit_integer_new(rt, x);
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
    i64 total = 0;
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
static pit_value impl_not(pit_runtime *rt, pit_value args) {
    if (pit_car(rt, args) == PIT_NIL) {
        return PIT_T;
    } else {
        return PIT_NIL;
    }
}
static pit_value impl_lt(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_bool_new(rt, x < y);
}
static pit_value impl_gt(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_bool_new(rt, x > y);
}
static pit_value impl_le(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_bool_new(rt, x <= y);
}
static pit_value impl_ge(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_bool_new(rt, x >= y);
}
static pit_value impl_bitwise_and(pit_runtime *rt, pit_value args) {
    i64 total = -1;
    while (args != PIT_NIL) {
        total &= pit_as_integer(rt, pit_car(rt, args));
        args = pit_cdr(rt, args);
    }
    return pit_integer_new(rt, total);
}
static pit_value impl_bitwise_or(pit_runtime *rt, pit_value args) {
    i64 total = 0;
    while (args != PIT_NIL) {
        total |= pit_as_integer(rt, pit_car(rt, args));
        args = pit_cdr(rt, args);
    }
    return pit_integer_new(rt, total);
}
static pit_value impl_bitwise_xor(pit_runtime *rt, pit_value args) {
    i64 total = 0;
    while (args != PIT_NIL) {
        total ^= pit_as_integer(rt, pit_car(rt, args));
        args = pit_cdr(rt, args);
    }
    return pit_integer_new(rt, total);
}
static pit_value impl_bitwise_not(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    return pit_integer_new(rt, ~x);
}
static pit_value impl_bitwise_lshift(pit_runtime *rt, pit_value args) {
    i64 val = pit_as_integer(rt, pit_car(rt, args));
    i64 shift = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_integer_new(rt, val << shift);
}
static pit_value impl_bitwise_rshift(pit_runtime *rt, pit_value args) {
    i64 val = pit_as_integer(rt, pit_car(rt, args));
    i64 shift = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_integer_new(rt, val >> shift);
}
void pit_install_library_essential(pit_runtime *rt) {
    /* special forms */
    pit_sfset(rt, pit_intern_cstr(rt, "quote"), pit_nativefunc_new(rt, impl_sf_quote));
    pit_sfset(rt, pit_intern_cstr(rt, "if"), pit_nativefunc_new(rt, impl_sf_if));
    pit_sfset(rt, pit_intern_cstr(rt, "cond"), pit_nativefunc_new(rt, impl_sf_cond));
    pit_sfset(rt, pit_intern_cstr(rt, "progn"), pit_nativefunc_new(rt, impl_sf_progn));
    pit_sfset(rt, pit_intern_cstr(rt, "or"), pit_nativefunc_new(rt, impl_sf_or));
    pit_sfset(rt, pit_intern_cstr(rt, "lambda"), pit_nativefunc_new(rt, impl_sf_lambda));
    /* macros */
    pit_mset(rt, pit_intern_cstr(rt, "defun!"), pit_nativefunc_new(rt, impl_m_defun));
    pit_mset(rt, pit_intern_cstr(rt, "defmacro!"), pit_nativefunc_new(rt, impl_m_defmacro));
    pit_mset(rt, pit_intern_cstr(rt, "defstruct!"), pit_nativefunc_new(rt, impl_m_defstruct));
    pit_mset(rt, pit_intern_cstr(rt, "let"), pit_nativefunc_new(rt, impl_m_let));
    pit_mset(rt, pit_intern_cstr(rt, "and"), pit_nativefunc_new(rt, impl_m_and));
    pit_mset(rt, pit_intern_cstr(rt, "setq!"), pit_nativefunc_new(rt, impl_m_setq));
    pit_mset(rt, pit_intern_cstr(rt, "case"), pit_nativefunc_new(rt, impl_m_case));
    /* error */
    pit_fset(rt, pit_intern_cstr(rt, "error!"), pit_nativefunc_new(rt, impl_error));
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
    pit_fset(rt, pit_intern_cstr(rt, "list/len"), pit_nativefunc_new(rt, impl_list_len));
    pit_fset(rt, pit_intern_cstr(rt, "list/reverse"), pit_nativefunc_new(rt, impl_list_reverse));
    pit_fset(rt, pit_intern_cstr(rt, "list/uniq"), pit_nativefunc_new(rt, impl_list_uniq));
    pit_fset(rt, pit_intern_cstr(rt, "list/append"), pit_nativefunc_new(rt, impl_list_append));
    pit_fset(rt, pit_intern_cstr(rt, "list/concat"), pit_nativefunc_new(rt, impl_list_concat));
    pit_fset(rt, pit_intern_cstr(rt, "list/take"), pit_nativefunc_new(rt, impl_list_take));
    pit_fset(rt, pit_intern_cstr(rt, "list/drop"), pit_nativefunc_new(rt, impl_list_drop));
    pit_fset(rt, pit_intern_cstr(rt, "list/map"), pit_nativefunc_new(rt, impl_list_map));
    pit_fset(rt, pit_intern_cstr(rt, "list/foldl"), pit_nativefunc_new(rt, impl_list_foldl));
    pit_fset(rt, pit_intern_cstr(rt, "list/filter"), pit_nativefunc_new(rt, impl_list_filter));
    pit_fset(rt, pit_intern_cstr(rt, "list/find"), pit_nativefunc_new(rt, impl_list_find));
    pit_fset(rt, pit_intern_cstr(rt, "list/contains?"), pit_nativefunc_new(rt, impl_list_contains_p));
    pit_fset(rt, pit_intern_cstr(rt, "list/all?"), pit_nativefunc_new(rt, impl_list_all_p));
    pit_fset(rt, pit_intern_cstr(rt, "list/zip-with"), pit_nativefunc_new(rt, impl_list_zip_with));
    /* bytestrings */ 
    pit_fset(rt, pit_intern_cstr(rt, "bytes/len"), pit_nativefunc_new(rt, impl_bytes_len));
    pit_fset(rt, pit_intern_cstr(rt, "bytes/range"), pit_nativefunc_new(rt, impl_bytes_range));
    /* array */
    pit_fset(rt, pit_intern_cstr(rt, "array"), pit_nativefunc_new(rt, impl_array));
    pit_fset(rt, pit_intern_cstr(rt, "array/to-list"), pit_nativefunc_new(rt, impl_array_to_list));
    pit_fset(rt, pit_intern_cstr(rt, "array/from-list"), pit_nativefunc_new(rt, impl_array_from_list));
    pit_fset(rt, pit_intern_cstr(rt, "array/repeat"), pit_nativefunc_new(rt, impl_array_repeat));
    pit_fset(rt, pit_intern_cstr(rt, "array/len"), pit_nativefunc_new(rt, impl_array_len));
    pit_fset(rt, pit_intern_cstr(rt, "array/get"), pit_nativefunc_new(rt, impl_array_get));
    pit_fset(rt, pit_intern_cstr(rt, "array/set!"), pit_nativefunc_new(rt, impl_array_set));
    pit_fset(rt, pit_intern_cstr(rt, "array/map"), pit_nativefunc_new(rt, impl_array_map));
    pit_fset(rt, pit_intern_cstr(rt, "array/map!"), pit_nativefunc_new(rt, impl_array_map_mut));
    /* arithmetic */
    pit_fset(rt, pit_intern_cstr(rt, "abs"), pit_nativefunc_new(rt, impl_abs));
    pit_fset(rt, pit_intern_cstr(rt, "+"), pit_nativefunc_new(rt, impl_add));
    pit_fset(rt, pit_intern_cstr(rt, "-"), pit_nativefunc_new(rt, impl_sub));
    pit_fset(rt, pit_intern_cstr(rt, "*"), pit_nativefunc_new(rt, impl_mul));
    pit_fset(rt, pit_intern_cstr(rt, "/"), pit_nativefunc_new(rt, impl_div));
    /* booleans */
    pit_fset(rt, pit_intern_cstr(rt, "not"), pit_nativefunc_new(rt, impl_not));
    /* comparisons */
    pit_fset(rt, pit_intern_cstr(rt, "<"), pit_nativefunc_new(rt, impl_lt));
    pit_fset(rt, pit_intern_cstr(rt, ">"), pit_nativefunc_new(rt, impl_gt));
    pit_fset(rt, pit_intern_cstr(rt, "<="), pit_nativefunc_new(rt, impl_le));
    pit_fset(rt, pit_intern_cstr(rt, ">="), pit_nativefunc_new(rt, impl_ge));
    /* bitwise arithmetic */
    pit_fset(rt, pit_intern_cstr(rt, "bitwise/and"), pit_nativefunc_new(rt, impl_bitwise_and));
    pit_fset(rt, pit_intern_cstr(rt, "bitwise/or"), pit_nativefunc_new(rt, impl_bitwise_or));
    pit_fset(rt, pit_intern_cstr(rt, "bitwise/xor"), pit_nativefunc_new(rt, impl_bitwise_xor));
    pit_fset(rt, pit_intern_cstr(rt, "bitwise/not"), pit_nativefunc_new(rt, impl_bitwise_not));
    pit_fset(rt, pit_intern_cstr(rt, "bitwise/lshift"), pit_nativefunc_new(rt, impl_bitwise_lshift));
    pit_fset(rt, pit_intern_cstr(rt, "bitwise/rshift"), pit_nativefunc_new(rt, impl_bitwise_rshift));
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

static pit_value impl_plist_get(pit_runtime *rt, pit_value args) {
    pit_value k = pit_car(rt, args);
    pit_value vs = pit_car(rt, pit_cdr(rt, args));
    return pit_plist_get(rt, k, vs);
}
void pit_install_library_plist(pit_runtime *rt) {
    /* property lists / keyword arguments */
    pit_fset(rt, pit_intern_cstr(rt, "plist/get"), pit_nativefunc_new(rt, impl_plist_get));
}

static pit_value impl_alist_get(pit_runtime *rt, pit_value args) {
    pit_value k = pit_car(rt, args);
    pit_value vs = pit_car(rt, pit_cdr(rt, args));
    while (vs != PIT_NIL) {
        pit_value v = pit_car(rt, vs);
        if (pit_equal(rt, k, pit_car(rt, v))) {
            return pit_cdr(rt, v);
        }
        vs = pit_cdr(rt, vs);
    }
    return PIT_NIL;
}
void pit_install_library_alist(pit_runtime *rt) {
    /* association lists */
    pit_fset(rt, pit_intern_cstr(rt, "alist/get"), pit_nativefunc_new(rt, impl_alist_get));
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
