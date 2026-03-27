#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lcq/pit/utils.h>
#include <lcq/pit/lexer.h>
#include <lcq/pit/parser.h>
#include <lcq/pit/runtime.h>
#include <lcq/pit/library.h>

enum pit_value_sort pit_value_sort(pit_value v) {
    /* if this isn't a NaN, or it's a quiet NaN, this is a real double */
    /* if (((v >> 52) & 0b011111111111) != 0b011111111111 || ((v >> 51) & 0b1) == 1) return PIT_VALUE_SORT_DOUBLE; */
    if (((v >> 52) & 0x7ff) != 0x7ff || ((v >> 51) & 1) == 1) return PIT_VALUE_SORT_DOUBLE;
    /* otherwise, we've packed something else in the significand
         0 for signaling NaN -+
           sign --+ +- 1 (NaN)| +- our sort tag       + our data
                  | |         | |                     |
                  s111111111110ttddddddddddddddddddddddddddddddddddddddddddddddddd */
    /* return (v & 0b0000000000000110000000000000000000000000000000000000000000000000) >> 49; */
    return (v & 0x6000000000000) >> 49; /* equivalent hex literal */
}

u64 pit_value_data(pit_value v) {
    /* return  v & 0b0000000000000001111111111111111111111111111111111111111111111111; */
    return v & 0x1ffffffffffff;
}

pit_runtime *pit_runtime_new() {
    pit_runtime *ret = malloc(sizeof(*ret));
    ret->heap = pit_arena_new(64 * 1024 * 1024, sizeof(pit_value_heavy));
    ret->backbuffer = pit_arena_new(64 * 1024 * 1024, sizeof(pit_value_heavy));
    ret->symtab = pit_arena_new(1024 * 1024, sizeof(pit_symtab_entry));
    ret->symtab_len = 0;
    ret->scratch = pit_arena_new(1024 * 1024, sizeof(u8));
    ret->expr_stack = pit_values_new(64 * 1024);
    ret->result_stack = pit_values_new(64 * 1024);
    ret->program = pit_runtime_eval_program_new(64 * 1024);
    ret->saved_bindings = pit_values_new(64 * 1024);
    ret->frozen_values = 0;
    ret->frozen_symtab = 0;
    ret->error = PIT_NIL;
    ret->source_line = ret->source_column = -1;
    ret->error_line = ret->error_column = -1;
    pit_value nil = pit_intern_cstr(ret, "nil"); /* nil must be the 0th symbol for PIT_NIL to work */
    pit_set(ret, nil, PIT_NIL);
    pit_value truth = pit_intern_cstr(ret, "t");
    pit_set(ret, truth, truth);
    pit_runtime_freeze(ret);
    return ret;
}

void pit_runtime_freeze(pit_runtime *rt) {
    rt->frozen_values = rt->heap->next;
    rt->frozen_symtab = rt->symtab->next;
}
void pit_runtime_reset(pit_runtime *rt) {
    rt->heap->next = rt->frozen_values;
    rt->symtab->next = rt->frozen_symtab;
}
bool pit_runtime_print_error(pit_runtime *rt) {
    if (!pit_eq(rt->error, PIT_NIL)) {
        char buf[1024] = {0};
        i64 end = pit_dump(rt, buf, sizeof(buf) - 1, rt->error, false);
        buf[end] = 0;
        fprintf(stderr, "error at line %ld, column %ld: %s\n", rt->error_line, rt->error_column, buf);
        return true;
    }
    return false;
}

#define CHECK_BUF if (buf >= end) { return buf - start; }
#define CHECK_BUF_LABEL(label) if (buf >= end) { goto label; }
i64 pit_dump(pit_runtime *rt, char *buf, i64 len, pit_value v, bool readable) {
    pit_value_heavy *h = NULL;
    if (len <= 0) return 0;
    switch (pit_value_sort(v)) {
    case PIT_VALUE_SORT_DOUBLE:
        return snprintf(buf, (size_t) len, "%lf", pit_as_double(rt, v));
    case PIT_VALUE_SORT_INTEGER:
        return snprintf(buf, (size_t) len, "%ld", pit_as_integer(rt, v));
    case PIT_VALUE_SORT_SYMBOL: {
        pit_symtab_entry *ent = pit_symtab_lookup(rt, v);
        if (ent
            && pit_value_sort(ent->name) == PIT_VALUE_SORT_REF
            && (h = pit_deref(rt, pit_as_ref(rt, ent->name)))
        ) {
            i64 i = 0;
            for (; i < h->in.bytes.len && i < len - 1; ++i) {
                buf[i] = (char) h->in.bytes.data[i];
            }
            return i;
        } else {
            return snprintf(buf, (size_t) len, "<broken symbol %ld>", pit_as_symbol(rt, v));
        }
    }
    case PIT_VALUE_SORT_REF: {
        pit_ref r = pit_as_ref(rt, v);
        char *end = buf + len;
        char *start = buf;
        h = pit_deref(rt, r);
        if (!h) snprintf(buf, (size_t) len, "<ref %ld>", r);
        else {
            switch (h->hsort) {
            case PIT_VALUE_HEAVY_SORT_CELL: {
                CHECK_BUF; *(buf++) = '{';
                CHECK_BUF; buf += pit_dump(rt, buf, end - buf, pit_car(rt, h->in.cell), readable);
                CHECK_BUF; *(buf++) = '}';
                return buf - start;
            }
            case PIT_VALUE_HEAVY_SORT_CONS: {
                pit_value cur = v;
                CHECK_BUF_LABEL(list_end);
                do {
                    if (pit_is_cons(rt, cur)) {
                        CHECK_BUF_LABEL(list_end); *(buf++) = ' ';
                        CHECK_BUF_LABEL(list_end); buf += pit_dump(rt, buf, end - buf, pit_car(rt, cur), readable);
                    } else {
                        CHECK_BUF_LABEL(list_end); buf += snprintf(buf, (size_t) (end - buf), " . ");
                        CHECK_BUF_LABEL(list_end); buf += pit_dump(rt, buf, end - buf, cur, readable);
                    }
                } while (!pit_eq((cur = pit_cdr(rt, cur)), PIT_NIL));
                CHECK_BUF_LABEL(list_end); *(buf++) = ')';
                list_end:
                *start = '(';
                return buf - start;
            }
            case PIT_VALUE_HEAVY_SORT_ARRAY: {
                i64 i = 0;
                CHECK_BUF_LABEL(array_end);
                if (h->in.array.len == 0) {
                    CHECK_BUF_LABEL(array_end); *(buf++) = '[';
                } else for (; i < h->in.array.len; ++i) {
                    CHECK_BUF_LABEL(array_end); *(buf++) = ' ';
                    CHECK_BUF_LABEL(array_end); buf += pit_dump(rt, buf, end - buf, h->in.array.data[i], readable);
                }
                CHECK_BUF_LABEL(array_end); *(buf++) = ']';
                array_end:
                *start = '[';
                return buf - start;
            }
            case PIT_VALUE_HEAVY_SORT_BYTES: {
                i64 i = 0;
                if (readable) { CHECK_BUF; buf[i++] = '"'; }
                i64 maxlen = len - i;
                for (i64 j = 0; i < maxlen && j < h->in.bytes.len;) {
                    if (buf[i - 1] != '\\' && (h->in.bytes.data[j] == '\\' || h->in.bytes.data[j] == '"')) {
                        CHECK_BUF; buf[i++] = '\\';
                    }
                    else {
                        CHECK_BUF; buf[i++] = (char) h->in.bytes.data[j++];
                    }
                }
                if (readable && i < len - 1) buf[i++] = '"';
                return i;
            }
            default:
                return snprintf(buf, (size_t) len, "<ref %ld>", r);
            }
        }
        break;
    }
    }
    return 0;
}

