#ifndef LCOLONQ_PIT_RUNTIME_VALUE_ARRAY_H
#define LCOLONQ_PIT_RUNTIME_VALUE_ARRAY_H

#include <lcq/pit/runtime.h>
#include <lcq/pit/runtime/value.h>

/* heavy value - array */
bool pit_value_is_array(pit_runtime *rt, pit_value a);
pit_value pit_value_array_new(pit_runtime *rt, i64 len);
pit_value pit_value_array_from_buf(pit_runtime *rt, pit_value *xs, i64 len);
i64 pit_value_array_len(pit_runtime *rt, pit_value arr);
pit_value pit_value_array_get(pit_runtime *rt, pit_value arr, i64 idx);
pit_value pit_value_array_set(pit_runtime *rt, pit_value arr, i64 idx, pit_value v);

#endif
