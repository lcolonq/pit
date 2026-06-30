#ifndef LCOLONQ_PIT_RUNTIME_VALUE_H
#define LCOLONQ_PIT_RUNTIME_VALUE_H

#include <lcq/pit/types.h>
#include <lcq/pit/runtime.h>

/* the basic value type - it's just a u64 */
enum pit_value_sort {
    PIT_VALUE_SORT_DOUBLE  = 0, /* 0b00 - double */
    PIT_VALUE_SORT_INTEGER = 1, /* 0b01 - NaN-boxed 49-bit integer */
    PIT_VALUE_SORT_SYMBOL  = 2, /* 0b10 - NaN-boxed index into symbol table */
    PIT_VALUE_SORT_REF     = 3  /* 0b11 - NaN-boxed index into "heavy object" arena */
};
enum pit_value_sort pit_value_sort(pit_value v);
u64 pit_value_data(pit_value v);

/* nil is always the symbol with index 0 */
#define PIT_NIL 0xfff4000000000000 /* 0b1111111111110100000000000000000000000000000000000000000000000000 */
#define PIT_T   (PIT_NIL+1)

/* "heavy" values, the targets of refs */
typedef pit_value (*pit_nativefunc)(struct pit_runtime *rt, pit_value args, void *data);
typedef struct {
    enum pit_value_heavy_sort {
        PIT_VALUE_HEAVY_SORT_CELL=0, /* value cell - basically, a "location" referred to by a variable binding */
        PIT_VALUE_HEAVY_SORT_CONS, /* cons cell - a pair of two values */
        PIT_VALUE_HEAVY_SORT_ARRAY, /* fixed-size array of values */
        PIT_VALUE_HEAVY_SORT_BYTES, /* bytestring */
        PIT_VALUE_HEAVY_SORT_FUNC, /* Lisp closure */
        PIT_VALUE_HEAVY_SORT_NATIVEFUNC, /* native function */
        PIT_VALUE_HEAVY_SORT_NATIVEDATA, /* native data (C pointer) */
        PIT_VALUE_HEAVY_SORT_FORWARDING_POINTER /* forwarding pointer to to-space (during GC) */
    } hsort;
    union {
        pit_value cell;
        struct { pit_value car, cdr; } cons;
        struct { pit_value *data; i64 len; } array;
        struct { u8 *data; i64 len; } bytes;
        struct { pit_value env; pit_value args; pit_value arg_rest_nm; pit_value body; } func;
        struct { pit_nativefunc f; void *data; } nativefunc;
        struct { pit_value tag; void *data; } nativedata;
        i64 forwarding_pointer;
    } in;
} pit_value_heavy;

pit_value pit_value_new(struct pit_runtime *rt, enum pit_value_sort s, u64 data);

bool pit_value_eq(pit_value a, pit_value b);
bool pit_value_equal(pit_runtime *rt, pit_value a, pit_value b);

#include <lcq/pit/runtime/value/small.h>
#include <lcq/pit/runtime/value/cell.h>
#include <lcq/pit/runtime/value/cons.h>
#include <lcq/pit/runtime/value/bytes.h>
#include <lcq/pit/runtime/value/array.h>
#include <lcq/pit/runtime/value/func.h>
#include <lcq/pit/runtime/value/nativedata.h>

#endif