void pit_trace_(pit_runtime *rt, const char *format, pit_value v) {
    char buf[1024] = {0};
    i64 end = pit_dump(rt, buf, sizeof(buf) - 1, v, true);
    buf[end] = 0;
    fprintf(stderr, format, buf);
}

void pit_error(pit_runtime *rt, const char *format, ...) {
    if (rt->error == PIT_NIL) { /* only record the first error encountered */
        char buf[1024] = {0};
        va_list vargs;
        va_start(vargs, format);
        vsnprintf(buf, sizeof(buf), format, vargs);
        va_end(vargs);
        rt->error = PIT_T; /* we set the error now to prevent infinite recursion */
        rt->error = pit_bytes_new_cstr(rt, buf); /* in case this errs also */
        if (rt->error == PIT_NIL) rt->error = PIT_T;
        rt->error_line = rt->source_line;
        rt->error_column = rt->source_column;
    }
}

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

double pit_as_double(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_DOUBLE) {
        pit_error(rt, "invalid use of value as double");
        return 0.0;
    }
    union { double dval; u64 ival; } x;
    x.ival = v;
    return x.dval;
}
pit_value pit_double_new(pit_runtime *rt, double d) {
    union { double dval; u64 ival; } x;
    x.dval = d;
    return pit_value_new(rt, PIT_VALUE_SORT_DOUBLE, x.ival);
}

i64 pit_as_integer(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_INTEGER) {
        pit_error(rt, "invalid use of value as integer");
        return -1;
    }
    u64 lo = pit_value_data(v);
    return ((i64) (lo << 15)) >> 15; /* sign-extend low 49 bits */

}
pit_value pit_integer_new(pit_runtime *rt, i64 i) {
    return pit_value_new(rt, PIT_VALUE_SORT_INTEGER, 0x1ffffffffffff & (u64) i);
}
pit_value pit_bool_new(pit_runtime *rt, bool i) {
    (void) rt;
    return i ? PIT_T : PIT_NIL;
}

pit_symbol pit_as_symbol(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_SYMBOL) {
        pit_error(rt, "invalid use of value as symbol");
        return -1;
    }
    return (pit_symbol) (pit_value_data(v) & 0xffffffff);
}
pit_value pit_symbol_new(pit_runtime *rt, pit_symbol s) {
    return pit_value_new(rt, PIT_VALUE_SORT_SYMBOL, (u64) s);
}

pit_ref pit_as_ref(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "invalid use of value as ref");
        return -1;
    }
    return (pit_ref) (pit_value_data(v) & 0xffffffff);
}
pit_value pit_ref_new(pit_runtime *rt, pit_ref r) {
    return pit_value_new(rt, PIT_VALUE_SORT_REF, (u64) r);
}

pit_value pit_heavy_new(pit_runtime *rt) {
    i64 idx = pit_arena_alloc_idx(rt->heap);
    if (idx < 0) {
        pit_error(rt, "failed to allocate space for heavy value");
        return PIT_NIL;
    }
    return pit_ref_new(rt, idx);
}

pit_value_heavy *pit_deref(pit_runtime *rt, pit_ref p) {
    return pit_arena_get(rt->heap, p);
}

