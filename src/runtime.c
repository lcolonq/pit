#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "utils.h"
#include "runtime.h"

pit_arena *pit_arena_new(i64 capacity, i64 elem_size) {
    pit_arena *a = malloc(sizeof(pit_arena) + capacity * elem_size);
    a->elem_size = elem_size;
    a->capacity = capacity;
    a->next = 0;
    return a;
}

i32 pit_arena_alloc_idx(pit_arena *a) {
    i32 byte_idx; pit_mul(&byte_idx, a->elem_size, a->next);
    if (byte_idx >= a->capacity) { return -1; }
    a->next += 1;
    return byte_idx;
}

i32 pit_arena_alloc_bulk_idx(pit_arena *a, i64 num) {
    i32 byte_idx; pit_mul(&byte_idx, a->elem_size, a->next);
    i32 byte_len; pit_mul(&byte_len, a->elem_size, num);
    if (byte_idx + byte_len > a->capacity) { return -1; }
    a->next += num;
    return byte_idx;
}

void *pit_arena_idx(pit_arena *a, i32 idx) {
    if (idx < 0 || idx >= a->capacity) return NULL;
    return &a->data[idx];
}

void *pit_arena_alloc(pit_arena *a) {
    i32 byte_idx = pit_arena_alloc_idx(a);
    return pit_arena_idx(a, byte_idx);
}

void *pit_arena_alloc_bulk(pit_arena *a, i64 num) {
    i32 byte_idx = pit_arena_alloc_bulk_idx(a, num);
    return pit_arena_idx(a, byte_idx);
}

enum pit_value_sort pit_value_sort(pit_value v) {
    // if this isn't a NaN, or it's a quiet NaN, this is a real double
    if (((v >> 52) & 0b011111111111) != 0b011111111111 || ((v >> 51) & 0b1) == 1) return PIT_VALUE_SORT_DOUBLE;
    // otherwise, we've packed something else in the significand
    //   0 for signaling NaN -+
    //     sign --+ +- 1 (NaN)| +- our sort tag       + our data
    //            | |         | |                     |
    //            s111111111110ttddddddddddddddddddddddddddddddddddddddddddddddddd
    return (v & 0b0000000000000110000000000000000000000000000000000000000000000000) >> 49;
}

u64 pit_value_data(pit_value v) {
    return  v & 0b0000000000000001111111111111111111111111111111111111111111111111;
}

pit_runtime *pit_runtime_new() {
    pit_runtime *ret = malloc(sizeof(*ret));
    ret->values = pit_arena_new(64 * 1024, sizeof(pit_value_heavy));
    ret->bytes = pit_arena_new(64 * 1024, sizeof(u8));
    ret->symtab = pit_arena_new(1024, sizeof(pit_symtab_entry));
    ret->symtab_len = 0;
    ret->error = PIT_NIL;
    pit_intern_cstr(ret, "nil");
    return ret;
}

