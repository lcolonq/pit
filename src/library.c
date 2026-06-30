#include <lcq/pit/vec.h>
#include <lcq/pit/lexer.h>
#include <lcq/pit/parser.h>
#include <lcq/pit/runtime.h>
#include <lcq/pit/library.h>

static pit_value impl_sf_quote(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_runtime_eval_program_push_literal(rt, rt->program, pit_value_cons_car(rt, args));
    return PIT_NIL;
}
static pit_value impl_sf_if(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value c = pit_value_cons_car(rt, args);
    if (pit_eval(rt, c) != PIT_NIL) {
        if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons_car(rt, pit_value_cons_cdr(rt, args))) < 0)
            pit_error(rt, "in special form \"if\": evaluation stack overflow");
    } else {
        if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons_car(rt, pit_value_cons_cdr(rt, pit_value_cons_cdr(rt, args)))) < 0)
            pit_error(rt, "in special form \"if\": evaluation stack overflow");
    }
    return PIT_NIL;
}
static pit_value impl_sf_cond(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    while (args != PIT_NIL) {
        pit_value clause = pit_value_cons_car(rt, args);
        pit_value cond = pit_value_cons_car(rt, clause);
        if (pit_eval(rt, cond) != PIT_NIL) {
            if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons(rt, pit_symtab_intern_cstr(rt, "progn"), pit_value_cons_cdr(rt, clause))) < 0)
                pit_error(rt, "in special form \"cond\": evaluation stack overflow");
            return PIT_NIL;
        }
        args = pit_value_cons_cdr(rt, args);
    }
    if (pit_vec_push(pit_value)(rt->expr_stack, PIT_NIL) < 0)
        pit_error(rt, "in special form \"cond\": evaluation stack overflow");
    return PIT_NIL;
}
static pit_value impl_sf_progn(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value bodyforms = args;
    pit_value final = PIT_NIL;
    while (bodyforms != PIT_NIL) {
        final = pit_eval(rt, pit_value_cons_car(rt, bodyforms));
        bodyforms = pit_value_cons_cdr(rt, bodyforms);
    }
    pit_runtime_eval_program_push_literal(rt, rt->program, final);
    return PIT_NIL;
}
static pit_value impl_sf_or(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value bodyforms = args;
    pit_value final = PIT_NIL;
    while (bodyforms != PIT_NIL) {
        final = pit_eval(rt, pit_value_cons_car(rt, bodyforms));
        if (final != PIT_NIL) break;
        bodyforms = pit_value_cons_cdr(rt, bodyforms);
    }
    pit_runtime_eval_program_push_literal(rt, rt->program, final);
    return PIT_NIL;
}
static pit_value impl_sf_lambda(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value as = pit_value_cons_car(rt, args);
    pit_value body = pit_value_cons_cdr(rt, args);
    pit_runtime_eval_program_push_literal(rt, rt->program, pit_value_func_lambda(rt, as, body));
    return PIT_NIL;
}
static pit_value impl_m_defun(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value nm = pit_value_cons_car(rt, args);
    pit_value as = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value body = pit_value_cons_cdr(rt, pit_value_cons_cdr(rt, args));
    return pit_value_list(rt, 3,
        pit_symtab_intern_cstr(rt, "fset!"),
        pit_value_list(rt, 2, pit_symtab_intern_cstr(rt, "quote"), nm),
        pit_value_cons(rt, pit_symtab_intern_cstr(rt, "lambda"), pit_value_cons(rt, as, body))
    );
}
static pit_value impl_m_defmacro(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value nm = pit_value_cons_car(rt, args);
    return pit_value_list(rt, 3,
        pit_symtab_intern_cstr(rt, "progn"),
        pit_value_cons(rt, pit_symtab_intern_cstr(rt, "defun!"), args),
        pit_value_list(rt, 2, pit_symtab_intern_cstr(rt, "set-symbol-macro!"), nm)
    );
}
static pit_value impl_m_defstruct(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value ret = PIT_NIL;
    pit_value df = PIT_NIL;
    pit_value aargs = PIT_NIL;
    char nm_str[128];
    char field_str[128];
    char buf[512];
    pit_value nm = pit_value_cons_car(rt, args);
    pit_value fields = pit_value_cons_cdr(rt, args);
    i64 field_idx = 0;
    i64 nm_len = pit_value_bytes_copy(rt, pit_symtab_symbol_name(rt, nm), (u8 *) nm_str, sizeof(nm_str) - 1);
    if (nm_len < 0) return PIT_NIL;
    nm_str[nm_len] = 0;
    /* constructor */
    pit_libc_string_snprintf(buf, sizeof(buf), ":%s", nm_str);
    aargs = pit_value_cons(rt, pit_symtab_intern_cstr(rt, buf), pit_value_cons(rt, pit_symtab_intern_cstr(rt, "array"), PIT_NIL));
    fields = pit_value_cons_cdr(rt, args);
    while (fields != PIT_NIL) {
        i64 field_len = pit_value_bytes_copy(rt,
            pit_symtab_symbol_name(rt, pit_value_cons_car(rt, fields)),
            (u8 *) field_str, sizeof(field_str) - 1
        );
        if (field_len < 0) return PIT_NIL;
        field_str[field_len] = 0;
        pit_libc_string_snprintf(buf, sizeof(buf), ":%s", field_str);
        aargs = pit_value_cons(rt,
            pit_value_list(rt, 3, pit_symtab_intern_cstr(rt, "plist/get"), pit_symtab_intern_cstr(rt, buf), pit_symtab_intern_cstr(rt, "kwargs")),
            aargs
        );
        fields = pit_value_cons_cdr(rt, fields);
    }
    pit_libc_string_snprintf(buf, sizeof(buf), "%s/new", nm_str);
    df = pit_value_list(rt, 4,
        pit_symtab_intern_cstr(rt, "defun!"),
        pit_symtab_intern_cstr(rt, buf),
        pit_value_list(rt, 2, pit_symtab_intern_cstr(rt, "&"), pit_symtab_intern_cstr(rt, "kwargs")),
        pit_value_list_reverse(rt, aargs)
    );
    ret = pit_value_cons(rt, df, ret);
    /* getters and setters */
    fields = pit_value_cons_cdr(rt, args);
    field_idx = 0;
    while (fields != PIT_NIL) {
        i64 field_len = pit_value_bytes_copy(rt,
            pit_symtab_symbol_name(rt, pit_value_cons_car(rt, fields)),
            (u8 *) field_str, sizeof(field_str) - 1
        );
        if (field_len < 0) return PIT_NIL;
        field_str[field_len] = 0;
        /* getter */
        pit_libc_string_snprintf(buf, sizeof(buf), "%s/get-%s", nm_str, field_str);
        df = pit_value_list(rt, 4,
            pit_symtab_intern_cstr(rt, "defun!"),
            pit_symtab_intern_cstr(rt, buf),
            pit_value_list(rt, 1, pit_symtab_intern_cstr(rt, "v")),
            pit_value_list(rt, 3,
                pit_symtab_intern_cstr(rt, "array/get"),
                pit_value_integer_new(rt, field_idx + 1),
                pit_symtab_intern_cstr(rt, "v")
            )
        );
        ret = pit_value_cons(rt, df, ret);
        /* setter */
        pit_libc_string_snprintf(buf, sizeof(buf), "%s/set-%s!", nm_str, field_str);
        df = pit_value_list(rt, 4,
            pit_symtab_intern_cstr(rt, "defun!"),
            pit_symtab_intern_cstr(rt, buf),
            pit_value_list(rt, 2, pit_symtab_intern_cstr(rt, "v"), pit_symtab_intern_cstr(rt, "x")),
            pit_value_list(rt, 4,
                pit_symtab_intern_cstr(rt, "array/set!"),
                pit_value_integer_new(rt, field_idx + 1),
                pit_symtab_intern_cstr(rt, "x"),
                pit_symtab_intern_cstr(rt, "v")
            )
        );
        ret = pit_value_cons(rt, df, ret);
        fields = pit_value_cons_cdr(rt, fields);
        field_idx += 1;
    }
    // (defstruct foo x y z)
    // (defun foo/new (kwargs) ...)
    // (defun foo/get-x (f) ...)
    // (defun foo/set-x! (f v) ...)
    // pit_trace(rt, ret);
    return pit_value_cons(rt, pit_symtab_intern_cstr(rt, "progn"), ret);
}
static pit_value impl_m_let(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value lparams = PIT_NIL;
    pit_value largs = PIT_NIL;
    pit_value binds = pit_value_cons_car(rt, args);
    pit_value bodyforms = pit_value_cons_cdr(rt, args);
    pit_value lambda, application;
    while (binds != PIT_NIL) {
        pit_value bind = pit_value_cons_car(rt, binds);
        pit_value sym = pit_value_cons_car(rt, bind);
        pit_value expr = pit_value_cons_car(rt, pit_value_cons_cdr(rt, bind));
        lparams = pit_value_cons(rt, sym, lparams);
        largs = pit_value_cons(rt, expr, largs);
        binds = pit_value_cons_cdr(rt, binds);
    }
    lambda = pit_value_cons(rt, pit_symtab_intern_cstr(rt, "lambda"), pit_value_cons(rt, lparams, bodyforms));
    application = pit_value_cons(rt, lambda, largs);
    return application;
}
static pit_value impl_m_and(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value ret = PIT_NIL;
    args = pit_value_list_reverse(rt, args);
    if (args != PIT_NIL) {
        ret = pit_value_cons_car(rt, args);
        args = pit_value_cons_cdr(rt, args);
    }
    while (args != PIT_NIL) {
        ret = pit_value_list(rt, 3, pit_symtab_intern_cstr(rt, "if"), pit_value_cons_car(rt, args), ret, PIT_NIL);
        args = pit_value_cons_cdr(rt, args);
    }
    return ret;
}
static pit_value impl_m_setq(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value sym = pit_value_cons_car(rt, args);
    pit_value v = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    return pit_value_list(rt, 3,
        pit_symtab_intern_cstr(rt, "set!"),
        pit_value_list(rt, 2, pit_symtab_intern_cstr(rt, "quote"), sym),
        v
    );
}

