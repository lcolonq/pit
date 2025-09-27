#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "utils.h"
#include "lexer.h"
#include "parser.h"
#include "runtime.h"
#include "library.h"

pit_arena *pit_arena_new(i64 capacity, i64 elem_size) {
    pit_arena *a = malloc(sizeof(pit_arena) + capacity * elem_size);
    a->elem_size = elem_size;
    a->capacity = capacity;
    a->next = 0;
    return a;
}
i32 pit_arena_next_idx(pit_arena *a) {
    i32 byte_idx; pit_mul(&byte_idx, a->elem_size, a->next);
    return byte_idx;
}
i32 pit_arena_alloc_idx(pit_arena *a) {
    i32 byte_idx = pit_arena_next_idx(a);
    if (byte_idx >= a->capacity) { return -1; }
    a->next += 1;
    return byte_idx;
}
i32 pit_arena_alloc_bulk_idx(pit_arena *a, i64 num) {
    i32 byte_idx = pit_arena_next_idx(a);
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
    ret->scratch = pit_arena_new(64 * 1024, sizeof(u8));
    ret->expr_stack = pit_values_new(1024);
    ret->result_stack = pit_values_new(1024);
    ret->program = pit_runtime_eval_program_new(64 * 1024);
    ret->saved_bindings = pit_values_new(1024);
    ret->frozen_values = 0;
    ret->frozen_bytes = 0;
    ret->frozen_symtab = 0;
    ret->error = PIT_NIL;
    ret->source_line = ret->source_column = -1;
    ret->error_line = ret->error_column = -1;
    pit_value nil = pit_intern_cstr(ret, "nil"); // nil must be the 0th symbol for PIT_NIL to work
    pit_set(ret, nil, PIT_NIL);
    pit_value truth = pit_intern_cstr(ret, "t");
    pit_set(ret, truth, truth);
    pit_install_library_essential(ret);
    pit_runtime_freeze(ret);
    return ret;
}

void pit_runtime_freeze(pit_runtime *rt) {
    rt->frozen_values = pit_arena_next_idx(rt->values);
    rt->frozen_bytes = pit_arena_next_idx(rt->bytes);
    rt->frozen_symtab = pit_arena_next_idx(rt->symtab);
}
void pit_runtime_reset(pit_runtime *rt) {
    rt->values->next = rt->frozen_values;
    rt->bytes->next = rt->frozen_bytes;
    rt->symtab->next = rt->frozen_symtab;
}
bool pit_runtime_print_error(pit_runtime *rt) {
    if (!pit_eq(rt->error, PIT_NIL)) {
        char buf[1024] = {0};
        pit_dump(rt, buf, sizeof(buf), rt->error, false);
        fprintf(stderr, "error at line %ld, column %ld: %s\n", rt->error_line, rt->error_column, buf);
        return true;
    }
    return false;
}

