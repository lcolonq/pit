#include <lcq/pit/runtime/gc.h>

static i64 gc_copy(pit_runtime *rt, pit_value_heavy *h) {
    if (h->hsort == PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER) {
        return h->in.forwarding_pointer;
    } else {
        i64 ret = rt->backbuffer->next;
        pit_value_heavy *g = pit_arena_alloc(rt->backbuffer);
        *g = *h;
        h->hsort = PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER;
        h->in.forwarding_pointer = ret;
        return ret;
    }
}
static pit_value gc_copy_value(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) == PIT_VALUE_SORT_REF) {
        pit_ref r = pit_value_as_ref(rt, v);
        pit_value_heavy *h = pit_value_ref_deref(rt, r);
        i64 new = gc_copy(rt, h);
        pit_annotated_ref *ann = pit_annotation_get(rt, r);
        if (ann != NULL) {
            pit_annotated_ref newann = *ann;
            newann.ref = new;
            if (pit_vec_push(pit_annotated_ref)(rt->backtrace, newann) < 0)
                pit_error(rt, "annotation overflow");
        }
        return pit_value_ref_new(rt, new);
    } else {
        return v;
    }
}
void pit_gc(pit_runtime *rt) {
    rt->frozen_values = 0;
    rt->frozen_symtab = 0;
    pit_arena *fromspace = rt->heap;
    pit_arena *tospace = rt->backbuffer;
    pit_vec(pit_annotated_ref) *fromspace_ann = rt->annotations;
    pit_vec(pit_annotated_ref) *tospace_ann = rt->backtrace;
    pit_arena_reset(tospace);
    pit_vec_reset(pit_annotated_ref)(tospace_ann);
    /* populate tospace with immediately reachable values */
    for (i64 i = 0; i < rt->symtab->next; ++i) {
        pit_symtab_entry *ent = pit_vec_get(pit_symtab_entry)(rt->symtab, i);
        if (ent == NULL) continue; /* TODO warn on failure here? */
        ent->name = gc_copy_value(rt, ent->name);
        ent->value = gc_copy_value(rt, ent->value);
        ent->function = gc_copy_value(rt, ent->function);
    }
    for (i64 i = 0; i < rt->saved_bindings->next; ++i) {
        pit_value *v = pit_vec_get(pit_value)(rt->saved_bindings, i);
        if (v != NULL) *v = gc_copy_value(rt, *v); /* TODO warn on failure here? */
    }
    for (i64 scan = 0; scan < tospace->next; ++scan) {
        pit_value_heavy *h = pit_arena_get(tospace, scan);
        switch (h->hsort) {
        case PIT_VALUE_HEAVY_SORT_CELL:
            h->in.cell = gc_copy_value(rt, h->in.cell);
            break;
        case PIT_VALUE_HEAVY_SORT_CONS:
            h->in.cons.car = gc_copy_value(rt, h->in.cons.car);
            h->in.cons.cdr = gc_copy_value(rt, h->in.cons.cdr);
            break;
        case PIT_VALUE_HEAVY_SORT_ARRAY: {
            i64 byte_len = 0; pit_mul(&byte_len, sizeof(pit_value), h->in.array.len);
            pit_value *data = pit_arena_alloc_back(tospace, byte_len);
            for (i64 i = 0; i < h->in.array.len; ++i) {
                data[i] = gc_copy_value(rt, h->in.array.data[i]);
            }
            h->in.array.data = data;
            break;
        }
        case PIT_VALUE_HEAVY_SORT_BYTES: {
            u8 *data = pit_arena_alloc_back(tospace, h->in.bytes.len);
            for (i64 i = 0; i < h->in.bytes.len; ++i) {
                data[i] = h->in.bytes.data[i];
            }
            h->in.bytes.data = data;
            break;
        }
        case PIT_VALUE_HEAVY_SORT_FUNC:
            h->in.func.env = gc_copy_value(rt, h->in.func.env);
            h->in.func.args = gc_copy_value(rt, h->in.func.args);
            h->in.func.arg_rest_nm = gc_copy_value(rt, h->in.func.arg_rest_nm);
            h->in.func.body = gc_copy_value(rt, h->in.func.body);
            break;
        case PIT_VALUE_HEAVY_SORT_NATIVEFUNC: break;
        case PIT_VALUE_HEAVY_SORT_NATIVEDATA:
            h->in.nativedata.tag = gc_copy_value(rt, h->in.nativedata.tag);
            break;
        case PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER:
            pit_error(rt, "garbage collection broken! encountered forwarding pointer in to-space");
            break;
        }
    }
    rt->heap = tospace;
    rt->backbuffer = fromspace;
    rt->annotations = tospace_ann;
    rt->backtrace = fromspace_ann;
    pit_vec_reset(pit_annotated_ref)(rt->backtrace);
}