i64 pit_dump(pit_runtime *rt, char *buf, i64 len, pit_value v) {
    pit_value_heavy *h = NULL;
    if (len <= 0) return 0;
    switch (pit_value_sort(v)) {
    case PIT_VALUE_SORT_DOUBLE:
        return snprintf(buf, len, "%lf", pit_as_double(rt, v));
    case PIT_VALUE_SORT_INTEGER:
        return snprintf(buf, len, "%ld", pit_as_integer(rt, v));
    case PIT_VALUE_SORT_SYMBOL:
        pit_symtab_entry *ent = pit_symtab_lookup(rt, v);
        if (ent
            && pit_value_sort(ent->name) == PIT_VALUE_SORT_REF
            && (h = pit_deref(rt, pit_as_ref(rt, ent->name)))
        ) {
            i64 i = 0;
            for (; i < h->bytes.len && i < len - 1; ++i) {
                buf[i] = h->bytes.data[i];
            }
            return i;
        } else {
            return snprintf(buf, len, "<broken symbol %d>", pit_as_symbol(rt, v));
        }
    case PIT_VALUE_SORT_REF:
        pit_ref r = pit_as_ref(rt, v);
        h = pit_deref(rt, r);
        if (!h) snprintf(buf, len, "<ref %d>", r);
        else {
            switch (h->hsort) {
            case PIT_VALUE_HEAVY_SORT_CONS:
                char *end = buf + len;
                char *start = buf;
                pit_value cur = v;
                do {
                    if (pit_is_cons(rt, cur)) {
                        *(buf++) = ' '; if (buf >= end) return end - buf;
                        buf += pit_dump(rt, buf, end - buf, pit_car(rt, cur));
                        if (buf >= end) return end - buf;
                    } else {
                        buf += snprintf(buf, end - buf, " . ");
                        if (buf >= end) return end - buf;
                        buf += pit_dump(rt, buf, end - buf, cur);
                        if (buf >= end) return end - buf;
                    }
                } while (!pit_eq((cur = pit_cdr(rt, cur)), PIT_NIL));
                *start = '(';
                *(buf++) = ')';
                return buf - start;
            case PIT_VALUE_HEAVY_SORT_BYTES:
                buf[0] = '"';
                i64 i = 1;
                for (i64 j = 0; i < len - 1 && j < h->bytes.len;) {
                    if (buf[i - 1] != '\\' && (h->bytes.data[j] == '\\' || h->bytes.data[j] == '"'))
                        buf[i++] = '\\';
                    else buf[i++] = h->bytes.data[j++];
                }
                if (i < len - 1) buf[i++] = '"';
                return i;
            default:
                return snprintf(buf, len, "<ref %d>", r);
            }
        }
        break;
    }
    return 0;
}

void pit_trace_(pit_runtime *rt, const char *format, pit_value v) {
    char buf[1024] = {0};
    pit_dump(rt, buf, sizeof(buf), v);
    fprintf(stderr, format, buf);
}

void pit_error(pit_runtime *rt, const char *format, ...) {
    if (pit_eq(rt->error, PIT_NIL)) { // only record the first error encountered
        char buf[1024] = {0};
        va_list vargs;
        va_start(vargs, format);
        vsnprintf(buf, sizeof(buf), format, vargs);
        va_end(vargs);
        rt->error = pit_bytes_new_cstr(rt, buf);
    }
}

void pit_check_error_maybe_panic(pit_runtime *rt) {
    if (!pit_eq(rt->error, PIT_NIL)) {
        char buf[1024] = {0};
        pit_dump(rt, buf, sizeof(buf), rt->error);
        pit_panic("runtime error: %s", buf);
    }
}

pit_value pit_value_new(pit_runtime *rt, enum pit_value_sort s, u64 data) {
    if (s == PIT_VALUE_SORT_DOUBLE) {
        if (((data >> 52) & 0b011111111111) == 0b011111111111 && ((data >> 51) & 0b1) == 0) {
            pit_error(rt, "attempted to create a signalling NaN double");
            return PIT_NIL;
        }
        return data;
    }
    return
        0b1111111111110000000000000000000000000000000000000000000000000000
        | (((u64) (s & 0b11)) << 49)
        | (data & 0b1111111111111111111111111111111111111111111111111);
}

double pit_as_double(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_DOUBLE) {
        pit_error(rt, "invalid use of value as double");
        return NAN;
    }
    return (double) v;
}
pit_value pit_double_new(pit_runtime *rt, double d) {
    return pit_value_new(rt, PIT_VALUE_SORT_DOUBLE, (u64) d);
}

i64 pit_as_integer(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_INTEGER) {
        pit_error(rt, "invalid use of value as integer");
        return -1;
    }
    u64 lo = pit_value_data(v);
    return ((i64) (lo << 15)) >> 15; // sign-extend low 49 bits
}
pit_value pit_integer_new(pit_runtime *rt, i64 i) {
    return pit_value_new(rt, PIT_VALUE_SORT_INTEGER, (u64) i);
}