bool pit_is_integer(pit_runtime *rt, pit_value a) {
    (void) rt;
    return pit_value_sort(a) == PIT_VALUE_SORT_INTEGER;
}
bool pit_is_double(pit_runtime *rt, pit_value a) {
    (void) rt;
    return pit_value_sort(a) == PIT_VALUE_SORT_DOUBLE;
}
bool pit_is_symbol(pit_runtime *rt, pit_value a) {
    (void) rt;
    return pit_value_sort(a) == PIT_VALUE_SORT_SYMBOL;
}
bool pit_is_value_heavy_sort(pit_runtime *rt, pit_value a, enum pit_value_heavy_sort e) {
    switch (pit_value_sort(a)) {
    case PIT_VALUE_SORT_REF: {
        pit_value_heavy *ha = pit_deref(rt, pit_as_ref(rt, a));
        if (!ha) { pit_error(rt, "bad ref"); return false; }
        return ha->hsort == e;
    }
    default:
        break;
    }
    return false;
}
bool pit_is_cell(pit_runtime *rt, pit_value a) {
    return pit_is_value_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_CELL);
}
bool pit_is_cons(pit_runtime *rt, pit_value a) {
    return pit_is_value_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_CONS);
}
bool pit_is_array(pit_runtime *rt, pit_value a) {
    return pit_is_value_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_ARRAY);
}
bool pit_is_bytes(pit_runtime *rt, pit_value a) {
    return pit_is_value_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_BYTES);
}
bool pit_is_func(pit_runtime *rt, pit_value a) {
    return pit_is_value_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_FUNC);
}
bool pit_is_nativefunc(pit_runtime *rt, pit_value a) {
    return pit_is_value_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_NATIVEFUNC);
}
bool pit_is_nativedata(pit_runtime *rt, pit_value a) {
    return pit_is_value_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_NATIVEDATA);
}
bool pit_is_forwarding_pointer(pit_runtime *rt, pit_value a) {
    return pit_is_value_heavy_sort(rt, a, PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER);
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
    case PIT_VALUE_SORT_REF: {
        pit_value_heavy *ha = pit_deref(rt, pit_as_ref(rt, a));
        if (!ha) { pit_error(rt, "bad ref"); return false; }
        pit_value_heavy *hb = pit_deref(rt, pit_as_ref(rt, b));
        if (!hb) { pit_error(rt, "bad ref"); return false; }
        if (ha->hsort != hb->hsort) return false;
        switch (ha->hsort) {
        case PIT_VALUE_HEAVY_SORT_CELL:
            return pit_equal(rt, ha->in.cell, hb->in.cell);
        case PIT_VALUE_HEAVY_SORT_CONS:
            return pit_equal(rt, ha->in.cons.car, hb->in.cons.car)
                && pit_equal(rt, ha->in.cons.cdr, hb->in.cons.cdr);
        case PIT_VALUE_HEAVY_SORT_ARRAY: {
            if (ha->in.array.len != hb->in.array.len) return false;
            for (i64 i = 0; i < ha->in.array.len; ++i) {
                if (!pit_equal(rt, ha->in.array.data[i], hb->in.array.data[i])) return false;
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
                pit_equal(rt, ha->in.func.env, hb->in.func.env)
                && pit_equal(rt, ha->in.func.args, hb->in.func.args)
                && pit_equal(rt, ha->in.func.body, hb->in.func.body);
        case PIT_VALUE_HEAVY_SORT_NATIVEFUNC:
            return ha->in.nativefunc == hb->in.nativefunc;
        case PIT_VALUE_HEAVY_SORT_NATIVEDATA:
            return
                pit_eq(ha->in.nativedata.tag, hb->in.nativedata.tag)
                && ha->in.nativedata.data == hb->in.nativedata.data;
        case PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER:
            return ha->in.forwarding_pointer == hb->in.forwarding_pointer;
        }
    }
    }
    return false;
}
pit_value pit_bytes_new(pit_runtime *rt, u8 *buf, i64 len) {
    u8 *dest = pit_arena_alloc_back(rt->heap, len);
    if (!dest) { pit_error(rt, "failed to allocate bytes"); return PIT_NIL; }
    memcpy(dest, buf, (size_t) len);
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for bytes"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_BYTES;
    h->in.bytes.data = dest;
    h->in.bytes.len = len;
    return ret;
}
pit_value pit_bytes_new_cstr(pit_runtime *rt, char *s) {
    return pit_bytes_new(rt, (u8 *) s, (i64) strlen(s));
}
pit_value pit_bytes_new_file(pit_runtime *rt, char *path) {
    if (rt->error != PIT_NIL) return PIT_NIL;
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        pit_error(rt, "failed to open file: %s", path);
        return PIT_NIL;
    }
    fseek(f, 0, SEEK_END);
    i64 len = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *dest = pit_arena_alloc_bulk(rt->heap, len);
    if (!dest) { pit_error(rt, "failed to allocate bytes"); fclose(f); return PIT_NIL; }
    if ((size_t) len != fread(dest, sizeof(char), (size_t) len, f)) {
        fclose(f);
        pit_error(rt, "failed to read file: %s", path);
        return PIT_NIL;
    }
    fclose(f);
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for bytes"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_BYTES;
    h->in.bytes.data = dest;
    h->in.bytes.len = len;
    return ret;
}
/* return true if v is a reference to bytes that are the same as those in buf */
bool pit_bytes_match(pit_runtime *rt, pit_value v, u8 *buf, i64 len) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return false;
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return false; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) return false;
    if (h->in.bytes.len != len) return false;
    for (i64 i = 0; i < len; ++i)
        if (h->in.bytes.data[i] != buf[i]) {
            return false;
        }
    return true;
}
i64 pit_as_bytes(pit_runtime *rt, pit_value v, u8 *buf, i64 maxlen) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return -1; }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
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

pit_value pit_intern(pit_runtime *rt, u8 *nm, i64 len) {
    if (rt->error != PIT_NIL) return PIT_NIL;
    for (i64 sidx = 0; sidx < rt->symtab_len; ++sidx) {
        pit_symtab_entry *sent = pit_arena_get(rt->symtab, sidx);
        if (!sent) { pit_error(rt, "corrupted symbol table"); return PIT_NIL; }
        if (pit_bytes_match(rt, sent->name, nm, len)) return pit_symbol_new(rt, sidx);
    }
    i64 idx = pit_arena_alloc_idx(rt->symtab);
    pit_symtab_entry *ent = pit_arena_get(rt->symtab, idx);
    if (!ent) { pit_error(rt, "failed to allocate symtab entry"); return PIT_NIL; }
    ent->name = pit_bytes_new(rt, nm, len);
    ent->value = PIT_NIL;
    ent->function = PIT_NIL;
    ent->is_macro = false;
    ent->is_special_form = false;
    ent->is_keyword = len >= 1 && nm[0] == ':';
    rt->symtab_len += 1;
    return pit_symbol_new(rt, idx);
}
pit_value pit_intern_cstr(pit_runtime *rt, char *nm) {
    return pit_intern(rt, (u8 *) nm, (i64) strlen(nm));
}
pit_value pit_symbol_name(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return ent->name;
}
bool pit_symbol_name_match(pit_runtime *rt, pit_value sym, u8 *buf, i64 len) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return pit_bytes_match(rt, ent->name, buf, len);
}
bool pit_symbol_name_match_cstr(pit_runtime *rt, pit_value sym, char *s) {
    return pit_symbol_name_match(rt, sym, (u8 *) s, (i64) strlen(s));
}
pit_symtab_entry *pit_symtab_lookup(pit_runtime *rt, pit_value sym) {
    pit_symbol s = pit_as_symbol(rt, sym);
    return pit_arena_get(rt->symtab, s);
}
pit_value pit_get_value_cell(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return ent->value;
}
pit_value pit_get_function_cell(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return ent->function;
}
pit_value pit_get(pit_runtime *rt, pit_value sym) {
    return pit_cell_get(rt, pit_get_value_cell(rt, sym), sym);
}
void pit_set(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symbol idx = pit_as_symbol(rt, sym);
    if (idx < rt->frozen_symtab) { pit_error(rt, "attempted to modify frozen symbol"); return; }
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    if (pit_value_sort(ent->value) != PIT_VALUE_SORT_REF) {
        ent->value = pit_cell_new(rt, PIT_NIL);
    }
    pit_cell_set(rt, ent->value, v, sym);
}
pit_value pit_fget(pit_runtime *rt, pit_value sym) {
    return pit_cell_get(rt, pit_get_function_cell(rt, sym), sym);
}
void pit_fset(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symbol idx = pit_as_symbol(rt, sym);
    if (idx < rt->frozen_symtab) { pit_error(rt, "attempted to modify frozen symbol"); return; }
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    if (pit_value_sort(ent->function) != PIT_VALUE_SORT_REF) {
        ent->function = pit_cell_new(rt, PIT_NIL);
    }
    pit_cell_set(rt, ent->function, v, sym);
}
bool pit_is_symbol_macro(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return false; }
    return ent->is_macro;
}
void pit_symbol_is_macro(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    ent->is_macro = true;
}
void pit_mset(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_fset(rt, sym, v);
    pit_symbol_is_macro(rt, sym);
}
bool pit_is_symbol_special_form(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return false; }
    return ent->is_special_form;
}
void pit_symbol_is_special_form(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    ent->is_special_form = true;
}
void pit_sfset(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_fset(rt, sym, v);
    pit_symbol_is_special_form(rt, sym);
}
void pit_bind(pit_runtime *rt, pit_value sym, pit_value cell) {
    /* although we cannot set frozen symbols, we can still bind them temporarily - no need to check */
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    pit_values_push(rt, rt->saved_bindings, ent->value);
    ent->value = cell;
}
pit_value pit_unbind(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    pit_value old = ent->value;
    ent->value = pit_values_pop(rt, rt->saved_bindings);
    return old;
}

