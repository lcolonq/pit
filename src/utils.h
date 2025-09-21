#ifndef PIT_UTILS_H
#define PIT_UTILS_H

#include <stdckdint.h>

void pit_panic_(const char *format, ...);
void pit_debug_(const char *format, ...);
#define PIT_STRSTR(x) #x
#define PIT_STR(x) PIT_STRSTR(x)
#define pit_panic(format, ...) pit_panic_("error [" __FILE__ ":" PIT_STR(__LINE__) "] " format "\n" __VA_OPT__(,) __VA_ARGS__)
#define pit_debug(format, ...) pit_debug_("[" __FILE__ ":" PIT_STR(__LINE__) "] " format "\n" __VA_OPT__(,) __VA_ARGS__)
#define pit_mul(result, a, b) if (ckd_mul(result, a, b)) pit_panic("integer overflow during multiplication");


#endif
