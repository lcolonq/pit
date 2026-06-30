#include <lcq/pit/runtime/value/small.h>

#ifndef PIT_NO_DOUBLE
double pit_value_as_double(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_DOUBLE) {
        pit_error(rt, "invalid use of value as double");
        return 0.0;
    }
    union { double dval; u64 ival; } x;
    x.ival = v;
    return x.dval;
}
bool pit_value_is_double(pit_runtime *rt, pit_value a) {
    (void) rt;
    return pit_value_sort(a) == PIT_VALUE_SORT_DOUBLE;
}
pit_value pit_value_double_new(pit_runtime *rt, double d) {
    union { double dval; u64 ival; } x;
    x.dval = d;
    return pit_value_new(rt, PIT_VALUE_SORT_DOUBLE, x.ival);
}
#endif

i64 pit_value_as_integer(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_INTEGER) {
        pit_error(rt, "invalid use of value as integer");
        return -1;
    }
    u64 lo = pit_value_data(v);
    return ((i64) (lo << 15)) >> 15; /* sign-extend low 49 bits */

}
bool pit_value_is_integer(pit_runtime *rt, pit_value a) {
    (void) rt;
    return pit_value_sort(a) == PIT_VALUE_SORT_INTEGER;
}
pit_value pit_value_integer_new(pit_runtime *rt, i64 i) {
    return pit_value_new(rt, PIT_VALUE_SORT_INTEGER, 0x1ffffffffffff & (u64) i);
}
pit_value pit_value_bool_new(pit_runtime *rt, bool i) {
    (void) rt;
    return i ? PIT_T : PIT_NIL;
}

pit_symbol pit_value_as_symbol(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_SYMBOL) {
        pit_error(rt, "invalid use of value as symbol");
        return -1;
    }
    return (pit_symbol) (pit_value_data(v) & 0xffffffff);
}
bool pit_value_is_symbol(pit_runtime *rt, pit_value a) {
    (void) rt;
    return pit_value_sort(a) == PIT_VALUE_SORT_SYMBOL;
}
pit_value pit_value_symbol_new(pit_runtime *rt, pit_symbol s) {
    return pit_value_new(rt, PIT_VALUE_SORT_SYMBOL, (u64) s);
}

pit_ref pit_value_as_ref(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "invalid use of value as ref");
        return -1;
    }
    return (pit_ref) (pit_value_data(v) & 0xffffffff);
}
bool pit_value_is_ref(pit_runtime *rt, pit_value a) {
    (void) rt;
    return pit_value_sort(a) == PIT_VALUE_SORT_REF;
}
pit_value pit_value_ref_new(pit_runtime *rt, pit_ref r) {
    return pit_value_new(rt, PIT_VALUE_SORT_REF, (u64) r);
}
pit_value pit_value_ref_heavy_new(pit_runtime *rt) {
    pit_arena_index idx = pit_arena_alloc_index(rt->heap);
    if (idx < 0) {
        pit_error(rt, "failed to allocate space for heavy value");
        return PIT_NIL;
    }
    return pit_value_ref_new(rt, idx);
}
pit_value_heavy *pit_value_ref_deref(pit_runtime *rt, pit_ref p) {
    return pit_arena_get(rt->heap, p);
}
bool pit_value_is_ref_heavy_sort(pit_runtime *rt, pit_value a, enum pit_value_heavy_sort e) {
    switch (pit_value_sort(a)) {
    case PIT_VALUE_SORT_REF: {
        pit_value_heavy *ha = pit_value_ref_deref(rt, pit_value_as_ref(rt, a));
        if (!ha) { pit_error(rt, "bad ref"); return false; }
        return ha->hsort == e;
    }
    default:
        break;
    }
    return false;
}
