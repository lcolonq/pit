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

pit_runtime *pit_runtime_new(u8 *buf, i64 len) {
    pit_arena *a = pit_arena_new(buf, len, sizeof(u8));
    pit_runtime *ret = pit_arena_alloc_back(a, sizeof(*ret));
    i64 heap_size = len / 4;
    i64 annotations_size = len / 32;
    i64 symtab_size = len / 16;
    i64 stack_size = len / 32;
    ret->heap = pit_arena_new(pit_arena_alloc_back(a, heap_size), heap_size, sizeof(pit_value_heavy));
    ret->backbuffer = pit_arena_new(pit_arena_alloc_back(a, heap_size), heap_size, sizeof(pit_value_heavy));
    ret->annotations = pit_vec_new(pit_annotated_ref)(pit_arena_alloc_back(a, annotations_size), annotations_size);
    ret->backtrace = pit_vec_new(pit_annotated_ref)(pit_arena_alloc_back(a, annotations_size), annotations_size);
    ret->symtab = pit_vec_new(pit_symtab_entry)(pit_arena_alloc_back(a, symtab_size), symtab_size);
    ret->expr_stack = pit_vec_new(pit_value)(pit_arena_alloc_back(a, stack_size), stack_size);
    ret->result_stack = pit_vec_new(pit_value)(pit_arena_alloc_back(a, stack_size), stack_size);
    ret->program = pit_vec_new(pit_runtime_eval_ins)(pit_arena_alloc_back(a, stack_size), stack_size);
    ret->saved_bindings = pit_vec_new(pit_value)(pit_arena_alloc_back(a, stack_size), stack_size);
    ret->frozen_values = 0;
    ret->frozen_symtab = 0;
    ret->error = PIT_NIL;
    ret->source_line = ret->source_column = -1;
    ret->error_line = ret->error_column = -1;
    pit_value nil = pit_symtab_intern_cstr(ret, "nil"); /* nil must be the 0th symbol for PIT_NIL to work */
    pit_symtab_set(ret, nil, PIT_NIL);
    pit_value truth = pit_symtab_intern_cstr(ret, "t");
    pit_symtab_set(ret, truth, truth);
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

pit_value pit_error_get(pit_runtime *rt) {
    pit_value ret = rt->error;
    rt->error = PIT_NIL;
    return ret;
}

void pit_error(pit_runtime *rt, char *format, ...) {
    if (rt->error == PIT_NIL) { /* only record the first error encountered */
        char buf[1024] = {0};
        va_list vargs;
        va_start(vargs, format);
        pit_libc_string_snprintf(buf, sizeof(buf), format, vargs);
        va_end(vargs);
        rt->error = PIT_T; /* we set the error now to prevent infinite recursion */
        rt->error = pit_value_bytes_new_cstr(rt, buf); /* in case this errs also */
        if (rt->error == PIT_NIL) rt->error = PIT_T;
        rt->error_line = rt->source_line;
        rt->error_column = rt->source_column;
    }
}

void pit_annotation_set(struct pit_runtime *rt, pit_ref ref, pit_annotation annotation) {
    pit_annotated_ref a;
    a.ref = ref;
    a.annotation = annotation;
    if (pit_vec_push(pit_annotated_ref)(rt->annotations, a) < 0)
        pit_error(rt, "annotation overflow");
}
pit_annotated_ref *pit_annotation_get(struct pit_runtime *rt, pit_ref ref) {
    for (i64 i = 0; i < rt->annotations->next; ++i) {
        pit_annotated_ref *a = pit_vec_get(pit_annotated_ref)(rt->annotations, i);
        if (a == NULL) pit_error(rt, "failed to get annotation");
        else if (a->ref == ref) {
            return a;
        }
    }
    return NULL;
}

void pit_runtime_eval_program_push_literal(pit_runtime *rt, pit_vec(pit_runtime_eval_ins) *s, pit_value x) {
    pit_runtime_eval_ins ent;
    ent.sort = PIT_RUNTIME_EVAL_INS_LITERAL;
    ent.in.literal = x;
    if (pit_vec_push(pit_runtime_eval_ins)(s, ent) < 0)
        pit_error(rt, "evaluation program overflow");
}
void pit_runtime_eval_program_push_apply(pit_runtime *rt, pit_vec(pit_runtime_eval_ins) *s, i64 arity, pit_annotated_ref *annotation) {
    pit_runtime_eval_ins ent;
    ent.sort = PIT_RUNTIME_EVAL_INS_APPLY;
    ent.in.apply.arity = arity;
    ent.in.apply.annotation = annotation;
    if (pit_vec_push(pit_runtime_eval_ins)(s, ent) < 0)
        pit_error(rt, "evaluation program overflow");
}
