#ifndef LCOLONQ_PIT_RUNTIME_VALUE_SMALL_H
#define LCOLONQ_PIT_RUNTIME_VALUE_SMALL_H

#include <lcq/pit/runtime.h>
#include <lcq/pit/runtime/value.h>

/* the small values that can be NaN-boxed: doubles, integers, symbols */

#ifndef PIT_NO_DOUBLE
double pit_value_as_double(pit_runtime *rt, pit_value v);
bool pit_value_is_double(pit_runtime *rt, pit_value a);
pit_value pit_value_double_new(pit_runtime *rt, double d);
#endif

i64 pit_value_as_integer(pit_runtime *rt, pit_value v);
bool pit_value_is_integer(pit_runtime *rt, pit_value a);
pit_value pit_value_integer_new(pit_runtime *rt, i64 i);
pit_value pit_value_bool_new(pit_runtime *rt, bool i);

pit_symbol pit_value_as_symbol(pit_runtime *rt, pit_value v);
bool pit_value_is_symbol(pit_runtime *rt, pit_value a);
pit_value pit_value_symbol_new(pit_runtime *rt, pit_symbol s);

pit_ref pit_value_as_ref(struct pit_runtime *rt, pit_value v);
bool pit_value_is_ref(pit_runtime *rt, pit_value a);
pit_value pit_value_ref_new(struct pit_runtime *rt, pit_ref r);
pit_value pit_value_ref_heavy_new(struct pit_runtime *rt);
pit_value_heavy *pit_value_ref_deref(struct pit_runtime *rt, pit_ref p);
bool pit_value_is_ref_heavy_sort(struct pit_runtime *rt, pit_value a, enum pit_value_heavy_sort e);

#endif
