#include <lcq/pit/runtime/value/nativedata.h>

bool pit_value_is_nativedata(pit_runtime *rt, pit_value a) {
    return pit_value_is_ref_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_NATIVEDATA);
}
pit_value pit_value_nativedata_new(pit_runtime *rt, pit_value tag, void *d) {
    pit_value ret = pit_value_ref_heavy_new(rt);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for nativedata"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_NATIVEDATA;
    h->in.nativedata.tag = tag;
    h->in.nativedata.data = d;
    return ret;
}
void *pit_value_nativedata_get(pit_runtime *rt, pit_value tag, pit_value v) {
    pit_value_heavy *h = NULL;
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "value was not a reference");
        return NULL;
    }
    h = pit_value_ref_deref(rt, pit_value_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return NULL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_NATIVEDATA) {
        pit_error(rt, "invalid use of value as nativedata");
        return NULL;
    }
    if (!pit_value_eq(h->in.nativedata.tag, tag)) {
        pit_error(rt, "native value does not match tag");
        return NULL;
    }
    if (!h->in.nativedata.data) {
        pit_error(rt, "nativedata was already freed");
        return NULL;
    }
    return h->in.nativedata.data;
}