i64 pit_dump(pit_runtime *rt, char *buf, i64 len, pit_value v, bool readable) {
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
            case PIT_VALUE_HEAVY_SORT_CELL: {
                char *end = buf + len;
                char *start = buf;
                *(buf++) = '{';
                buf += pit_dump(rt, buf, end - buf, pit_car(rt, h->cell), readable);
                *(buf++) = '}';
                return buf - start;
            }
            case PIT_VALUE_HEAVY_SORT_CONS: {
                char *end = buf + len;
                char *start = buf;
                pit_value cur = v;
                do {
                    if (pit_is_cons(rt, cur)) {
                        *(buf++) = ' '; if (buf >= end) return end - buf;
                        buf += pit_dump(rt, buf, end - buf, pit_car(rt, cur), readable);
                        if (buf >= end) return end - buf;
                    } else {
                        buf += snprintf(buf, end - buf, " . ");
                        if (buf >= end) return end - buf;
                        buf += pit_dump(rt, buf, end - buf, cur, readable);
                        if (buf >= end) return end - buf;
                    }
                } while (!pit_eq((cur = pit_cdr(rt, cur)), PIT_NIL));
                *start = '(';
                *(buf++) = ')';
                return buf - start;
            }
            case PIT_VALUE_HEAVY_SORT_BYTES:
                i64 i = 0;
                if (readable) buf[i++] = '"';
                i64 maxlen = len - i;
                for (i64 j = 0; i < maxlen && j < h->bytes.len;) {
                    if (buf[i - 1] != '\\' && (h->bytes.data[j] == '\\' || h->bytes.data[j] == '"'))
                        buf[i++] = '\\';
                    else buf[i++] = h->bytes.data[j++];
                }
                if (readable && i < len - 1) buf[i++] = '"';
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
    pit_dump(rt, buf, sizeof(buf), v, true);
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
        rt->error_line = rt->source_line;
        rt->error_column = rt->source_column;
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
    case PIT_VALUE_SORT_REF:
        pit_value_heavy *ha = pit_deref(rt, a);
        if (!ha) { pit_error(rt, "bad ref"); return false; }
        return ha->hsort == e;
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
        case PIT_VALUE_HEAVY_SORT_CELL:
            return pit_equal(rt, ha->cell, hb->cell);
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
        case PIT_VALUE_HEAVY_SORT_FUNC:
            return
                pit_equal(rt, ha->func.env, hb->func.env)
                && pit_equal(rt, ha->func.args, hb->func.args)
                && pit_equal(rt, ha->func.body, hb->func.body);
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
pit_value pit_bytes_new_file(pit_runtime *rt, char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        pit_error(rt, "failed to open file: %s", path);
        return PIT_NIL;
    }
    fseek(f, 0, SEEK_END);
    i64 len = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *dest = pit_arena_alloc_bulk(rt->bytes, len);
    if (!dest) { pit_error(rt, "failed to allocate bytes"); fclose(f); return PIT_NIL; }
    fread(dest, sizeof(char), len, f);
    fclose(f);
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, ret);
    if (!h) { pit_error(rt, "failed to create new heavy value for bytes"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_BYTES;
    h->bytes.data = dest;
    h->bytes.len = len;
    return ret;
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
i64 pit_as_bytes(pit_runtime *rt, pit_value v, u8 *buf, i64 maxlen) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return -1;
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return -1; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) {
        pit_error(rt, "invalid use of value as bytes");
        return -1;
    }
    i64 len = maxlen < h->bytes.len ? maxlen : h->bytes.len;
    for (i64 i = 0; i < len; ++i) {
        buf[i] = h->bytes.data[i];
    }
    return len;
}
bool pit_lexer_from_bytes(pit_runtime *rt, pit_lexer *ret, pit_value v) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) return false;
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, v));
    if (!h) { pit_error(rt, "bad ref"); return false; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_BYTES) {
        pit_error(rt, "invalid use of value as bytes");
        return -1;
    }
    pit_lex_bytes(ret, (char *) h->bytes.data, h->bytes.len);
    return true;
}
pit_value pit_read_bytes(pit_runtime *rt, pit_value v) { // read a single lisp form from a bytestring
    pit_lexer lex;
    if (!pit_lexer_from_bytes(rt, &lex, v)) {
        pit_error(rt, "failed to initialize lexer");
        return PIT_NIL;
    }
    pit_parser parse;
    pit_parser_from_lexer(&parse, &lex);
    pit_value program = pit_parse(rt, &parse, NULL);
    return pit_eval(rt, program);
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
    ent->is_macro = false;
    ent->is_special_form = false;
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
bool pit_symbol_name_match(pit_runtime *rt, pit_value sym, u8 *buf, i64 len) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return pit_bytes_match(rt, ent->name, buf, len);
}
bool pit_symbol_name_match_cstr(pit_runtime *rt, pit_value sym, char *s) {
    return pit_symbol_name_match(rt, sym, (u8 *) s, strlen(s));
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
    return pit_cell_get(rt, pit_get_value_cell(rt, sym));
}
void pit_set(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symbol idx = pit_as_symbol(rt, sym);
    if (idx < rt->frozen_symtab) { pit_error(rt, "attempted to modify frozen symbol"); return; }
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    if (pit_value_sort(ent->value) != PIT_VALUE_SORT_REF) {
        ent->value = pit_cell_new(rt, PIT_NIL);
    }
    pit_cell_set(rt, ent->value, v);
}
pit_value pit_fget(pit_runtime *rt, pit_value sym) {
    return pit_cell_get(rt, pit_get_function_cell(rt, sym));
}
void pit_fset(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symbol idx = pit_as_symbol(rt, sym);
    if (idx < rt->frozen_symtab) { pit_error(rt, "attempted to modify frozen symbol"); return; }
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    if (pit_value_sort(ent->function) != PIT_VALUE_SORT_REF) {
        ent->function = pit_cell_new(rt, PIT_NIL);
    }
    pit_cell_set(rt, ent->function, v);
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
    // although we cannot set frozen symbols, we can still bind them temporarily - no need to check
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
    pit_value_heavy *h = pit_deref(rt, ret);
    if (!h) { pit_error(rt, "failed to create new heavy value for cell"); return PIT_NIL; }
    h->hsort = PIT_VALUE_HEAVY_SORT_CELL;
    h->cell = v;
    return ret;
}
pit_value pit_cell_get(pit_runtime *rt, pit_value cell) {
    if (pit_value_sort(cell) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "cell value is not ref");
        return PIT_NIL;
    }
    pit_value_heavy *h = pit_deref(rt, pit_as_ref(rt, cell));
    if (!h) { pit_error(rt, "bad ref"); return PIT_NIL; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CELL) {
        pit_error(rt, "cell value ref does not point to cell");
        return PIT_NIL;
    }
    return h->cell;
}
void pit_cell_set(pit_runtime *rt, pit_value cell, pit_value v) {
    if (pit_value_sort(cell) != PIT_VALUE_SORT_REF) {
        pit_error(rt, "cell value is not ref");
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
    h->cell = v;
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
    pit_value temp[64];
    if (num > 64) { pit_error(rt, "failed to create list of size %d\n", num); return PIT_NIL; }
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
void pit_setcar(pit_runtime *rt, pit_value v, pit_value x) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return; }
    pit_ref idx = pit_as_ref(rt, v);
    if (idx < rt->frozen_values) { pit_error(rt, "attempted to modify frozen cons"); return; }
    pit_value_heavy *h = pit_deref(rt, idx);
    if (!h) { pit_error(rt, "bad ref"); return; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) { pit_error(rt, "not a cons"); return; }
    h->cons.car = x;
}
void pit_setcdr(pit_runtime *rt, pit_value v, pit_value x) {
    if (pit_value_sort(v) != PIT_VALUE_SORT_REF) { pit_error(rt, "not a ref"); return; }
    pit_ref idx = pit_as_ref(rt, v);
    if (idx < rt->frozen_values) { pit_error(rt, "attempted to modify frozen cons"); return; }
    pit_value_heavy *h = pit_deref(rt, idx);
    if (!h) { pit_error(rt, "bad ref"); return; }
    if (h->hsort != PIT_VALUE_HEAVY_SORT_CONS) { pit_error(rt, "not a cons"); return; }
    h->cons.cdr = x;
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
        if (pit_eq(needle, pit_car(rt, haystack))) return pit_intern_cstr(rt, "t");
        haystack = pit_cdr(rt, haystack);
    }
    return PIT_NIL;
}

