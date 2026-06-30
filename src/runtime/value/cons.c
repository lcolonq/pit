#include <lcq/pit/runtime/value/cons.h>

bool pit_value_is_cons(pit_runtime *rt, pit_value a) {
    return pit_value_is_ref_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_CONS);
}
pit_value pit_value_cons(pit_runtime *rt, pit_value car, pit_value cdr) {
    pit_value ret = pit_value_ref_heavy_new(rt);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for cons"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_CONS;
    h->in.cons.car = car;
    h->in.cons.cdr = cdr;
    return ret;
}
pit_value pit_value_cons_car(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return PIT_NIL;
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) return PIT_NIL;
    return h->in.cons.car;
}
pit_value pit_value_cons_cdr(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return PIT_NIL;
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) return PIT_NIL;
    return h->in.cons.cdr;
}
void pit_value_cons_setcar(pit_runtime *rt, pit_value v, pit_value x) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return; }
    pit_ref idx = pit_value_as_ref(rt, v);
    if (idx < rt->frozen_values) { pit_error(rt, "attempted to modify frozen cons"); return; }
    pit_value_heavy *h = pit_value_ref_deref(rt, idx);
    if (!h) { pit_error(rt, "bad ref"); return; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) { pit_error(rt, "not a cons"); return; }
    h->in.cons.car = x;
}
void pit_value_cons_setcdr(pit_runtime *rt, pit_value v, pit_value x) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return; }
    pit_ref idx = pit_value_as_ref(rt, v);
    if (idx < rt->frozen_values) { pit_error(rt, "attempted to modify frozen cons"); return; }
    pit_value_heavy *h = pit_value_ref_deref(rt, idx);
    if (!h) { pit_error(rt, "bad ref"); return; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) { pit_error(rt, "not a cons"); return; }
    h->in.cons.cdr = x;
}

pit_value pit_value_list(pit_runtime *rt, i64 num, ...) {
    pit_value temp[64] = {0};
    pit_value ret = PIT_NIL;
    if (num > 64) { pit_error(rt, "failed to create list of size %d\n", num); return PIT_NIL; }
    va_list elems;
    va_start(elems, num);
    for (i64 i = 0; i < num; ++i) {
        temp[i] = va_arg(elems, pit_value);
    }
    va_end(elems);
    for (i64 i = 0; i < num; ++i) {
        ret = pit_value_cons(rt, temp[num - i - 1], ret);
    }
    return ret;
}
i64 pit_value_list_len(pit_runtime *rt, pit_value xs) {
    i64 ret = 0;
    while (xs != PIT_NIL) {
        ret += 1;
        xs = pit_value_cons_cdr(rt, xs);
    }
    return ret;
}
pit_value pit_value_list_append(pit_runtime *rt, pit_value xs, pit_value ys) {
    pit_value ret = ys;
    xs = pit_value_list_reverse(rt, xs);
    while (xs != PIT_NIL) {
        ret = pit_value_cons(rt, pit_value_cons_car(rt, xs), ret);
        xs = pit_value_cons_cdr(rt, xs);
    }
    return ret;
}
pit_value pit_value_list_reverse(pit_runtime *rt, pit_value xs) {
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL) {
        ret = pit_value_cons(rt, pit_value_cons_car(rt, xs), ret);
        xs = pit_value_cons_cdr(rt, xs);
    }
    return ret;
}
pit_value pit_value_list_contains_eq(pit_runtime *rt, pit_value needle, pit_value haystack) {
    while (haystack != PIT_NIL) {
        if (pit_value_eq(needle, pit_value_cons_car(rt, haystack))) return PIT_T;
        haystack = pit_value_cons_cdr(rt, haystack);
    }
    return PIT_NIL;
}
pit_value pit_value_list_contains_equal(pit_runtime *rt, pit_value needle, pit_value haystack) {
    while (haystack != PIT_NIL) {
        if (pit_value_equal(rt, needle, pit_value_cons_car(rt, haystack))) return PIT_T;
        haystack = pit_value_cons_cdr(rt, haystack);
    }
    return PIT_NIL;
}
pit_value pit_value_list_plist_get(pit_runtime *rt, pit_value k, pit_value vs) {
    while (vs != PIT_NIL) {
        if (pit_value_eq(k, pit_value_cons_car(rt, vs))) {
            return pit_value_cons_car(rt, pit_value_cons_cdr(rt, vs));
        }
        vs = pit_value_cons_cdr(rt, vs);
    }
    return PIT_NIL;
}
