#ifndef LCOLONQ_PIT_RUNTIME_VALUE_CONS_H
#define LCOLONQ_PIT_RUNTIME_VALUE_CONS_H

#include <lcq/pit/runtime.h>
#include <lcq/pit/runtime/value.h>

/* heavy value - cons/list */
bool pit_value_is_cons(pit_runtime *rt, pit_value a);
pit_value pit_value_cons(pit_runtime *rt, pit_value car, pit_value cdr);
pit_value pit_value_cons_car(pit_runtime *rt, pit_value v);
pit_value pit_value_cons_cdr(pit_runtime *rt, pit_value v);
void pit_value_cons_setcar(pit_runtime *rt, pit_value v, pit_value x);
void pit_value_cons_setcdr(pit_runtime *rt, pit_value v, pit_value x);

pit_value pit_value_list(pit_runtime *rt, i64 num, ...);
i64 pit_value_list_len(pit_runtime *rt, pit_value xs);
pit_value pit_value_list_append(pit_runtime *rt, pit_value xs, pit_value ys);
pit_value pit_value_list_reverse(pit_runtime *rt, pit_value xs);
pit_value pit_value_list_contains_eq(pit_runtime *rt, pit_value needle, pit_value haystack);
pit_value pit_value_list_contains_equal(pit_runtime *rt, pit_value needle, pit_value haystack);
pit_value pit_value_list_plist_get(pit_runtime *rt, pit_value k, pit_value vs);

#endif
