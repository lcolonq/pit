#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <lcq/pit/utils.h>

void pit_panic(const char *format, ...) {
    va_list vargs;
    va_start(vargs, format);
    vfprintf(stderr, format, vargs);
    va_end(vargs);
    exit(1);
}

void pit_debug(const char *format, ...) {
    va_list vargs;
    va_start(vargs, format);
    vfprintf(stderr, format, vargs);
    va_end(vargs);
}

static uintptr_t pit_align_down(uintptr_t addr, uintptr_t align) {
    return addr & ~(align - 1); /* easy! just zero the low bits */
}
static uintptr_t pit_align_up(uintptr_t addr, uintptr_t align) {
    return (addr + align - 1) /* increment past the next aligned address... */
        & ~(align - 1); /* ...and then zero the low bits */
}

pit_arena *pit_arena_new(i64 capacity, i64 elem_size) {
    i64 byte_len = 0;
    pit_mul(&byte_len, elem_size, capacity);
    pit_arena *a = (pit_arena *) malloc(sizeof(pit_arena) + (size_t) byte_len);
    if (!a || byte_len <= 0) return NULL;
    a->elem_size = elem_size;
    a->capacity = byte_len / elem_size;
    a->next = 0;
    a->back = byte_len;
    return a;
}
void pit_arena_reset(pit_arena *a) {
    a->next = 0;
    pit_mul(&a->back, a->elem_size, a->capacity);
}
static i64 pit_arena_byte_idx(pit_arena *a, i64 idx) {
    i64 byte_idx = 0; pit_mul(&byte_idx, a->elem_size, idx);
    return byte_idx;
}
i64 pit_arena_alloc_idx(pit_arena *a) {
    i64 ret = a->next;
    i64 byte_idx = pit_arena_byte_idx(a, ret);
    if (byte_idx + a->elem_size >= a->back) { return -1; }
    a->next += 1;
    return ret;
}
i64 pit_arena_alloc_bulk_idx(pit_arena *a, i64 num) {
    i64 ret = a->next;
    i64 byte_idx = pit_arena_byte_idx(a, ret);
    i64 byte_len = 0; pit_mul(&byte_len, a->elem_size, num);
    if (byte_idx + byte_len > a->back) { return -1; }
    a->next += num;
    return ret;
}
void *pit_arena_get(pit_arena *a, i64 idx) {
    i64 byte_idx = pit_arena_byte_idx(a, idx);
    if (byte_idx < 0 || byte_idx + a->elem_size >= a->back) { return NULL; }
    return &a->data[byte_idx];
}
void *pit_arena_alloc(pit_arena *a) {
    i64 idx = pit_arena_alloc_idx(a);
    return pit_arena_get(a, idx);
}
void *pit_arena_alloc_bulk(pit_arena *a, i64 num) {
    i64 idx = pit_arena_alloc_bulk_idx(a, num);
    return pit_arena_get(a, idx);
}
void *pit_arena_alloc_back(pit_arena *a, i64 sz) {
    i64 next_byte = pit_arena_byte_idx(a, a->next);
    i64 back_byte = (i64) pit_align_down((uintptr_t) (a->back - sz), sizeof(void *));
    if (back_byte < next_byte) return NULL;
    a->back = back_byte;
    return &a->data[a->back];
}
