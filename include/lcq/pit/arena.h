#ifndef LCOLONQ_PIT_ARENA_H
#define LCOLONQ_PIT_ARENA_H

#include <lcq/pit/types.h>

typedef i64 pit_arena_index;

static inline uintptr_t pit_align_down(uintptr_t addr, uintptr_t align) {
    return addr & ~(align - 1); /* easy! just zero the low bits */
}
static inline uintptr_t pit_align_up(uintptr_t addr, uintptr_t align) {
    return (addr + align - 1) /* increment past the next aligned address... */
        & ~(align - 1); /* ...and then zero the low bits */
}
typedef struct {
    i64 elem_size, /* size of one element */
        capacity, /* capacity in of data in bytes - only used to reset */
        next, /* index (in elements) of next element to insert */
        back; /* index (in bytes) one past the end of data. */
    /* back starts at capacity, and decreases as you alloc_back */
    u8 data[];
} pit_arena;

/* create a new arena in a buffer of buf_len size in bytes that stores elements of elem_size */
pit_arena *pit_arena_new(u8 *buf, i64 buf_len, i64 elem_size);

/* remove all elements from an array */
void pit_arena_reset(pit_arena *a);

/* allocate space for one or multiple elements, and return the index of the first element */
pit_arena_index pit_arena_alloc_index(pit_arena *a);
pit_arena_index pit_arena_alloc_array_index(pit_arena *a, i64 num);

/* allocate space for one or multiple elements, and return a pointer */
void *pit_arena_alloc(pit_arena *a);
void *pit_arena_alloc_array(pit_arena *a, i64 num);

/* retrieve a pointer to the element(s) at a given index */
void *pit_arena_get(pit_arena *a, pit_arena_index idx);

/* allocate arbitrary bytes on the "back" of the arena.
   this can be an arbitrary size in bytes */
void *pit_arena_alloc_back(pit_arena *a, i64 sz);

#endif