pit_value pit_cell_new(pit_runtime *rt, pit_value v) {
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for cell"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_CELL;
    h->in.cell = v;
    return ret;
}
pit_value pit_cell_get(pit_runtime *rt, pit_value cell, pit_value sym) {
    if (pit_value_sort(cell) != PIT_VALUE_SORT_REF) {
        char buf[256];
        i64 end = pit_dump(rt, buf, sizeof(buf) - 1, sym, false);
        buf[end] = 0;
        pit_error(rt, "attempted to get unbound variable/function: %s", buf);
        return PIT_NIL;
    }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, cell));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CELL) {
        pit_error(rt, "cell value ref does not point to cell");
        return PIT_NIL;
    }
    return h->in.cell;
}
void pit_cell_set(pit_runtime *rt, pit_value cell, pit_value v, pit_value sym) {
    if (pit_value_sort(cell) != PIT_VALUE_SORT_REF) {
        char buf[256];
        i64 end = pit_dump(rt, buf, sizeof(buf) - 1, sym, false);
        buf[end] = 0;
        pit_error(rt, "attempted to set unbound variable/function: %s", buf);
        return;
    }
    pit_ref idx = pit_as_ref(rt, cell);
    if (idx < rt->frozen_values) { pit_error(rt, "attempt to modify frozen cell"); return; }
    pit_value_heavy *h = pit_deref(rt, idx);
    if (!h) { pit_error(rt, "bad ref"); return; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CELL) {
        pit_error(rt, "cell value ref does not point to cell");
        return;
    }
    h->in.cell = v;
}

pit_value pit_array_new(pit_runtime *rt, i64 len) {
    if (len < 0) { pit_error(rt, "failed to create array of negative size"); return PIT_NIL; }
    i64 byte_len = 0; pit_mul(&byte_len, sizeof(pit_value), len);
    pit_value *dest = pit_arena_alloc_bulk(rt->heap, byte_len);
    if (!dest) { pit_error(rt, "failed to allocate array"); return PIT_NIL; }
    for (i64 i = 0; i < len; ++i) dest[i] = PIT_NIL;
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for array"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_ARRAY;
    h->in.array.data = dest;
    h->in.array.len = len;
    return ret;
}
pit_value pit_array_from_buf(pit_runtime *rt, pit_value *xs, i64 len) {
    pit_value ret = pit_array_new(rt, len);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to deref heavy value for array"); return PIT_NIL; }
    memcpy(h->in.array.data, xs, (size_t) len * (size_t) sizeof(pit_value));
    return ret;
}
i64 pit_array_len(pit_runtime *rt, pit_value arr) {
    if (pit_value_sort(arr) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return -1; }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, arr));
    if (!h) { pit_error(rt, "bad ref"); return -1; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_ARRAY) { pit_error(rt, "not an array"); return -1; }
    return h->in.array.len;
}
pit_value pit_array_get(pit_runtime *rt, pit_value arr, i64 idx) {
    if (pit_value_sort(arr) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return PIT_NIL; }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, arr));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_ARRAY) { pit_error(rt, "not an array"); return PIT_NIL; }
    if (idx < 0 || idx >= h->in.array.len) {
        pit_error(rt, "array index out of bounds: %d", idx);
        return PIT_NIL;
    }
    return h->in.array.data[idx];
}
pit_value pit_array_set(pit_runtime *rt, pit_value arr, i64 idx, pit_value v) {
    if (pit_value_sort(arr) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return PIT_NIL; }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, arr));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_ARRAY) { pit_error(rt, "not an array"); return PIT_NIL; }
    if (idx < 0 || idx >= h->in.array.len) {
        pit_error(rt, "array index out of bounds: %d", idx);
        return PIT_NIL;
    }
    h->in.array.data[idx] = v;
    return v;
}

