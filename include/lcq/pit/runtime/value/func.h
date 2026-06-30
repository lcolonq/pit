#ifndef LCOLONQ_PIT_RUNTIME_VALUE_FUNC_H
#define LCOLONQ_PIT_RUNTIME_VALUE_FUNC_H

#include <lcq/pit/runtime.h>
#include <lcq/pit/runtime/value.h>

/* heavy value - func / nativefunc */
bool pit_value_is_func(pit_runtime *rt, pit_value a);
bool pit_value_is_nativefunc(pit_runtime *rt, pit_value a);
pit_value pit_value_func_lambda(pit_runtime *rt, pit_value args, pit_value body);
pit_value pit_value_nativefunc_new_with_data(pit_runtime *rt, pit_nativefunc f, void *data);
pit_value pit_value_nativefunc_new(pit_runtime *rt, pit_nativefunc f);
pit_value pit_value_apply(pit_runtime *rt, pit_value f, pit_value args);

#endif