pit_value pit_free_vars(pit_runtime *rt, pit_value args, pit_value body) {
    i64 expr_stack_reset = rt->expr_stack->top;
    pit_value bound = args;
    pit_value ret = PIT_NIL;
    pit_values_push(rt, rt->expr_stack, body);
    while (rt->expr_stack->top > 0) {
        pit_value cur = pit_values_pop(rt, rt->expr_stack);
        if (pit_is_cons(rt, cur)) {
            pit_value fsym = pit_car(rt, cur);
            bool is_symbol = pit_is_symbol(rt, fsym);
            pit_value args = pit_cdr(rt, cur);
            if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "lambda")) {
                bound = pit_append(rt, pit_car(rt, args), bound);
            } else if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "quote")) {
                // don't look inside quote!
                // if we add other special forms, make sure to consider them here if necessary!
            } else {
                while (args != PIT_NIL) {
                    pit_values_push(rt, rt->expr_stack, pit_car(rt, args));
                    args = pit_cdr(rt, args);
                }
                if (!is_symbol) {
                    pit_values_push(rt, rt->expr_stack, fsym);
                }
            }
        } else if (pit_is_symbol(rt, cur)) {
            if (pit_contains_eq(rt, cur, bound) == PIT_NIL) {
                ret = pit_cons(rt, cur, ret);
            }
        }
    }
    rt->expr_stack->top = expr_stack_reset;
    return ret;
}
pit_value pit_lambda(pit_runtime *rt, pit_value args, pit_value body) {
    pit_value ret = pit_heavy_new(rt);
    pit_value_heavy *h = pit_deref(rt, ret);
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
    while (args != PIT_NIL) {
        pit_value nm = pit_car(rt, args);
        pit_value ent = pit_cons(rt, nm, pit_cell_new(rt, PIT_NIL));
        arg_cells = pit_cons(rt, ent, arg_cells);
        args = pit_cdr(rt, args);
    }
    arg_cells = pit_reverse(rt, arg_cells);
    h->func.args = arg_cells;
    h->func.env = env;
    h->func.body = expanded;
    return ret;
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
        if (h->hsort == PIT_VALUE_HEAVY_SORT_FUNC) {
            pit_value bound = PIT_NIL;
            pit_value env = h->func.env;
            while (env != PIT_NIL) {
                pit_value b = pit_car(rt, env);
                pit_value nm = pit_car(rt, b);
                pit_bind(rt, nm, pit_cdr(rt, b));
                bound = pit_cons(rt, nm, bound);
                env = pit_cdr(rt, env);
            }
            pit_value anames = h->func.args;
            while (anames != PIT_NIL) {
                pit_value aform = pit_car(rt, anames);
                pit_value nm = pit_car(rt, aform);
                pit_value cell = pit_cdr(rt, aform);
                pit_cell_set(rt, cell, pit_car(rt, args));
                pit_bind(rt, nm, cell);
                bound = pit_cons(rt, nm, bound);
                args = pit_cdr(rt, args);
                anames = pit_cdr(rt, anames);
            }
            pit_value ret = pit_eval(rt, h->func.body);
            while (bound != PIT_NIL) {
                pit_unbind(rt, pit_car(rt, bound));
                bound = pit_cdr(rt, bound);
            }
            return ret;
        } else if (h->hsort == PIT_VALUE_HEAVY_SORT_NATIVEFUNC) {
            return h->nativefunc(rt, args);
        } else {
            pit_error(rt, "attempt to apply non-nativefunc ref");
            return PIT_NIL;
        }
    default:
        pit_error(rt, "attempted to apply non-function value");
        return PIT_NIL;
    }
}