pit_value pit_cons(pit_runtime *rt, pit_value car, pit_value cdr) {
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for cons"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_CONS;
    h->in.cons.car = car;
    h->in.cons.cdr = cdr;
    return ret;
}
pit_value pit_list(pit_runtime *rt, i64 num, ...) {
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
        ret = pit_cons(rt, temp[num - i - 1], ret);
    }
    return ret;
}
i64 pit_list_len(pit_runtime *rt, pit_value xs) {
    i64 ret = 0;
    while (xs != PIT_NIL) {
        ret += 1;
        xs = pit_cdr(rt, xs);
    }
    return ret;
}
pit_value pit_car(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return PIT_NIL;
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) return PIT_NIL;
    return h->in.cons.car;
}
pit_value pit_cdr(pit_runtime *rt, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return PIT_NIL;
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) return PIT_NIL;
    return h->in.cons.cdr;
}
void pit_setcar(pit_runtime *rt, pit_value v, pit_value x) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return; }
    pit_ref idx = pit_as_ref(rt, v);
    if (idx < rt->frozen_values) { pit_error(rt, "attempted to modify frozen cons"); return; }
    pit_value_heavy *h = pit_deref(rt, idx);
    if (!h) { pit_error(rt, "bad ref"); return; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) { pit_error(rt, "not a cons"); return; }
    h->in.cons.car = x;
}
void pit_setcdr(pit_runtime *rt, pit_value v, pit_value x) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return; }
    pit_ref idx = pit_as_ref(rt, v);
    if (idx < rt->frozen_values) { pit_error(rt, "attempted to modify frozen cons"); return; }
    pit_value_heavy *h = pit_deref(rt, idx);
    if (!h) { pit_error(rt, "bad ref"); return; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) { pit_error(rt, "not a cons"); return; }
    h->in.cons.cdr = x;
}
pit_value pit_append(pit_runtime *rt, pit_value xs, pit_value ys) {
    pit_value ret = ys;
    xs = pit_reverse(rt, xs);
    while (xs != PIT_NIL) {
        ret = pit_cons(rt, pit_car(rt, xs), ret);
        xs = pit_cdr(rt, xs);
    }
    return ret;
}
pit_value pit_reverse(pit_runtime *rt, pit_value xs) {
    pit_value ret = PIT_NIL;
    while (xs != PIT_NIL) {
        ret = pit_cons(rt, pit_car(rt, xs), ret);
        xs = pit_cdr(rt, xs);
    }
    return ret;
}
pit_value pit_contains_eq(pit_runtime *rt, pit_value needle, pit_value haystack) {
    while (haystack != PIT_NIL) {
        if (pit_eq(needle, pit_car(rt, haystack))) return PIT_T;
        haystack = pit_cdr(rt, haystack);
    }
    return PIT_NIL;
}
pit_value pit_contains_equal(pit_runtime *rt, pit_value needle, pit_value haystack) {
    while (haystack != PIT_NIL) {
        if (pit_equal(rt, needle, pit_car(rt, haystack))) return PIT_T;
        haystack = pit_cdr(rt, haystack);
    }
    return PIT_NIL;
}
pit_value pit_plist_get(pit_runtime *rt, pit_value k, pit_value vs) {
    while (vs != PIT_NIL) {
        if (pit_eq(k, pit_car(rt, vs))) {
            return pit_car(rt, pit_cdr(rt, vs));
        }
        vs = pit_cdr(rt, vs);
    }
    return PIT_NIL;
}

pit_value pit_free_vars(pit_runtime *rt, pit_value initial_bound, pit_value body) {
    i64 expr_stack_reset = rt->expr_stack->next;
    pit_value ret = PIT_NIL;
    pit_values_push(rt, rt->expr_stack, pit_cons(rt, initial_bound, body));
    while (rt->expr_stack->next > expr_stack_reset) {
        pit_value boundscur = pit_values_pop(rt, rt->expr_stack);
        pit_value bound = pit_car(rt, boundscur);
        pit_value cur = pit_cdr(rt, boundscur);
        if (pit_is_cons(rt, cur)) {
            pit_value fsym = pit_car(rt, cur);
            bool is_symbol = pit_is_symbol(rt, fsym);
            pit_value fargs = pit_cdr(rt, cur);
            if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "lambda")) {
                pit_value new_bound = pit_append(rt, pit_car(rt, fargs), bound);
                fargs = pit_cdr(rt, fargs);
                while (fargs != PIT_NIL) {
                    pit_values_push(rt, rt->expr_stack, pit_cons(rt, new_bound, pit_car(rt, fargs)));
                    fargs = pit_cdr(rt, fargs);
                }
            } else if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "quote")) {
                /* don't look inside quote!
                   if we add other special forms, make sure to consider them here if necessary! */
            } else {
                while (fargs != PIT_NIL) {
                    pit_values_push(rt, rt->expr_stack, pit_cons(rt, bound, pit_car(rt, fargs)));
                    fargs = pit_cdr(rt, fargs);
                }
                if (!is_symbol) {
                    pit_values_push(rt, rt->expr_stack, pit_cons(rt, bound, fsym));
                }
            }
        } else if (pit_is_symbol(rt, cur)) {
            if (pit_contains_eq(rt, cur, bound) == PIT_NIL) {
                ret = pit_cons(rt, cur, ret);
            }
        }
    }
    rt->expr_stack->next = expr_stack_reset;
    return ret;
}
pit_value pit_lambda(pit_runtime *rt, pit_value args, pit_value body) {
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for lambda"); return PIT_NIL; }
    pit_value expanded = pit_expand_macros(rt, pit_cons(rt, pit_intern_cstr(rt, "progn"), body));
    pit_value freevars = pit_free_vars(rt, args, expanded);
    pit_value env = PIT_NIL;
    while (freevars != PIT_NIL) {
        pit_value sym = pit_car(rt, freevars);
        pit_value cell = pit_get_value_cell(rt, sym);
        env = pit_cons(rt, pit_cons(rt, sym, cell), env);
        freevars = pit_cdr(rt, freevars);
    }
    h->hsort = PIT_VALUE_HEAVY_SORT_FUNC;
    pit_value arg_cells = PIT_NIL;
    pit_value arg_rest_nm = PIT_NIL;
    pit_value separator = pit_intern_cstr(rt, "&");
    while (args != PIT_NIL) {
        pit_value nm = pit_car(rt, args);
        if (pit_eq(nm, separator)) {
            pit_value next_nm = pit_car(rt, pit_cdr(rt, args));
            if (next_nm == PIT_NIL) { pit_error(rt, "invalid & in lambda list"); return PIT_NIL; }
            arg_rest_nm = next_nm;
            arg_cells = pit_cons(rt, next_nm, arg_cells);
            break;
        } else {
            arg_cells = pit_cons(rt, nm, arg_cells);
            args = pit_cdr(rt, args);
        }
    }
    arg_cells = pit_reverse(rt, arg_cells);
    h->in.func.args = arg_cells;
    h->in.func.arg_rest_nm = arg_rest_nm;
    h->in.func.env = env;
    h->in.func.body = expanded;
    return ret;
}
pit_value pit_nativefunc_new(pit_runtime *rt, pit_nativefunc f) {
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for nativefunc"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_NATIVEFUNC;
    h->in.nativefunc = f;
    return ret;
}
pit_value pit_apply(pit_runtime *rt, pit_value f, pit_value args) {
    char buf[256] = {0};
    if (pit_is_symbol(rt, f)) {
        f = pit_fget(rt, f);
    }
    /* if f is not a symbol, assume it is a func or nativefunc
       most commonly, this happens when you funcall a variable
       with a function in the value cell, e.g. passing a lambda to a function */
    switch (pit_value_sort(f)) {
    case PIT_VALUE_SORT_REF: {
        pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, f));
        if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
        if (h->hsort == PIT_VALUE_HEAVY_SORT_FUNC) {
            /* calling a Lisp function is simple! */
            pit_value bound = PIT_NIL;
            pit_value env = h->in.func.env;
            while (env != PIT_NIL) { /* first, bind all entries in the closure */
                pit_value b = pit_car(rt, env);
                pit_value nm = pit_car(rt, b);
                pit_bind(rt, nm, pit_cdr(rt, b));
                bound = pit_cons(rt, nm, bound);
                env = pit_cdr(rt, env);
            }
            pit_value anames = h->in.func.args;
            while (anames != PIT_NIL) { /* bind all argument names to their values */
                pit_value nm = pit_car(rt, anames);
                pit_value cell = pit_cell_new(rt, PIT_NIL);
                if (h->in.func.arg_rest_nm != PIT_NIL && pit_eq(nm, h->in.func.arg_rest_nm)) {
                    pit_cell_set(rt, cell, args, nm);
                    pit_bind(rt, nm, cell);
                    break;
                } else {
                    pit_cell_set(rt, cell, pit_car(rt, args), nm);
                    pit_bind(rt, nm, cell);
                }
                bound = pit_cons(rt, nm, bound);
                args = pit_cdr(rt, args);
                anames = pit_cdr(rt, anames);
            }
            pit_value ret = pit_eval(rt, h->in.func.body); /* evaluate the body */
            while (bound != PIT_NIL) { /* unbind everything we bound earlier, in reverse */
                pit_unbind(rt, pit_car(rt, bound));
                bound = pit_cdr(rt, bound);
            }
            return ret;
        } else if (h->hsort == PIT_VALUE_HEAVY_SORT_NATIVEFUNC) {
            /* calling native functions is even simpler */
            return h->in.nativefunc(rt, args);
        } else {
            i64 end = pit_dump(rt, buf, sizeof(buf) - 1, f, true);
            buf[end] = 0;
            pit_error(rt, "attempted to apply non-function ref: %s", buf);
            return PIT_NIL;
        }
    }
    default: {
        i64 end = pit_dump(rt, buf, sizeof(buf) - 1, f, true);
        buf[end] = 0;
        pit_error(rt, "attempted to apply non-function value: %s", buf);
        return PIT_NIL;
    }
    }
}