pit_symbol pit_as_symbol(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_SYMBOL) {
        pit_error(rt, "invalid use of value as symbol");
        return -1;
    }
    return pit_value_data(v) & 0xffffffff;
}
pit_value pit_symbol_new(pit_runtime *rt, pit_symbol s) {
    return pit_value_new(rt, PIT_VALUE_SORT_SYMBOL, (u64) s);
}

pit_ref pit_as_ref(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "invalid use of value as ref");
        return -1;
    }
    return pit_value_data(v) & 0xffffffff;
}
pit_value pit_ref_new(pit_runtime *rt, pit_ref r) {
    return pit_value_new(rt, PIT_VALUE_SORT_REF, (u64) r);
}

pit_value pit_heavy_new(pit_runtime *rt) {
    i32 idx = pit_arena_alloc_idx(rt->values);
    return pit_ref_new(rt, idx);
}

pit_value_heavy *pit_deref(pit_runtime *rt, pit_ref p) {
    return pit_arena_idx(rt->values, p);
}

bool pit_is_cons(pit_runtime *rt, pit_value a) {
    switch (pit_value_sort(a)) {
    case PIT_VALUE_SORT_REF:
        pit_value_heavy *ha = pit_deref(rt, a);
        if (!ha) { pit_error(rt, "bad ref"); return false; }
        switch (ha->hsort) {
        case PIT_VALUE_HEAVY_SORT_CONS:
            return true;
        default:
            break;
        }
    default:
        break;
    }
    return false;
}
bool pit_eq(pit_value a, pit_value b) {
    return a == b;
}
bool pit_equal(pit_runtime *rt, pit_value a, pit_value b) {
    if (pit_value_sort(a) != pit_value_sort(b)) return false;
    switch (pit_value_sort(a)) {
    case PIT_VALUE_SORT_DOUBLE:
    case PIT_VALUE_SORT_INTEGER:
    case PIT_VALUE_SORT_SYMBOL:
        return pit_value_data(a) == pit_value_data(b);
    case PIT_VALUE_SORT_REF:
        pit_value_heavy *ha = pit_deref(rt, a);
        if (!ha) { pit_error(rt, "bad ref"); return false; }
        pit_value_heavy *hb = pit_deref(rt, b);
        if (!hb) { pit_error(rt, "bad ref"); return false; }
        if (ha->hsort != hb->hsort) return false;
        switch (ha->hsort) {
        case PIT_VALUE_HEAVY_SORT_CONS:
            return pit_equal(rt, ha->cons.car, hb->cons.car) && pit_equal(rt, ha->cons.cdr, hb->cons.cdr);
        case PIT_VALUE_HEAVY_SORT_ARRAY:
            if (ha->array.len != hb->array.len) return false;
            for (i64 i = 0; i < ha->array.len; ++i) {
                if (!pit_equal(rt, ha->array.data[i], hb->array.data[i])) return false;
            }
            return true;
        case PIT_VALUE_HEAVY_SORT_BYTES:
            if (ha->bytes.len != hb->bytes.len) return false;
            for (i64 i = 0; i < ha->bytes.len; ++i) {
                if (ha->bytes.data[i] != hb->bytes.data[i]) return false;
            }
            return true;
        case PIT_VALUE_HEAVY_SORT_NATIVEFUNC:
            return ha->nativefunc == hb->nativefunc;
        }
    }
    return false;
}

pit_value pit_bytes_new(pit_runtime *rt, u8 *buf, i64 len) {
    u8 *dest = pit_arena_alloc_bulk(rt->bytes, len);
    if (!dest) { pit_error(rt, "failed to allocate bytes"); return PIT_NIL; }
    memcpy(dest, buf, len);
    if (!dest) return PIT_NIL;
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, ret);
    if (!h) { pit_error(rt, "failed to create new heavy value for bytes"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_BYTES;
    h->bytes.data = dest;
    h->bytes.len = len;
    return ret;
}

pit_value pit_bytes_new_cstr(pit_runtime *rt, char *s) {
    return pit_bytes_new(rt, (u8 *) s, strlen(s));
}

// return true if v is a reference to bytes that are the same as those in buf
bool pit_bytes_match(pit_runtime *rt, pit_value v, u8 *buf, i64 len) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return false;
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return false; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) return false;
    if (h->bytes.len != len) return false;
    for (i64 i = 0; i < len; ++i)
        if (h->bytes.data[i] != buf[i]) {
            return false;
        }
    return true;
}

