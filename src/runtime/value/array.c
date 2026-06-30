#include <lcq/pit/runtime/value/array.h>

bool pit_value_is_array(pit_runtime *rt, pit_value a) {
    return pit_value_is_ref_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_ARRAY);
}

pit_value pit_value_array_new(pit_runtime *rt, i64 len) {
    if (len < 0) { pit_error(rt, "failed to create array of negative size"); return PIT_NIL; }
    i64 byte_len = 0; pit_mul(&byte_len, sizeof(pit_value), len);
    pit_value *dest = pit_arena_alloc_array(rt->heap, byte_len);
    if (!dest) { pit_error(rt, "failed to allocate array"); return PIT_NIL; }
    for (i64 i = 0; i < len; ++i) dest[i] = PIT_NIL;
    pit_value ret = pit_value_ref_heavy_new(rt);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for array"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_ARRAY;
    h->in.array.data = dest;
    h->in.array.len = len;
    return ret;
}
pit_value pit_value_array_from_buf(pit_runtime *rt, pit_value *xs, i64 len) {
    pit_value ret = pit_value_array_new(rt, len);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to deref heavy value for array"); return PIT_NIL; }
    pit_libc_string_memcpy((u8 *) h->in.array.data, (u8 *) xs, (size_t) len * (size_t) sizeof(pit_value));
    return ret;
}
i64 pit_value_array_len(pit_runtime *rt, pit_value arr) {
    if (pit_value_sort(arr) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return -1; }
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, arr));
    if (!h) { pit_error(rt, "bad ref"); return -1; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_ARRAY) { pit_error(rt, "not an array"); return -1; }
    return h->in.array.len;
}
pit_value pit_value_array_get(pit_runtime *rt, pit_value arr, i64 idx) {
    if (pit_value_sort(arr) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return PIT_NIL; }
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, arr));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_ARRAY) { pit_error(rt, "not an array"); return PIT_NIL; }
    if (idx < 0 || idx >= h->in.array.len) {
        pit_error(rt, "array index out of bounds: %d", idx);
        return PIT_NIL;
    }
    return h->in.array.data[idx];
}
pit_value pit_value_array_set(pit_runtime *rt, pit_value arr, i64 idx, pit_value v) {
    if (pit_value_sort(arr) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return PIT_NIL; }
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, arr));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_ARRAY) { pit_error(rt, "not an array"); return PIT_NIL; }
    if (idx < 0 || idx >= h->in.array.len) {
        pit_error(rt, "array index out of bounds: %d", idx);
        return PIT_NIL;
    }
    h->in.array.data[idx] = v;
    return v;
}