pit_values *pit_values_new(i64 capacity) {
    i64 cap = capacity / sizeof(pit_value);
    pit_values *ret = malloc(sizeof(*ret) + cap * sizeof(pit_value));
    ret->top = 0;
    ret->cap = cap;
    return ret;
}
void pit_values_push(pit_runtime *rt, pit_values *s, pit_value x) {
    (void) rt;
    s->data[s->top++] = x;
    if (s->top >= s->cap) { pit_error(rt, "evaluation stack overflow"); }
}
pit_value pit_values_pop(pit_runtime *rt, pit_values *s) {
    if (s->top == 0) { pit_error(rt, "evaluation stack underflow"); return PIT_NIL; }
    return s->data[--s->top];
}

pit_runtime_eval_program *pit_runtime_eval_program_new(i64 capacity) {
    i64 cap = capacity / sizeof(pit_runtime_eval_program_entry);
    pit_runtime_eval_program *ret = malloc(sizeof(*ret) + cap * sizeof(pit_runtime_eval_program_entry));
    ret->top = 0;
    ret->cap = cap;
    return ret;
}
void pit_runtime_eval_program_push(pit_runtime *rt, pit_runtime_eval_program *s, pit_runtime_eval_program_entry x) {
    (void) rt;
    s->data[s->top++] = x;
    if (s->top >= s->cap) { pit_error(rt, "evaluation program overflow"); }
}

pit_value pit_expand_macros(pit_runtime *rt, pit_value top) {
    i64 expr_stack_reset = rt->expr_stack->top;
    i64 result_stack_reset = rt->result_stack->top;
    i64 program_reset = rt->program->top;
    pit_values_push(rt, rt->expr_stack, top);
    while (rt->expr_stack->top > 0) {
        if (rt->error != PIT_NIL) goto end;
        pit_value cur = pit_values_pop(rt, rt->expr_stack);
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
                pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                    .sort = EVAL_PROGRAM_ENTRY_LITERAL,
                    .literal = pit_car(rt, args),
                });
            } else if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "quote")) {
                pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                    .sort = EVAL_PROGRAM_ENTRY_LITERAL,
                    .literal = cur,
                });
            } else if (is_symbol && pit_symbol_name_match_cstr(rt, fsym, "lambda")) {
                pit_value args = pit_cdr(rt, cur);
                pit_value bindings = pit_car(rt, args);
                pit_value body = pit_cdr(rt, args);
                pit_values_push(rt, rt->expr_stack, pit_list(rt, 2, pit_intern_cstr(rt, "defer"), bindings));
                i64 argcount = 0;
                while (body != PIT_NIL) {
                    pit_value a = pit_car(rt, body);
                    pit_values_push(rt, rt->expr_stack, a);
                    body = pit_cdr(rt, body);
                    argcount += 1;
                }
                pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                    .sort = EVAL_PROGRAM_ENTRY_APPLY,
                    .apply = argcount + 1,
                });
                pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                    .sort = EVAL_PROGRAM_ENTRY_LITERAL,
                    .literal = fsym,
                });
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
                pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                    .sort = EVAL_PROGRAM_ENTRY_APPLY,
                    .apply = argcount,
                });
                if (is_symbol) {
                    pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                        .sort = EVAL_PROGRAM_ENTRY_LITERAL,
                        .literal = fsym,
                    });
                }
            }
        } else {
            pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                .sort = EVAL_PROGRAM_ENTRY_LITERAL,
                .literal = cur,
            });
        }
    }
    for (i64 idx = rt->program->top - 1; idx >= program_reset; --idx) {
        if (rt->error != PIT_NIL) goto end;
        pit_runtime_eval_program_entry *ent = &rt->program->data[idx];
        switch (ent->sort) {
        case EVAL_PROGRAM_ENTRY_LITERAL:
            pit_values_push(rt, rt->result_stack, ent->literal);
            break;
        case EVAL_PROGRAM_ENTRY_APPLY:
            pit_value f = pit_values_pop(rt, rt->result_stack);
            pit_value args = PIT_NIL;
            for (i64 i = 0; i < ent->apply; ++i) {
                args = pit_cons(rt, pit_values_pop(rt, rt->result_stack), args);
            }
            pit_values_push(rt, rt->result_stack, pit_cons(rt, f, args));
            break;
        default:
            pit_error(rt, "unknown program entry");
            goto end;
        }
    }
