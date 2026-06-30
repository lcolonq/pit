#ifndef LCOLONQ_PIT_RUNTIME_VALUE_CELL_H
#define LCOLONQ_PIT_RUNTIME_VALUE_CELL_H

#include <lcq/pit/runtime.h>
#include <lcq/pit/runtime/value.h>

/* heavy value - cell */

bool pit_value_is_cell(pit_runtime *rt, pit_value a);
pit_value pit_value_cell_new(pit_runtime *rt, pit_value v);
pit_value pit_value_cell_get(pit_runtime *rt, pit_value cell, pit_value sym);
void pit_value_cell_set(pit_runtime *rt, pit_value cell, pit_value v, pit_value sym);

#endif