pit_value pit_nativedata_new(pit_runtime *rt, pit_value tag, void *d) {
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, ret));
    if (!h) { pit_error(rt, "failed to create new heavy value for nativedata"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_NATIVEDATA;
    h->in.nativedata.tag = tag;
    h->in.nativedata.data = d;
    return ret;
}
void *pit_nativedata_get(pit_runtime *rt, pit_value tag, pit_value v) {
    pit_value_heavy *h = NULL;
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "value was not a reference");
        return NULL;
    }
    h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return NULL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_NATIVEDATA) {
        pit_error(rt, "invalid use of value as nativedata");
        return NULL;
    }
    if (!pit_eq(h->in.nativedata.tag, tag)) {
        pit_error(rt, "native value does not match tag");
        return NULL;
    }
    if (!h->in.nativedata.data) {
        pit_error(rt, "nativedata was already freed");
        return NULL;
    }
    return h->in.nativedata.data;
}

pit_values *pit_values_new(i64 capacity) {
    i64 cap = capacity / (i64) sizeof(pit_value);
    pit_values *ret = malloc(sizeof(*ret) + (size_t) cap * sizeof(pit_value));
    ret->next = 0;
    ret->capacity = cap;
    return ret;
}
void pit_values_push(pit_runtime *rt, pit_values *s, pit_value x) {
    (void) rt;
    s->data[s->next++] = x;
    if (s->next >= s->capacity) { pit_error(rt, "evaluation stack overflow"); }
}
pit_value pit_values_pop(pit_runtime *rt, pit_values *s) {
    if (s->next == 0) { pit_error(rt, "evaluation stack underflow"); return PIT_NIL; }
    return s->data[--s->next];
}

pit_runtime_eval_program *pit_runtime_eval_program_new(i64 capacity) {
    i64 cap = capacity / (i64) sizeof(pit_runtime_eval_program_entry);
    pit_runtime_eval_program *ret = malloc(sizeof(*ret) + (size_t) cap * sizeof(pit_runtime_eval_program_entry));
    ret->next = 0;
    ret->capacity = cap;
    return ret;
}
void pit_runtime_eval_program_push_literal(pit_runtime *rt, pit_runtime_eval_program *s, pit_value x) {
    pit_runtime_eval_program_entry *ent = &s->data[s->next++];
    ent->sort = EVAL_PROGRAM_ENTRY_LITERAL;
    ent->in.literal = x;
    if (s->next >= s->capacity) { pit_error(rt, "evaluation program overflow"); }
    (void) rt;
}
void pit_runtime_eval_program_push_apply(pit_runtime *rt, pit_runtime_eval_program *s, i64 arity) {
    pit_runtime_eval_program_entry *ent = &s->data[s->next++];
    ent->sort = EVAL_PROGRAM_ENTRY_APPLY;
    ent->in.apply = arity;
    if (s->next >= s->capacity) { pit_error(rt, "evaluation program overflow"); }
    (void) rt;
}