end:
    pit_value ret = pit_values_pop(rt, rt->result_stack);
    rt->expr_stack->top = expr_stack_reset;
    rt->result_stack->top = result_stack_reset;
    rt->program->top = program_reset;
    return ret;
}

pit_value pit_eval(pit_runtime *rt, pit_value top) {
    i64 expr_stack_reset = rt->expr_stack->top;
    i64 result_stack_reset = rt->result_stack->top;
    i64 program_reset = rt->program->top;
    pit_values_push(rt, rt->expr_stack, top);
    // first, convert the expression tree into "polish notation" in program
    while (rt->expr_stack->top > 0) {
        if (rt->error != PIT_NIL) goto end;
        pit_value cur = pit_values_pop(rt, rt->expr_stack);
        if (pit_is_cons(rt, cur)) { // compound expressions: function/macro application special forms
            pit_value fsym = pit_car(rt, cur);
            bool is_symbol = pit_is_symbol(rt, fsym);
            if (is_symbol && pit_is_symbol_special_form(rt, fsym)) { // special forms
                pit_value f = pit_fget(rt, fsym);
                pit_value args = pit_cdr(rt, cur);
                // special forms are nativefuncs that directly manipulate the stacks
                // basically macros, but we don't need to evaluate the return value
                pit_apply(rt, f, args);
            } else if (is_symbol && pit_is_symbol_macro(rt, fsym)) { // macros
                pit_value f = pit_fget(rt, fsym);
                pit_value args = pit_cdr(rt, cur);
                pit_value res = pit_apply(rt, f, args);
                pit_values_push(rt, rt->expr_stack, res);
            } else { // normal functions
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
                pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                    .sort = EVAL_PROGRAM_ENTRY_APPLY,
                    .apply = argcount,
                });
                if (is_symbol) {
                    pit_value f = pit_fget(rt, fsym);
                    pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                        .sort = EVAL_PROGRAM_ENTRY_LITERAL,
                        .literal = f,
                    });
                }
            }
        } else if (pit_is_symbol(rt, cur)) { // unquoted symbols: variable lookup
            pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                .sort = EVAL_PROGRAM_ENTRY_LITERAL,
                .literal = pit_get(rt, cur),
            });
        } else { // other expressions evaluate to themselves!
            pit_runtime_eval_program_push(rt, rt->program, (pit_runtime_eval_program_entry) {
                .sort = EVAL_PROGRAM_ENTRY_LITERAL,
                .literal = cur,
            });
        }
    }
    // then, execute the polish notation program from right to left
    // this has the nice consequence of putting the arguments in the right order
    for (i64 idx = rt->program->top - 1; idx >= program_reset; --idx) {
        if (rt->error != PIT_NIL) goto end;
        pit_runtime_eval_program_entry *ent = &rt->program->data[idx];
        switch (ent->sort) {
        case EVAL_PROGRAM_ENTRY_LITERAL:
            pit_values_push(rt, rt->result_stack, ent->literal);
            break;
        case EVAL_PROGRAM_ENTRY_APPLY:
            pit_value f = pit_values_pop(rt, rt->result_stack);
            pit_value args = PIT_NIL;
            for (i64 i = 0; i < ent->apply; ++i) {
                args = pit_cons(rt, pit_values_pop(rt, rt->result_stack), args);
            }
            pit_values_push(rt, rt->result_stack, pit_apply(rt, f, args));
            break;
        default:
            pit_error(rt, "unknown program entry");
            goto end;
        }
    }
end:
    pit_value ret = pit_values_pop(rt, rt->result_stack);
    rt->expr_stack->top = expr_stack_reset;
    rt->result_stack->top = result_stack_reset;
    rt->program->top = program_reset;
    return ret;
}
