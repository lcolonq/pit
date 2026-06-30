#ifndef LCOLONQ_PIT_RUNTIME_H
#define LCOLONQ_PIT_RUNTIME_H

#include <lcq/pit/types.h>
#include <lcq/pit/utils.h>
#include <lcq/pit/vec.h>
#include <lcq/pit/arena.h>
#include <lcq/pit/lexer.h>

typedef u64 pit_value;
typedef i64 pit_symbol; /* a symbol at runtime is an index into the runtime's symbol table */
typedef i64 pit_ref; /* a reference is an index into the runtime's arena */
PIT_DECLARE_VEC(pit_value)

struct pit_runtime;

/* symbol table entries. these are created/looked up when you intern a symbol */
typedef struct {
    pit_value name; /* ref to bytestring */
    pit_value value; /* ref to cell */
    pit_value function; /* ref to cell */
    bool is_macro, is_special_form, is_keyword;
} pit_symtab_entry;
PIT_DECLARE_VEC(pit_symtab_entry)

/* annotation attached to (some) heavy values detailing things like line numbers */
typedef struct {
    i64 line, column;
} pit_annotation;
typedef struct {
    pit_ref ref;
    pit_annotation annotation;
} pit_annotated_ref;
PIT_DECLARE_VEC(pit_annotated_ref)
void pit_annotation_set(struct pit_runtime *rt, pit_ref ref, pit_annotation annotation);
pit_annotated_ref *pit_annotation_get(struct pit_runtime *rt, pit_ref ref);

/* "programs"; vectors of "instructions" for a very simple VM used by the evaluator */
typedef struct {
    enum {
        PIT_RUNTIME_EVAL_INS_LITERAL,
        PIT_RUNTIME_EVAL_INS_APPLY
    } sort;
    union {
        pit_value literal;
        struct { i64 arity; pit_annotated_ref *annotation; } apply; 
    } in;
} pit_runtime_eval_ins;
PIT_DECLARE_VEC(pit_runtime_eval_ins)
void pit_runtime_eval_program_push_literal(struct pit_runtime *rt, pit_vec(pit_runtime_eval_ins) *s, pit_value x);
void pit_runtime_eval_program_push_apply(struct pit_runtime *rt, pit_vec(pit_runtime_eval_ins) *s, i64 arity, pit_annotated_ref *annotation);

typedef struct pit_runtime {
    /* interpreter state */
    pit_arena *heap; /* all heavy values, bytestrings, and arrays. */
    /* bytestrings and arrays are allocated at the end (descending), heavy values are allocated at the front */
    /* this allows us to iterate over only heavy values at the front (useful in Cheney's algorithm for GC */
    pit_arena *backbuffer; /* additional allocation, the same size as the heap (used by GC) */
    pit_vec(pit_annotated_ref) *annotations;
    pit_vec(pit_annotated_ref) *backtrace; /* we reuse this vector for both backtraces and the GC */
    pit_vec(pit_symtab_entry) *symtab;/* all symbols */
    /* temporary/"scratch" memory */
    pit_vec(pit_value) *saved_bindings; /* stack used to save old values of bindings to be restored ("shallow binding") */
    pit_vec(pit_value) *expr_stack; /* stack of subexpressions to evaluate during evaluation */
    pit_vec(pit_value) *result_stack; /* stack of intermediate values during evaluation */
    pit_vec(pit_runtime_eval_ins) *program; /* intermediate stack-based program constructed during evaluation */
    /* bookkeeping */
    /* "frozen" values offsets: values before these offsets are immutable, and we can reset here later */
    i64 frozen_values, frozen_symtab;
    pit_value error; /* error value - if this is non-nil, an error has occured! only tracks the first error */
    i64 source_line, source_column; /* for error reporting only; line and column of token start */
    i64 error_line, error_column; /* line and column of token start at time of error */
} pit_runtime;
pit_runtime *pit_runtime_new(u8 *buf, i64 len);

void pit_runtime_freeze(pit_runtime *rt); /* freeze the runtime at the current point - everything currently defined becomes immutable */
void pit_runtime_reset(pit_runtime *rt); /* restore the runtime to the frozen point, resetting everything that has happened since */
bool pit_runtime_print_error(pit_runtime *rt); /* return true if an error has occured, and print to stderr */

#define pit_debug_trace(rt, v) pit_debug_trace_(rt, "Trace [" __FILE__ ":" PIT_STR(__LINE__) "] %s\n", v)
void pit_debug_trace_(pit_runtime *rt, char *format, pit_value v);
pit_value pit_error_get(pit_runtime *rt);
void pit_error(pit_runtime *rt, char *format, ...);

/* repl / file loading */
pit_value pit_load_file(pit_runtime *rt, char *path);
void pit_repl(pit_runtime *rt);

#include <lcq/pit/runtime/value.h>
#include <lcq/pit/runtime/symtab.h>
#include <lcq/pit/runtime/dump.h>
#include <lcq/pit/runtime/macroexpand.h>
#include <lcq/pit/runtime/eval.h>
#include <lcq/pit/runtime/gc.h>

#endif