// (case x (y 'foo) (z 'bar))
// (cond ((eq x 'y) 'foo) ((eq x 'z) 'bar))
static pit_value impl_m_case(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value x = pit_value_cons_car(rt, args);
    pit_value cases = pit_value_cons_cdr(rt, args);
    pit_value clauses = PIT_NIL;
    pit_value xvar = pit_symtab_intern_cstr(rt, "(internal case)");
    while (cases != PIT_NIL) {
        pit_value c = pit_value_cons_car(rt, cases);
        clauses = pit_value_cons(rt,
            pit_value_list(rt, 2,
                pit_value_list(rt, 3, pit_symtab_intern_cstr(rt, "equal?"),
                    xvar,
                    pit_value_list(rt, 2, pit_symtab_intern_cstr(rt, "quote"), pit_value_cons_car(rt, c))
                ),
                pit_value_cons_car(rt, pit_value_cons_cdr(rt, c))
            ),
            clauses
        );
        cases = pit_value_cons_cdr(rt, cases);
    }
    return pit_value_list(rt, 3,
        pit_symtab_intern_cstr(rt, "let"),
        pit_value_list(rt, 1, pit_value_list(rt, 2, xvar, x)),
        pit_value_cons(rt, pit_symtab_intern_cstr(rt, "cond"), pit_value_list_reverse(rt, clauses))
    );
}
static pit_value impl_set(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value sym = pit_value_cons_car(rt, args);
    pit_value v = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_symtab_set(rt, sym, v);
    return v;
}
static pit_value impl_fset(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value sym = pit_value_cons_car(rt, args);
    pit_value v = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_symtab_fset(rt, sym, v);
    return v;
}
static pit_value impl_symbol_mark_macro(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value sym = pit_value_cons_car(rt, args);
    pit_symtab_symbol_mark_macro(rt, sym);
    return PIT_NIL;
}
static pit_value impl_funcall(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value f = pit_value_cons_car(rt, args);
    return pit_value_apply(rt, f, pit_value_cons_cdr(rt, args));
}
static pit_value impl_apply(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value f = pit_value_cons_car(rt, args);
    pit_value xs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    return pit_value_apply(rt, f, xs);
}
static pit_value impl_error(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    rt->error = PIT_T;
    rt->error = pit_value_cons_car(rt, args);
    rt->error_line = rt->source_line;
    rt->error_column = rt->source_column;
    return PIT_NIL;
}
static pit_value impl_eval(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_eval(rt, pit_value_cons_car(rt, args));
}
static pit_value impl_eq_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value x = pit_value_cons_car(rt, args);
    pit_value y = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    return pit_value_bool_new(rt, pit_value_eq(x, y));
}
static pit_value impl_equal_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value x = pit_value_cons_car(rt, args);
    pit_value y = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    return pit_value_bool_new(rt, pit_value_equal(rt, x, y));
}
static pit_value impl_integer_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_bool_new(rt, pit_value_is_integer(rt, pit_value_cons_car(rt, args)));
}
static pit_value impl_double_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_bool_new(rt, pit_value_is_double(rt, pit_value_cons_car(rt, args)));
}
static pit_value impl_symbol_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_bool_new(rt, pit_value_is_symbol(rt, pit_value_cons_car(rt, args)));
}
static pit_value impl_cons_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_bool_new(rt, pit_value_is_cons(rt, pit_value_cons_car(rt, args)));
}
static pit_value impl_array_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_bool_new(rt, pit_value_is_array(rt, pit_value_cons_car(rt, args)));
}
static pit_value impl_bytes_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_bool_new(rt, pit_value_is_bytes(rt, pit_value_cons_car(rt, args)));
}
static pit_value impl_function_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value a = pit_value_cons_car(rt, args);
    bool b = (pit_value_is_symbol(rt, a) && pit_symtab_fget(rt, a) != PIT_NIL)
        || pit_value_is_func(rt, a)
        || pit_value_is_nativefunc(rt, a);
    return pit_value_bool_new(rt, b);
}
static pit_value impl_cons(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_cons(rt, pit_value_cons_car(rt, args), pit_value_cons_car(rt, pit_value_cons_cdr(rt, args)));
}
static pit_value impl_car(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_cons_car(rt, pit_value_cons_car(rt, args));
}
static pit_value impl_cdr(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_cons_cdr(rt, pit_value_cons_car(rt, args));
}
static pit_value impl_setcar(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value v = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value_cons_setcar(rt, pit_value_cons_car(rt, args), v);
    return v;
}
static pit_value impl_setcdr(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value v = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value_cons_setcdr(rt, pit_value_cons_car(rt, args), v);
    return v;
}
static pit_value impl_list(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    (void) rt;
    return args;
}
static pit_value impl_list_nth(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 n = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    pit_value xs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    while (xs != PIT_NIL && n-- > 0) {
        xs = pit_value_cons_cdr(rt, xs);
    }
    return pit_value_cons_car(rt, xs);
}
static pit_value impl_list_iota(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 n = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    pit_value ret = PIT_NIL;
    while (n > 0) {
        ret = pit_value_cons(rt, pit_value_integer_new(rt, --n), ret);
    }
    return ret;
}
static pit_value impl_list_len(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value arr = pit_value_cons_car(rt, args);
    return pit_value_integer_new(rt, pit_value_list_len(rt, arr));
}
static pit_value impl_list_reverse(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return pit_value_list_reverse(rt, pit_value_cons_car(rt, args));
}
static pit_value impl_list_uniq(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value xs = pit_value_cons_car(rt, args);
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL) {
        pit_value x = pit_value_cons_car(rt, xs);
        if (pit_value_list_contains_equal(rt, x, ret) == PIT_NIL) {
            ret = pit_value_cons(rt, x, ret);
        }
        xs = pit_value_cons_cdr(rt, xs);
    }
    return pit_value_list_reverse(rt, ret);
}
static pit_value impl_list_append(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    args = pit_value_list_reverse(rt, args);
    pit_value ret = pit_value_cons_car(rt, args);
    pit_value ls = pit_value_cons_cdr(rt, args);
    while (ls != PIT_NIL) {
        pit_value xs = pit_value_list_reverse(rt, pit_value_cons_car(rt, ls));
        while (xs != PIT_NIL) {
            ret = pit_value_cons(rt, pit_value_cons_car(rt, xs), ret);
            xs = pit_value_cons_cdr(rt, xs);
        }
        ls = pit_value_cons_cdr(rt, ls);
    }
    return ret;
}
static pit_value impl_list_concat(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    return impl_list_append(rt, pit_value_cons_car(rt, args), NULL);
}
static pit_value impl_list_take(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 num = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    pit_value arr = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value ret = PIT_NIL;
    while (num > 0 && arr != PIT_NIL) {
        ret = pit_value_cons(rt, pit_value_cons_car(rt, arr), ret);
        arr = pit_value_cons_cdr(rt, arr);
        num -= 1;
    }
    return pit_value_list_reverse(rt, ret);
}
static pit_value impl_list_drop(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 num = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    pit_value arr = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    while (num > 0 && arr != PIT_NIL) {
        arr = pit_value_cons_cdr(rt, arr);
        num -= 1;
    }
    return arr;
}
static pit_value impl_list_map(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value func = pit_value_cons_car(rt, args);
    pit_value xs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL) {
        pit_value y = pit_value_apply(rt, func, pit_value_cons(rt, pit_value_cons_car(rt, xs), PIT_NIL));
        ret = pit_value_cons(rt, y, ret);
        xs = pit_value_cons_cdr(rt, xs);
    }
    return pit_value_list_reverse(rt, ret);
}
static pit_value impl_list_foldl(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value func = pit_value_cons_car(rt, args);
    pit_value acc = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value xs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, pit_value_cons_cdr(rt, args)));
    while (xs != PIT_NIL) {
        acc = pit_value_apply(rt, func, pit_value_list(rt, 2, pit_value_cons_car(rt, xs), acc));
        xs = pit_value_cons_cdr(rt, xs);
    }
    return acc;
}
static pit_value impl_list_filter(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value func = pit_value_cons_car(rt, args);
    pit_value xs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL) {
        pit_value x = pit_value_cons_car(rt, xs);
        pit_value y = pit_value_apply(rt, func, pit_value_cons(rt, x, PIT_NIL));
        if (y != PIT_NIL) {
            ret = pit_value_cons(rt, x, ret);
        }
        xs = pit_value_cons_cdr(rt, xs);
    }
    return pit_value_list_reverse(rt, ret);
}
static pit_value impl_list_find(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value func = pit_value_cons_car(rt, args);
    pit_value xs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    while (xs != PIT_NIL) {
        pit_value x = pit_value_cons_car(rt, xs);
        pit_value y = pit_value_apply(rt, func, pit_value_cons(rt, x, PIT_NIL));
        if (y != PIT_NIL) {
            return x;
        }
        xs = pit_value_cons_cdr(rt, xs);
    }
    return PIT_NIL;
}
static pit_value impl_list_contains_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value needle = pit_value_cons_car(rt, args);
    pit_value haystack = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    while (haystack != PIT_NIL) {
        if (pit_value_equal(rt, needle, pit_value_cons_car(rt, haystack))) return PIT_T;
        haystack = pit_value_cons_cdr(rt, haystack);
    }
    return PIT_NIL;
}
static pit_value impl_list_all_p(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value f = pit_value_cons_car(rt, args);
    pit_value xs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    while (xs != PIT_NIL) {
        pit_value x = pit_value_cons_car(rt, xs);
        if (pit_value_apply(rt, f, pit_value_cons(rt, x, PIT_NIL)) == PIT_NIL) {
            return PIT_NIL;
        }
        xs = pit_value_cons_cdr(rt, xs);
    }
    return PIT_T;
}
static pit_value impl_list_zip_with(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value f = pit_value_cons_car(rt, args);
    pit_value xs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value ys = pit_value_cons_car(rt, pit_value_cons_cdr(rt, pit_value_cons_cdr(rt, args)));
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL && ys != PIT_NIL) {
        pit_value z = pit_value_apply(rt, f, pit_value_list(rt, 2, pit_value_cons_car(rt, xs), pit_value_cons_car(rt, ys)));
        ret = pit_value_cons(rt, z, ret);
        xs = pit_value_cons_cdr(rt, xs); ys = pit_value_cons_cdr(rt, ys);
    }
    return pit_value_list_reverse(rt, ret);
}
static pit_value impl_bytes_len(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value v = pit_value_cons_car(rt, args);
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "value is not a ref");
        return PIT_NIL;
    }
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) { pit_error(rt, "ref is not bytes"); return PIT_NIL; }
    return pit_value_integer_new(rt, h->in.bytes.len);
}
static pit_value impl_bytes_range(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 start = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    i64 end = pit_value_as_integer(rt, pit_value_cons_car(rt, pit_value_cons_cdr(rt, args)));
    pit_value v = pit_value_cons_car(rt, pit_value_cons_cdr(rt, pit_value_cons_cdr(rt, args)));
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "value is not a ref");
        return PIT_NIL;
    }
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, v));
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
    return pit_value_bytes_new(rt, h->in.bytes.data + start, end - start);
}
static pit_value impl_array(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 len = pit_value_list_len(rt, args);
    pit_value ret = pit_value_array_new(rt, len);
    i64 idx = 0;
    while (args != PIT_NIL) {
        pit_value_array_set(rt, ret, idx++, pit_value_cons_car(rt, args));
        args = pit_value_cons_cdr(rt, args);
    }
    return ret;
}
static pit_value impl_array_to_list(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value arr = pit_value_cons_car(rt, args);
    i64 ilen = pit_value_array_len(rt, arr);
    pit_value ret = PIT_NIL;
    i64 i = 0;
    for (; i < ilen; ++i) {
        ret = pit_value_cons(rt, pit_value_array_get(rt, arr, i), ret);
    }
    return pit_value_list_reverse(rt, ret);
}
static pit_value impl_array_from_list(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 i = 0;
    pit_value xs = pit_value_cons_car(rt, args);
    i64 ilen = pit_value_list_len(rt, xs);
    pit_value ret = pit_value_array_new(rt, ilen);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to deref heavy value for array"); return PIT_NIL; }
    while (xs != PIT_NIL) {
        h->in.array.data[i] = pit_value_cons_car(rt, xs);
        xs = pit_value_cons_cdr(rt, xs);
        i += 1;
    }
    return ret;
}
static pit_value impl_array_repeat(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 i = 0;
    pit_value v = pit_value_cons_car(rt, args);
    pit_value len = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    i64 ilen = pit_value_as_integer(rt, len);
    pit_value ret = pit_value_array_new(rt, ilen);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to deref heavy value for array"); return PIT_NIL; }
    for (; i < ilen; ++i) {
        h->in.array.data[i] = v;
    }
    return ret;
}
static pit_value impl_array_len(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value arr = pit_value_cons_car(rt, args);
    return pit_value_integer_new(rt, pit_value_array_len(rt, arr));
}
static pit_value impl_array_get(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value idx = pit_value_cons_car(rt, args);
    pit_value arr = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    return pit_value_array_get(rt, arr, pit_value_as_integer(rt, idx));
}
static pit_value impl_array_set(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value idx = pit_value_cons_car(rt, args);
    pit_value v = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    pit_value arr = pit_value_cons_car(rt, pit_value_cons_cdr(rt, pit_value_cons_cdr(rt, args)));
    return pit_value_array_set(rt, arr, pit_value_as_integer(rt, idx), v);
}
static pit_value impl_array_map(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value func = pit_value_cons_car(rt, args);
    pit_value arr = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    i64 len = pit_value_array_len(rt, arr);
    pit_value ret = pit_value_array_new(rt, len);
    i64 i = 0;
    for (i = 0; i < len; ++i) {
        pit_value y = pit_value_apply(rt, func, pit_value_cons(rt, pit_value_array_get(rt, arr, i), PIT_NIL));
        pit_value_array_set(rt, ret, i, y);
    }
    return ret;
}
static pit_value impl_array_map_mut(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value func = pit_value_cons_car(rt, args);
    pit_value arr = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    i64 len = pit_value_array_len(rt, arr);
    i64 i = 0;
    for (i = 0; i < len; ++i) {
        pit_value y = pit_value_apply(rt, func, pit_value_cons(rt, pit_value_array_get(rt, arr, i), PIT_NIL));
        pit_value_array_set(rt, arr, i, y);
    }
    return arr;
}
static pit_value impl_abs(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 x = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    if (x < 0) return pit_value_integer_new(rt, -x);
    return pit_value_integer_new(rt, x);
}
static pit_value impl_add(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 total = 0;
    while (args != PIT_NIL) {
        total += pit_value_as_integer(rt, pit_value_cons_car(rt, args));
        args = pit_value_cons_cdr(rt, args);
    }
    return pit_value_integer_new(rt, total);
}
static pit_value impl_sub(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 total = 0;
    while (args != PIT_NIL) {
        total -= pit_value_as_integer(rt, pit_value_cons_car(rt, args));
        args = pit_value_cons_cdr(rt, args);
    }
    return pit_value_integer_new(rt, total);
}
static pit_value impl_mul(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 total = 1;
    while (args != PIT_NIL) {
        total *= pit_value_as_integer(rt, pit_value_cons_car(rt, args));
        args = pit_value_cons_cdr(rt, args);
    }
    return pit_value_integer_new(rt, total);
}
static pit_value impl_div(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 total = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    args = pit_value_cons_cdr(rt, args);
    while (args != PIT_NIL) {
        i64 denom = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
        if (denom == 0) {
            pit_error(rt, "divide by zero");
            return PIT_NIL;
        }
        total /= denom;
        args = pit_value_cons_cdr(rt, args);
    }
    return pit_value_integer_new(rt, total);
}
static pit_value impl_not(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    if (pit_value_cons_car(rt, args) == PIT_NIL) {
        return PIT_T;
    } else {
        return PIT_NIL;
    }
}
static pit_value impl_lt(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 x = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    i64 y = pit_value_as_integer(rt, pit_value_cons_car(rt, pit_value_cons_cdr(rt, args)));
    return pit_value_bool_new(rt, x < y);
}
static pit_value impl_gt(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 x = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    i64 y = pit_value_as_integer(rt, pit_value_cons_car(rt, pit_value_cons_cdr(rt, args)));
    return pit_value_bool_new(rt, x > y);
}
static pit_value impl_le(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 x = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    i64 y = pit_value_as_integer(rt, pit_value_cons_car(rt, pit_value_cons_cdr(rt, args)));
    return pit_value_bool_new(rt, x <= y);
}
static pit_value impl_ge(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 x = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    i64 y = pit_value_as_integer(rt, pit_value_cons_car(rt, pit_value_cons_cdr(rt, args)));
    return pit_value_bool_new(rt, x >= y);
}
static pit_value impl_bitwise_and(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 total = -1;
    while (args != PIT_NIL) {
        total &= pit_value_as_integer(rt, pit_value_cons_car(rt, args));
        args = pit_value_cons_cdr(rt, args);
    }
    return pit_value_integer_new(rt, total);
}
static pit_value impl_bitwise_or(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 total = 0;
    while (args != PIT_NIL) {
        total |= pit_value_as_integer(rt, pit_value_cons_car(rt, args));
        args = pit_value_cons_cdr(rt, args);
    }
    return pit_value_integer_new(rt, total);
}
static pit_value impl_bitwise_xor(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 total = 0;
    while (args != PIT_NIL) {
        total ^= pit_value_as_integer(rt, pit_value_cons_car(rt, args));
        args = pit_value_cons_cdr(rt, args);
    }
    return pit_value_integer_new(rt, total);
}
static pit_value impl_bitwise_not(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 x = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    return pit_value_integer_new(rt, ~x);
}
static pit_value impl_bitwise_lshift(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 val = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    i64 shift = pit_value_as_integer(rt, pit_value_cons_car(rt, pit_value_cons_cdr(rt, args)));
    return pit_value_integer_new(rt, val << shift);
}
static pit_value impl_bitwise_rshift(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    i64 val = pit_value_as_integer(rt, pit_value_cons_car(rt, args));
    i64 shift = pit_value_as_integer(rt, pit_value_cons_car(rt, pit_value_cons_cdr(rt, args)));
    if (shift >= 64) val = 0;
    else val >>= shift;
    return pit_value_integer_new(rt, val);
}
void pit_install_library_essential(pit_runtime *rt) {
    /* special forms */
    pit_symtab_sfset(rt, pit_symtab_intern_cstr(rt, "quote"), pit_value_nativefunc_new(rt, impl_sf_quote));
    pit_symtab_sfset(rt, pit_symtab_intern_cstr(rt, "if"), pit_value_nativefunc_new(rt, impl_sf_if));
    pit_symtab_sfset(rt, pit_symtab_intern_cstr(rt, "cond"), pit_value_nativefunc_new(rt, impl_sf_cond));
    pit_symtab_sfset(rt, pit_symtab_intern_cstr(rt, "progn"), pit_value_nativefunc_new(rt, impl_sf_progn));
    pit_symtab_sfset(rt, pit_symtab_intern_cstr(rt, "or"), pit_value_nativefunc_new(rt, impl_sf_or));
    pit_symtab_sfset(rt, pit_symtab_intern_cstr(rt, "lambda"), pit_value_nativefunc_new(rt, impl_sf_lambda));
    /* macros */
    pit_symtab_mset(rt, pit_symtab_intern_cstr(rt, "defun!"), pit_value_nativefunc_new(rt, impl_m_defun));
    pit_symtab_mset(rt, pit_symtab_intern_cstr(rt, "defmacro!"), pit_value_nativefunc_new(rt, impl_m_defmacro));
    pit_symtab_mset(rt, pit_symtab_intern_cstr(rt, "defstruct!"), pit_value_nativefunc_new(rt, impl_m_defstruct));
    pit_symtab_mset(rt, pit_symtab_intern_cstr(rt, "let"), pit_value_nativefunc_new(rt, impl_m_let));
    pit_symtab_mset(rt, pit_symtab_intern_cstr(rt, "and"), pit_value_nativefunc_new(rt, impl_m_and));
    pit_symtab_mset(rt, pit_symtab_intern_cstr(rt, "setq!"), pit_value_nativefunc_new(rt, impl_m_setq));
    pit_symtab_mset(rt, pit_symtab_intern_cstr(rt, "case"), pit_value_nativefunc_new(rt, impl_m_case));
    /* error */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "error!"), pit_value_nativefunc_new(rt, impl_error));
    /* eval */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "eval!"), pit_value_nativefunc_new(rt, impl_eval));
    /* predicates */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "eq?"), pit_value_nativefunc_new(rt, impl_eq_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "equal?"), pit_value_nativefunc_new(rt, impl_equal_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "integer?"), pit_value_nativefunc_new(rt, impl_integer_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "double?"), pit_value_nativefunc_new(rt, impl_double_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "symbol?"), pit_value_nativefunc_new(rt, impl_symbol_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "cons?"), pit_value_nativefunc_new(rt, impl_cons_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array?"), pit_value_nativefunc_new(rt, impl_array_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bytes?"), pit_value_nativefunc_new(rt, impl_bytes_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "function?"), pit_value_nativefunc_new(rt, impl_function_p));
    /* symbols */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "set!"), pit_value_nativefunc_new(rt, impl_set));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "fset!"), pit_value_nativefunc_new(rt, impl_fset));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "symbol-is-macro!"), pit_value_nativefunc_new(rt, impl_symbol_mark_macro));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "funcall"), pit_value_nativefunc_new(rt, impl_funcall));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "apply"), pit_value_nativefunc_new(rt, impl_apply));
    /* cons cells */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "cons"), pit_value_nativefunc_new(rt, impl_cons));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "car"), pit_value_nativefunc_new(rt, impl_car));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "cdr"), pit_value_nativefunc_new(rt, impl_cdr));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "setcar!"), pit_value_nativefunc_new(rt, impl_setcar));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "setcdr!"), pit_value_nativefunc_new(rt, impl_setcdr));
    /* cons lists*/
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list"), pit_value_nativefunc_new(rt, impl_list));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/nth"), pit_value_nativefunc_new(rt, impl_list_nth));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/iota"), pit_value_nativefunc_new(rt, impl_list_iota));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/len"), pit_value_nativefunc_new(rt, impl_list_len));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/reverse"), pit_value_nativefunc_new(rt, impl_list_reverse));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/uniq"), pit_value_nativefunc_new(rt, impl_list_uniq));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/append"), pit_value_nativefunc_new(rt, impl_list_append));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/concat"), pit_value_nativefunc_new(rt, impl_list_concat));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/take"), pit_value_nativefunc_new(rt, impl_list_take));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/drop"), pit_value_nativefunc_new(rt, impl_list_drop));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/map"), pit_value_nativefunc_new(rt, impl_list_map));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/foldl"), pit_value_nativefunc_new(rt, impl_list_foldl));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/filter"), pit_value_nativefunc_new(rt, impl_list_filter));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/find"), pit_value_nativefunc_new(rt, impl_list_find));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/contains?"), pit_value_nativefunc_new(rt, impl_list_contains_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/all?"), pit_value_nativefunc_new(rt, impl_list_all_p));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "list/zip-with"), pit_value_nativefunc_new(rt, impl_list_zip_with));
    /* bytestrings */ 
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bytes/len"), pit_value_nativefunc_new(rt, impl_bytes_len));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bytes/range"), pit_value_nativefunc_new(rt, impl_bytes_range));
    /* array */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array"), pit_value_nativefunc_new(rt, impl_array));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array/to-list"), pit_value_nativefunc_new(rt, impl_array_to_list));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array/from-list"), pit_value_nativefunc_new(rt, impl_array_from_list));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array/repeat"), pit_value_nativefunc_new(rt, impl_array_repeat));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array/len"), pit_value_nativefunc_new(rt, impl_array_len));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array/get"), pit_value_nativefunc_new(rt, impl_array_get));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array/set!"), pit_value_nativefunc_new(rt, impl_array_set));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array/map"), pit_value_nativefunc_new(rt, impl_array_map));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "array/map!"), pit_value_nativefunc_new(rt, impl_array_map_mut));
    /* arithmetic */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "abs"), pit_value_nativefunc_new(rt, impl_abs));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "+"), pit_value_nativefunc_new(rt, impl_add));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "-"), pit_value_nativefunc_new(rt, impl_sub));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "*"), pit_value_nativefunc_new(rt, impl_mul));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "/"), pit_value_nativefunc_new(rt, impl_div));
    /* booleans */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "not"), pit_value_nativefunc_new(rt, impl_not));
    /* comparisons */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "<"), pit_value_nativefunc_new(rt, impl_lt));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, ">"), pit_value_nativefunc_new(rt, impl_gt));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "<="), pit_value_nativefunc_new(rt, impl_le));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, ">="), pit_value_nativefunc_new(rt, impl_ge));
    /* bitwise arithmetic */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bitwise/and"), pit_value_nativefunc_new(rt, impl_bitwise_and));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bitwise/or"), pit_value_nativefunc_new(rt, impl_bitwise_or));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bitwise/xor"), pit_value_nativefunc_new(rt, impl_bitwise_xor));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bitwise/not"), pit_value_nativefunc_new(rt, impl_bitwise_not));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bitwise/lshift"), pit_value_nativefunc_new(rt, impl_bitwise_lshift));
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "bitwise/rshift"), pit_value_nativefunc_new(rt, impl_bitwise_rshift));
}

static pit_value impl_plist_get(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value k = pit_value_cons_car(rt, args);
    pit_value vs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    return pit_value_list_plist_get(rt, k, vs);
}
void pit_install_library_plist(pit_runtime *rt) {
    /* property lists / keyword arguments */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "plist/get"), pit_value_nativefunc_new(rt, impl_plist_get));
}

static pit_value impl_alist_get(pit_runtime *rt, pit_value args, void *data) {
    (void) data;
    pit_value k = pit_value_cons_car(rt, args);
    pit_value vs = pit_value_cons_car(rt, pit_value_cons_cdr(rt, args));
    while (vs != PIT_NIL) {
        pit_value v = pit_value_cons_car(rt, vs);
        if (pit_value_equal(rt, k, pit_value_cons_car(rt, v))) {
            return pit_value_cons_cdr(rt, v);
        }
        vs = pit_value_cons_cdr(rt, vs);
    }
    return PIT_NIL;
}
void pit_install_library_alist(pit_runtime *rt) {
    /* association lists */
    pit_symtab_fset(rt, pit_symtab_intern_cstr(rt, "alist/get"), pit_value_nativefunc_new(rt, impl_alist_get));
}