pit_value pit_intern(pit_runtime *rt, u8 *nm, i64 len) {
    for (i64 i = 0; i < rt->symtab_len; ++i) {
        pit_symbol idx = i * sizeof(pit_symtab_entry);
        pit_symtab_entry *ent = pit_arena_idx(rt->symtab, idx);
        if (!ent) { pit_error(rt, "corrupted symbol table"); return PIT_NIL; }
        if (pit_bytes_match(rt, ent->name, nm, len)) return pit_symbol_new(rt, idx);
    }
    i64 idx = pit_arena_alloc_idx(rt->symtab);
    pit_symtab_entry *ent = pit_arena_idx(rt->symtab, idx);
    if (!ent) { pit_error(rt, "failed to allocate symtab entry"); return PIT_NIL; }
    ent->name = pit_bytes_new(rt, nm, len);
    ent->value = PIT_NIL;
    ent->function = PIT_NIL;
    rt->symtab_len += 1;
    return pit_symbol_new(rt, idx);
}

pit_value pit_intern_cstr(pit_runtime *rt, char *nm) {
    return pit_intern(rt, (u8 *) nm, strlen(nm));
}

pit_symtab_entry *pit_symtab_lookup(pit_runtime *rt, pit_value sym) {
    pit_symbol s = pit_as_symbol(rt, sym);
    return pit_arena_idx(rt->symtab, s);
}
pit_value pit_get(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return ent->value;
}
void pit_set(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    ent->value = v;
}
pit_value pit_fget(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return ent->function;
}
void pit_fset(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    ent->function = v;
}

pit_value pit_cons(pit_runtime *rt, pit_value car, pit_value cdr) {
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, ret);
    if (!h) { pit_error(rt, "failed to create new heavy value for cons"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_CONS;
    h->cons.car = car;
    h->cons.cdr = cdr;
    return ret;
}

pit_value pit_list(pit_runtime *rt, i64 num, ...) {
    pit_value *temp = calloc(num, sizeof(pit_value));
    va_list elems;
    va_start(elems, num);
    for (i64 i = 0; i < num; ++i) {
        temp[i] = va_arg(elems, pit_value);
    }
    va_end(elems);
    pit_value ret = PIT_NIL;
    for (i64 i = 0; i < num; ++i) {
        ret = pit_cons(rt, temp[num - i - 1], ret);
    }
    return ret;
}

pit_value pit_car(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return PIT_NIL;
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) return PIT_NIL;
    return h->cons.car;
}
pit_value pit_cdr(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return PIT_NIL;
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) return PIT_NIL;
    return h->cons.cdr;
}

pit_value pit_nativefunc_new(pit_runtime *rt, pit_nativefunc f) {
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, ret);
    if (!h) { pit_error(rt, "failed to create new heavy value for nativefunc"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_NATIVEFUNC;
    h->nativefunc = f;
    return ret;
}

pit_value pit_apply(pit_runtime *rt, pit_value f, pit_value args) {
    switch (pit_value_sort(f)) {
    case PIT_VALUE_SORT_REF:
        pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, f));
        if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
        if (h->hsort != PIT_VALUE_HEAVY_SORT_NATIVEFUNC) {
            pit_error(rt, "attempt to apply non-nativefunc ref");
            return PIT_NIL;
        }
        return h->nativefunc(rt, args);
    default:
        pit_error(rt, "attempted to apply non-function value");
        return PIT_NIL;
    }
}
