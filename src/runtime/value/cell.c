#include <lcq/pit/runtime/value/cell.h>

bool pit_value_is_cell(pit_runtime *rt, pit_value a) {
    return pit_value_is_ref_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_CELL);
}
pit_value pit_value_cell_new(pit_runtime *rt, pit_value v) {
    pit_value ret = pit_value_ref_heavy_new(rt);
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for cell"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_CELL;
    h->in.cell = v;
    return ret;
}
pit_value pit_value_cell_get(pit_runtime *rt, pit_value cell, pit_value sym) {
    if (pit_value_sort(cell) != PIT_VALUE_SORT_REF) {
        char buf[256];
        i64 end = pit_dump(rt, buf, sizeof(buf) - 1, sym, false);
        buf[end] = 0;
        pit_error(rt, "attempted to get unbound variable/function: %s", buf);
        return PIT_NIL;
    }
    pit_value_heavy *h = pit_value_ref_deref(rt, pit_value_as_ref(rt, cell));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CELL) {
        pit_error(rt, "cell value ref does not point to cell");
        return PIT_NIL;
    }
    return h->in.cell;
}
void pit_value_cell_set(pit_runtime *rt, pit_value cell, pit_value v, pit_value sym) {
    if (pit_value_sort(cell) != PIT_VALUE_SORT_REF) {
        char buf[256];
        i64 end = pit_dump(rt, buf, sizeof(buf) - 1, sym, false);
        buf[end] = 0;
        pit_error(rt, "attempted to set unbound variable/function: %s", buf);
        return;
    }
    pit_ref idx = pit_value_as_ref(rt, cell);
    if (idx < rt->frozen_values) { pit_error(rt, "attempt to modify frozen cell"); return; }
    pit_value_heavy *h = pit_value_ref_deref(rt, idx);
    if (!h) { pit_error(rt, "bad ref"); return; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CELL) {
        pit_error(rt, "cell value ref does not point to cell");
        return;
    }
    h->in.cell = v;
}
