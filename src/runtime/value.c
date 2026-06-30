#include <lcq/pit/runtime/value.h>

pit_value pit_value_new(pit_runtime *rt, enum pit_value_sort s, u64 data) {
    if (s == PIT_VALUE_SORT_DOUBLE) {
        /* if (((data >> 52) & 0b011111111111) == 0b011111111111 && ((data >> 51) & 0b1) == 0) { */
        if (((data >> 52) & 0x7ff) == 0x7ff && ((data >> 51) & 1) == 0) {
            pit_error(rt, "attempted to create a signalling NaN double");
            return PIT_NIL;
        }
        return data;
    }
    return
        /* 0b1111111111110000000000000000000000000000000000000000000000000000 */
        0xfff0000000000000
        /* | (((u64) (s & 0b11)) << 49) */
        | (((u64) (s & 3)) << 49)
        /* | (data & 0b1111111111111111111111111111111111111111111111111); */
        | (data & 0x1ffffffffffff);
}

bool pit_value_eq(pit_value a, pit_value b) {
    return a == b;
}

bool pit_value_equal(pit_runtime *rt, pit_value a, pit_value b) {
    if (pit_value_sort(a) != pit_value_sort(b)) return false;
    switch (pit_value_sort(a)) {
    case PIT_VALUE_SORT_DOUBLE:
    case PIT_VALUE_SORT_INTEGER:
    case PIT_VALUE_SORT_SYMBOL:
        return pit_value_data(a) == pit_value_data(b);
    case PIT_VALUE_SORT_REF: {
        pit_value_heavy *ha = pit_value_ref_deref(rt, pit_value_as_ref(rt, a));
        if (!ha) { pit_error(rt, "bad ref"); return false; }
        pit_value_heavy *hb = pit_value_ref_deref(rt, pit_value_as_ref(rt, b));
        if (!hb) { pit_error(rt, "bad ref"); return false; }
        if (ha->hsort != hb->hsort) return false;
        switch (ha->hsort) {
        case PIT_VALUE_HEAVY_SORT_CELL:
            return pit_value_equal(rt, ha->in.cell, hb->in.cell);
        case PIT_VALUE_HEAVY_SORT_CONS:
            return pit_value_equal(rt, ha->in.cons.car, hb->in.cons.car)
                && pit_value_equal(rt, ha->in.cons.cdr, hb->in.cons.cdr);
        case PIT_VALUE_HEAVY_SORT_ARRAY: {
            if (ha->in.array.len != hb->in.array.len) return false;
            for (i64 i = 0; i < ha->in.array.len; ++i) {
                if (!pit_value_equal(rt, ha->in.array.data[i], hb->in.array.data[i])) return false;
            }
            return true;
        }
        case PIT_VALUE_HEAVY_SORT_BYTES: {
            if (ha->in.bytes.len != hb->in.bytes.len) return false;
            for (i64 i = 0; i < ha->in.bytes.len; ++i) {
                if (ha->in.bytes.data[i] != hb->in.bytes.data[i]) return false;
            }
            return true;
        }
        case PIT_VALUE_HEAVY_SORT_FUNC:
            return
                pit_value_equal(rt, ha->in.func.env, hb->in.func.env)
                && pit_value_equal(rt, ha->in.func.args, hb->in.func.args)
                && pit_value_equal(rt, ha->in.func.body, hb->in.func.body);
        case PIT_VALUE_HEAVY_SORT_NATIVEFUNC:
            return ha->in.nativefunc.f == hb->in.nativefunc.f
                && ha->in.nativefunc.data == hb->in.nativefunc.data;
        case PIT_VALUE_HEAVY_SORT_NATIVEDATA:
            return
                pit_value_eq(ha->in.nativedata.tag, hb->in.nativedata.tag)
                && ha->in.nativedata.data == hb->in.nativedata.data;
        case PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER:
            return ha->in.forwarding_pointer == hb->in.forwarding_pointer;
        }
    }
    }
    return false;
}
