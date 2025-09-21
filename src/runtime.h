#ifndef PIT_RUNTIME_H
#define PIT_RUNTIME_H

#include "types.h"
#include "utils.h"

typedef struct {
    i64 elem_size, capacity, next;
    u8 data[];
} pit_arena;
pit_arena *pit_arena_new(i64 capacity, i64 elem_size);
i32 pit_arena_alloc_idx(pit_arena *a);
void *pit_arena_alloc(pit_arena *a);

// nil is always the symbol with index 0
#define PIT_NIL 0b1111111111110100000000000000000000000000000000000000000000000000
enum pit_value_sort {
    PIT_VALUE_SORT_DOUBLE  = 0b00, // double
    PIT_VALUE_SORT_INTEGER = 0b01, // NaN-boxed 49-bit integer
    PIT_VALUE_SORT_SYMBOL  = 0b10, // NaN-boxed index into symbol table
    PIT_VALUE_SORT_REF     = 0b11, // NaN-boxed index into "heavy object" arena
};
typedef i32 pit_symbol; // a symbol at runtime is an index into the runtime's symbol table
typedef i32 pit_ref; // a reference is an index into the runtime's arena
typedef u64 pit_value;
enum pit_value_sort pit_value_sort(pit_value v);
u64 pit_value_data(pit_value v);

struct pit_runtime;
typedef pit_value (*pit_nativefunc)(struct pit_runtime *rt, pit_value args);
typedef struct { // "heavy" values, the targets of refs
    enum pit_value_heavy_sort {
        PIT_VALUE_HEAVY_SORT_CONS=0,
        PIT_VALUE_HEAVY_SORT_ARRAY,
        PIT_VALUE_HEAVY_SORT_BYTES,
        PIT_VALUE_HEAVY_SORT_NATIVEFUNC,
    } hsort;
    union {
        struct { pit_value car, cdr; } cons;
        struct { pit_value *data; i64 len; } array;
        struct { u8 *data; i64 len; } bytes;
        pit_nativefunc nativefunc;
    };
} pit_value_heavy;

typedef struct {
    pit_value name;
    pit_value value;
    pit_value function;
    bool is_macro;
} pit_symtab_entry;

typedef struct pit_runtime {
    pit_arena *values;
    pit_arena *bytes;
    pit_arena *symtab; i64 symtab_len;
    pit_value error;
} pit_runtime;

pit_runtime *pit_runtime_new();

i64 pit_dump(pit_runtime *rt, char *buf, i64 len, pit_value v);
#define pit_trace(rt, v) pit_trace_(rt, "Trace [" __FILE__ ":" PIT_STR(__LINE__) "] %s\n", v)
void pit_trace_(pit_runtime *rt, const char *format, pit_value v);
void pit_error(pit_runtime *rt, const char *format, ...);
void pit_check_error_maybe_panic(pit_runtime *rt);

// working with small values
pit_value pit_value_new(pit_runtime *rt, enum pit_value_sort s, u64 data);
double pit_as_double(pit_runtime *rt, pit_value v);
pit_value pit_double_new(pit_runtime *rt, double d);
i64 pit_as_integer(pit_runtime *rt, pit_value v);
pit_value pit_integer_new(pit_runtime *rt, i64 i);
pit_symbol pit_as_symbol(pit_runtime *rt, pit_value v);
pit_value pit_symbol_new(pit_runtime *rt, pit_symbol s);
pit_ref pit_as_ref(pit_runtime *rt, pit_value v);
pit_value pit_ref_new(pit_runtime *rt, pit_ref r);

// working with heavy values and refs
pit_value pit_heavy_new(pit_runtime *rt);
pit_value_heavy *pit_deref(pit_runtime *rt, pit_ref p);

// convenient predicates
bool pit_is_integer(pit_runtime *rt, pit_value a);
bool pit_is_double(pit_runtime *rt, pit_value a);
bool pit_is_symbol(pit_runtime *rt, pit_value a);
bool pit_is_value_heavy_sort(pit_runtime *rt, pit_value a, enum pit_value_heavy_sort e);
bool pit_is_cons(pit_runtime *rt, pit_value a);
bool pit_is_array(pit_runtime *rt, pit_value a);
bool pit_is_bytes(pit_runtime *rt, pit_value a);
bool pit_is_nativefunc(pit_runtime *rt, pit_value a);
bool pit_truthful(pit_value a);
bool pit_eq(pit_value a, pit_value b);
bool pit_equal(pit_runtime *rt, pit_value a, pit_value b);

// working with binary data
pit_value pit_bytes_new(pit_runtime *rt, u8 *buf, i64 len);
pit_value pit_bytes_new_cstr(pit_runtime *rt, char *s);
bool pit_bytes_match(pit_runtime *rt, pit_value v, u8 *buf, i64 len);

// working with the symbol table
pit_value pit_intern(pit_runtime *rt, u8 *nm, i64 len);
pit_value pit_intern_cstr(pit_runtime *rt, char *nm);
pit_symtab_entry *pit_symtab_lookup(pit_runtime *rt, pit_value sym);
pit_value pit_get(pit_runtime *rt, pit_value sym);
void pit_set(pit_runtime *rt, pit_value sym, pit_value v);
pit_value pit_fget(pit_runtime *rt, pit_value sym);
void pit_fset(pit_runtime *rt, pit_value sym, pit_value v);
bool pit_is_symbol_macro(pit_runtime *rt, pit_value sym);
void pit_symbol_is_macro(pit_runtime *rt, pit_value sym);
void pit_mset(pit_runtime *rt, pit_value sym, pit_value v);

// working with cons cells
pit_value pit_cons(pit_runtime *rt, pit_value car, pit_value cdr);
pit_value pit_list(pit_runtime *rt, i64 num, ...);
pit_value pit_car(pit_runtime *rt, pit_value v);
pit_value pit_cdr(pit_runtime *rt, pit_value v);

// working with functions
pit_value pit_nativefunc_new(pit_runtime *rt, pit_nativefunc f);
pit_value pit_apply(pit_runtime *rt, pit_value f, pit_value args);

// evaluation!
pit_value pit_eval(pit_runtime *rt, pit_value e);

#endif
