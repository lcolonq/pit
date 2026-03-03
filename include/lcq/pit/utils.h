#ifndef LCOLONQ_PIT_UTILS_H
#define LCOLONQ_PIT_UTILS_H

#include <stdckdint.h>
#include <lcq/pit/types.h>

/* assorted utilities and debugging tools */
#define PIT_STRSTR(x) #x
#define PIT_STR(x) PIT_STRSTR(x)
void pit_panic(const char *format, ...);
void pit_debug(const char *format, ...);
#define pit_mul(result, a, b) if (ckd_mul(result, a, b)) pit_panic("integer overflow during multiplication%s","");

/* arenas */
typedef struct {
    i64 elem_size, capacity, next, back;
    u8 data[];
} pit_arena;
pit_arena *pit_arena_new(i64 len, i64 elem_size);
void pit_arena_reset(pit_arena *a);
i64 pit_arena_alloc_idx(pit_arena *a);
i64 pit_arena_alloc_bulk_idx(pit_arena *a, i64 num);
void *pit_arena_get(pit_arena *a, i64 idx);
void *pit_arena_alloc(pit_arena *a);
void *pit_arena_alloc_bulk(pit_arena *a, i64 num);
void *pit_arena_alloc_back(pit_arena *a, i64 sz);

#endif