pit_value pit_expand_macros(pit_runtime *rt, pit_value top) {
    i64 expr_stack_reset = rt->expr_stack->next;
    i64 result_stack_reset = rt->result_stack->next;
    i64 program_reset = rt->program->next;
    pit_values_push(rt, rt->expr_stack, top);
    while (rt->expr_stack->next > expr_stack_reset) {
        pit_value cur;
        if (rt->error != PIT_NIL) goto end;
        cur = pit_values_pop(rt, rt->expr_stack);
        if (pit_is_cons(rt, cur)) {
            pit_value fsym = pit_car(rt, cur);
            bool is_symbol = pit_is_symbol(rt, fsym);
            if (is_symbol && pit_is_symbol_macro(rt, fsym)) {
                pit_value f = pit_fget(rt, fsym);
                pit_value args = pit_cdr(rt, cur);
                pit_value res = pit_apply(rt, f, args);
                pit_values_push(rt, rt->expr_stack, res);
            } else if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "defer")) {
                pit_value args = pit_cdr(rt, cur);
                pit_runtime_eval_program_push_literal(rt, rt->program, pit_car(rt, args));
            } else if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "quote")) {
                pit_runtime_eval_program_push_literal(rt, rt->program, cur);
            } else if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "lambda")) {
                pit_value args = pit_cdr(rt, cur);
                pit_value bindings = pit_car(rt, args);
                pit_value body = pit_cdr(rt, args);
                i64 argcount = 0;
                pit_values_push(rt, rt->expr_stack, pit_list(rt, 2, pit_intern_cstr(rt, "defer"), bindings));
                while (body != PIT_NIL) {
                    pit_value a = pit_car(rt, body);
                    pit_values_push(rt, rt->expr_stack, a);
                    body = pit_cdr(rt, body);
                    argcount += 1;
                }
                pit_runtime_eval_program_push_apply(rt, rt->program, argcount + 1);
                pit_runtime_eval_program_push_literal(rt, rt->program, fsym);
            } else {
                pit_value args = pit_cdr(rt, cur);
                i64 argcount = 0;
                while (args != PIT_NIL) {
                    pit_value a = pit_car(rt, args);
                    pit_values_push(rt, rt->expr_stack, a);
                    args = pit_cdr(rt, args);
                    argcount += 1;
                }
                if (!is_symbol) {
                    pit_values_push(rt, rt->expr_stack, fsym);
                }
                pit_runtime_eval_program_push_apply(rt, rt->program, argcount);
                if (is_symbol) {
                    pit_runtime_eval_program_push_literal(rt, rt->program, fsym);
                }
            }
        } else {
            pit_runtime_eval_program_push_literal(rt, rt->program, cur);
        }
    }
    for (i64 idx = rt->program->next - 1; idx >= program_reset; --idx) {
        pit_runtime_eval_program_entry *ent;
        if (rt->error != PIT_NIL) goto end;
        ent = &rt->program->data[idx];
        switch (ent->sort) {
        case EVAL_PROGRAM_ENTRY_LITERAL:
            pit_values_push(rt, rt->result_stack, ent->in.literal);
            break;
        case EVAL_PROGRAM_ENTRY_APPLY: {
            pit_value f = pit_values_pop(rt, rt->result_stack);
            pit_value args = PIT_NIL;
            for (i64 i = 0; i < ent->in.apply; ++i) {
                args = pit_cons(rt, pit_values_pop(rt, rt->result_stack), args);
            }
            pit_values_push(rt, rt->result_stack, pit_cons(rt, f, args));
            break;
        }
        default:
            pit_error(rt, "unknown program entry");
            goto end;
        }
    }
end: {
        pit_value ret = pit_values_pop(rt, rt->result_stack);
        rt->expr_stack->next = expr_stack_reset;
        rt->result_stack->next = result_stack_reset;
        rt->program->next = program_reset;
        return ret;
    }
}

pit_value pit_eval(pit_runtime *rt, pit_value top) {
    i64 expr_stack_reset = rt->expr_stack->next;
    i64 result_stack_reset = rt->result_stack->next;
    i64 program_reset = rt->program->next;
    pit_values_push(rt, rt->expr_stack, top);
    /* first, convert the expression tree into "polish notation" in program */
    while (rt->expr_stack->next > expr_stack_reset) {
        pit_value cur;
        if (rt->error != PIT_NIL) goto end;
        cur = pit_values_pop(rt, rt->expr_stack);
        if (pit_is_cons(rt, cur)) { /* compound expressions: function/macro application special forms */
            pit_value fsym = pit_car(rt, cur);
            bool is_symbol = pit_is_symbol(rt, fsym);
            if (is_symbol && pit_is_symbol_special_form(rt, fsym)) { /* special forms */
                pit_value f = pit_fget(rt, fsym);
                pit_value args = pit_cdr(rt, cur);
                /* special forms are nativefuncs that directly manipulate the stacks
                   basically macros, but we don't need to evaluate the return value */
                pit_apply(rt, f, args);
            } else if (is_symbol && pit_is_symbol_macro(rt, fsym)) { /* macros */
                pit_value f = pit_fget(rt, fsym);
                pit_value args = pit_cdr(rt, cur);
                pit_value res = pit_apply(rt, f, args);
                pit_values_push(rt, rt->expr_stack, res);
            } else { /* normal functions */
                pit_value args = pit_cdr(rt, cur);
                i64 argcount = 0;
                while (args != PIT_NIL) {
                    pit_values_push(rt, rt->expr_stack, pit_car(rt, args));
                    args = pit_cdr(rt, args);
                    argcount += 1;
                }
                if (!is_symbol) {
                    pit_values_push(rt, rt->expr_stack, fsym);
                }
                pit_runtime_eval_program_push_apply(rt, rt->program, argcount);
                if (is_symbol) {
                    pit_value f = pit_fget(rt, fsym);
                    pit_runtime_eval_program_push_literal(rt, rt->program, f);
                }
            }
        } else if (pit_is_symbol(rt, cur)) { /* unquoted symbols: variable lookup */
            pit_symtab_entry *ent = pit_symtab_lookup(rt, cur);
            if (ent->is_keyword) {
                pit_runtime_eval_program_push_literal(rt, rt->program, cur);
            } else {
                pit_runtime_eval_program_push_literal(rt, rt->program, pit_get(rt, cur));
            }
        } else { /* other expressions evaluate to themselves! */
            pit_runtime_eval_program_push_literal(rt, rt->program, cur);
        }
    }
    /* then, execute the polish notation program from right to left
       this has the nice consequence of putting the arguments in the right order */
    for (i64 idx = rt->program->next - 1; idx >= program_reset; --idx) {
        pit_runtime_eval_program_entry *ent;
        if (rt->error != PIT_NIL) goto end;
        ent = &rt->program->data[idx];
        switch (ent->sort) {
        case EVAL_PROGRAM_ENTRY_LITERAL:
            pit_values_push(rt, rt->result_stack, ent->in.literal);
            break;
        case EVAL_PROGRAM_ENTRY_APPLY: {
            pit_value f = pit_values_pop(rt, rt->result_stack);
            pit_value args = PIT_NIL;
            for (i64 i = 0; i < ent->in.apply; ++i) {
                args = pit_cons(rt, pit_values_pop(rt, rt->result_stack), args);
            }
            pit_values_push(rt, rt->result_stack, pit_apply(rt, f, args));
            break;
        }
        default:
            pit_error(rt, "unknown program entry");
            goto end;
        }
    }
end: {
        pit_value ret = pit_values_pop(rt, rt->result_stack);
        rt->expr_stack->next = expr_stack_reset;
        rt->result_stack->next = result_stack_reset;
        rt->program->next = program_reset;
        return ret;
    }
}

