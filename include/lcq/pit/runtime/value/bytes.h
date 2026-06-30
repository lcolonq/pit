#ifndef LCOLONQ_PIT_RUNTIME_VALUE_BYTES_H
#define LCOLONQ_PIT_RUNTIME_VALUE_BYTES_H

#include <lcq/pit/runtime.h>
#include <lcq/pit/runtime/value.h>

/* heavy value - bytes */

bool pit_value_is_bytes(pit_runtime *rt, pit_value a);
pit_value pit_value_bytes_new(pit_runtime *rt, u8 *buf, i64 len);
pit_value pit_value_bytes_new_cstr(pit_runtime *rt, char *s);
pit_value pit_value_bytes_new_file(pit_runtime *rt, char *path);
bool pit_value_bytes_match(pit_runtime *rt, pit_value v, u8 *buf, i64 len);
i64 pit_value_bytes_copy(pit_runtime *rt, pit_value v, u8 *buf, i64 maxlen);

#endif
