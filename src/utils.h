#ifndef PIT_UTILS_H
#define PIT_UTILS_H

#include <stdckdint.h>

#define PIT_STRSTR(x) #x
#define PIT_STR(x) PIT_STRSTR(x)
void pit_panic(const char *format, ...);
void pit_debug(const char *format, ...);
#define pit_mul(result, a, b) if (ckd_mul(result, a, b)) pit_panic("integer overflow during multiplication%s","");


#endif
