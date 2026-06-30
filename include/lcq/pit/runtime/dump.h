#ifndef LCOLONQ_PIT_RUNTIME_DUMP_H
#define LCOLONQ_PIT_RUNTIME_DUMP_H

#include <lcq/pit/runtime.h>

/* pretty-print a pit value
   if readable is true, try to produce output that can be machine-read (quotes on strings, etc) */
i64 pit_dump(pit_runtime *rt, char *buf, i64 len, pit_value v, bool readable);

#endif