static i64 gc_copy(pit_arena *tospace, pit_value_heavy *h) {
    if (h->hsort == PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER) {
        return h->in.forwarding_pointer;
    } else {
        i64 ret = tospace->next;
        pit_value_heavy *g = pit_arena_alloc(tospace);
        *g = *h;
        h->hsort = PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER;
        h->in.forwarding_pointer = ret;
        return ret;
    }
}
static pit_value gc_copy_value(pit_runtime *rt, pit_arena *tospace, pit_value v) {
    if (pit_value_sort(v) == PIT_VALUE_SORT_REF) {
        pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
        i64 new = gc_copy(tospace, h);
        return pit_ref_new(rt, new);
    } else {
        return v;
    }
}
void pit_collect_garbage(pit_runtime *rt) {
    rt->frozen_values = 0;
    rt->frozen_symtab = 0;
    pit_arena *fromspace = rt->heap;
    pit_arena *tospace = rt->backbuffer;
    pit_arena_reset(tospace);
    /* populate tospace with immediately reachable values */
    for (i64 i = 0; i < rt->symtab_len; ++i) {
        pit_symtab_entry *ent = pit_arena_get(rt->symtab, i);
        ent->name = gc_copy_value(rt, tospace, ent->name);
        ent->value = gc_copy_value(rt, tospace, ent->value);
        ent->function = gc_copy_value(rt, tospace, ent->function);
    }
    for (i64 i = 0; i < rt->saved_bindings->next; ++i) {
        pit_value *v = &rt->saved_bindings->data[i];
        *v = gc_copy_value(rt, tospace, *v);
    }
    for (i64 scan = 0; scan < tospace->next; ++scan) {
        pit_value_heavy *h = pit_arena_get(tospace, scan);
        switch (h->hsort) {
        case PIT_VALUE_HEAVY_SORT_CELL:
            h->in.cell = gc_copy_value(rt, tospace, h->in.cell);
            break;
        case PIT_VALUE_HEAVY_SORT_CONS:
            h->in.cons.car = gc_copy_value(rt, tospace, h->in.cons.car);
            h->in.cons.cdr = gc_copy_value(rt, tospace, h->in.cons.cdr);
            break;
        case PIT_VALUE_HEAVY_SORT_ARRAY: {
            i64 byte_len = 0; pit_mul(&byte_len, sizeof(pit_value), h->in.array.len);
            pit_value *data = pit_arena_alloc_back(tospace, byte_len);
            for (i64 i = 0; i < h->in.array.len; ++i) {
                data[i] = gc_copy_value(rt, tospace, h->in.array.data[i]);
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
            h->in.func.env = gc_copy_value(rt, tospace, h->in.func.env);
            h->in.func.args = gc_copy_value(rt, tospace, h->in.func.args);
            h->in.func.arg_rest_nm = gc_copy_value(rt, tospace, h->in.func.arg_rest_nm);
            h->in.func.body = gc_copy_value(rt, tospace, h->in.func.body);
            break;
        case PIT_VALUE_HEAVY_SORT_NATIVEFUNC: break;
        case PIT_VALUE_HEAVY_SORT_NATIVEDATA:
            h->in.nativedata.tag = gc_copy_value(rt, tospace, h->in.nativedata.tag);
            break;
        case PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER:
            pit_error(rt, "garbage collection broken! encountered forwarding pointer in to-space");
            break;
        }
    }
    rt->heap = tospace;
    rt->backbuffer = fromspace;
}

static void check_invariants(pit_runtime *rt) {
    if (rt->scratch->next != 0) {
        pit_error(rt, "leaked scratch memory! %ld", rt->scratch->next);
    }
    if (rt->scratch->next != 0) {
        pit_error(rt, "leaked scratch memory! %ld", rt->scratch->next);
    }
}

pit_value pit_load_file(pit_runtime *rt, char *path) {
    pit_lexer lex;
    pit_parser parse;
    bool eof = false;
    pit_value p = PIT_NIL;
    pit_value ret = PIT_NIL;
    if (pit_lex_file(&lex, path) < 0) {
        pit_error(rt, "failed to lex file: %s", path);
        return PIT_NIL;
    }
    pit_parser_from_lexer(&parse, &lex);
    while (p = pit_parse(rt, &parse, &eof), !eof) {
        check_invariants(rt); if (pit_runtime_print_error(rt)) return PIT_NIL;
        ret = pit_eval(rt, p);
        check_invariants(rt); if (pit_runtime_print_error(rt)) return PIT_NIL;
        pit_collect_garbage(rt);
        check_invariants(rt); if (pit_runtime_print_error(rt)) return PIT_NIL;
    }
    check_invariants(rt); if (pit_runtime_print_error(rt)) return PIT_NIL;
    fprintf(stderr, "value allocs at exit: %ld\n", rt->heap->next);
    return ret;
}

void pit_repl(pit_runtime *rt) {
    size_t bufcap = 8;
    char *buf = malloc(bufcap);
    i64 len = 0;
    pit_runtime_freeze(rt);
    check_invariants(rt); if (pit_runtime_print_error(rt)) exit(1);
    setbuf(stdout, NULL);
    printf("> ");
    while ((buf[len++] = (char) getchar()) != EOF) {
        if (len >= (i64) bufcap) {
            bufcap *= 2;
            buf = realloc(buf, bufcap);
        }
        pit_value res;
        pit_lexer lex;
        pit_parser parse;
        bool eof = false;
        pit_value p = PIT_NIL;
        i64 depth = 0;
        bool lex_error = false;
        pit_lex_token tok = PIT_LEX_TOKEN_EOF;
        if (buf[len - 1] != '\n') continue;
        pit_lex_bytes(&lex, buf, len);
        while (!lex_error && (tok = pit_lex_next(&lex)) != PIT_LEX_TOKEN_EOF) {
            switch (tok) {
            case PIT_LEX_TOKEN_ERROR: lex_error = true; break;
            case PIT_LEX_TOKEN_LPAREN: depth += 1; break;
            case PIT_LEX_TOKEN_RPAREN: depth -= 1; break;
            default: break;
            }
        }
        if (lex_error || depth > 0) continue;
        buf[len - 1] = 0;
        pit_lex_bytes(&lex, buf, len);
        pit_parser_from_lexer(&parse, &lex);
        while (p = pit_parse(rt, &parse, &eof), !eof) {
            check_invariants(rt);
            res = pit_eval(rt, p);
            check_invariants(rt);
        }
        if (pit_runtime_print_error(rt)) {
            rt->error = PIT_NIL;
            printf("> ");
        } else {
            char dumpbuf[1024] = {0};
            pit_dump(rt, dumpbuf, sizeof(dumpbuf) - 1, res, true);
            pit_collect_garbage(rt);
            printf("%s\n> ", dumpbuf);
        }
        len = 0;
    }
    if (len >= (i64) sizeof(buf)) {
        fprintf(stderr, "expression exceeded REPL buffer size\n");
    } else {
        printf("bye!\n");
    }
    free(buf);
}
