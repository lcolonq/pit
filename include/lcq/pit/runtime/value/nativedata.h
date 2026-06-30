#ifndef LCOLONQ_PIT_RUNTIME_VALUE_NATIVEDATA_H
#define LCOLONQ_PIT_RUNTIME_VALUE_NATIVEDATA_H

#include <lcq/pit/runtime.h>
#include <lcq/pit/runtime/value.h>

/* heavy value - nativedata */
bool pit_value_is_nativedata(pit_runtime *rt, pit_value a);
pit_value pit_value_nativedata_new(pit_runtime *rt, pit_value tag, void *d);
void *pit_value_nativedata_get(pit_runtime *rt, pit_value tag, pit_value v);

#endif
