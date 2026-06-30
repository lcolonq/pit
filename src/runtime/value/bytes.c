#include <lcq/pit/runtime/value/bytes.h>

bool pit_value_is_bytes(pit_runtime *rt, pit_value a) {
    return pit_value_is_ref_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_BYTES);
}
pit_value pit_value_bytes_new(pit_runtime *rt, u8 *buf, i64 len) {
    u8 *dest = pit_arena_alloc_back(rt->heap, len);
    if (!dest) { pit_error(rt, "failed to allocate bytes"); return PIT_NIL; }
    pit_libc_string_memcpy(dest, buf, (size_t) len);
    pit_value ret = pit_value_ref_heavy_new(rt);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for bytes"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_BYTES;
    h->in.bytes.data = dest;
    h->in.bytes.len = len;
    return ret;
}
pit_value pit_value_bytes_new_cstr(pit_runtime *rt, char *s) {
    return pit_value_bytes_new(rt, (u8 *) s, (i64) pit_libc_string_strlen(s));
}
/* return true if v is a reference to bytes that are the same as those in buf */
bool pit_value_bytes_match(pit_runtime *rt, pit_value v, u8 *buf, i64 len) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return false;
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return false; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) return false;
    if (h->in.bytes.len != len) return false;
    for (i64 i = 0; i < len; ++i)
        if (h->in.bytes.data[i] != buf[i]) {
            return false;
        }
    return true;
}
i64 pit_value_bytes_copy(pit_runtime *rt, pit_value v, u8 *buf, i64 maxlen) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return -1; }
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return -1; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) {
        pit_error(rt, "invalid use of value as bytes");
        return -1;
    }
    i64 len = maxlen < h->in.bytes.len ? maxlen : h->in.bytes.len;
    for (i64 i = 0; i < len; ++i) {
        buf[i] = h->in.bytes.data[i];
    }
    return len;
}
