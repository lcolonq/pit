#include <lcq/pit/arena.h>
#include <lcq/pit/utils.h>

pit_arena *pit_arena_new(u8 *buf, i64 buf_len, i64 elem_size) {
    uintptr_t base = (uintptr_t) buf;
    uintptr_t aligned = pit_align_up(base, sizeof(void *));
    pit_arena *a = (pit_arena *) aligned;
    uintptr_t data = aligned + sizeof(pit_arena);
    i64 offset = (i64) data - (i64) base;
    i64 remaining = (i64) pit_align_down((uintptr_t) (buf_len - offset), sizeof(void *));
    if (!a || remaining <= 0) return NULL;
    a->elem_size = elem_size;
    a->capacity = remaining;
    a->next = 0;
    a->back = remaining;
    return a;
}
void pit_arena_reset(pit_arena *a) {
    a->next = 0;
    a->back = a->capacity;
}
static i64 pit_arena_byte_idx(pit_arena *a, pit_arena_index idx) {
    i64 byte_idx = 0; pit_mul(&byte_idx, a->elem_size, idx);
    return byte_idx;
}
pit_arena_index pit_arena_alloc_index(pit_arena *a) {
    i64 ret = a->next;
    i64 byte_idx = pit_arena_byte_idx(a, ret);
    if (byte_idx + a->elem_size >= a->back) { return -1; }
    a->next += 1;
    return ret;
}
pit_arena_index pit_arena_alloc_array_index(pit_arena *a, i64 num) {
    i64 ret = a->next;
    i64 byte_idx = pit_arena_byte_idx(a, ret);
    i64 byte_len = 0; pit_mul(&byte_len, a->elem_size, num);
    if (byte_idx + byte_len > a->back) { return -1; }
    a->next += num;
    return ret;
}
void *pit_arena_alloc(pit_arena *a) {
    return pit_arena_get(a, pit_arena_alloc_index(a));
}
void *pit_arena_alloc_array(pit_arena *a, i64 num) {
    return pit_arena_get(a, pit_arena_alloc_array_index(a, num));
}

void *pit_arena_get(pit_arena *a, pit_arena_index idx) {
    i64 byte_idx = pit_arena_byte_idx(a, idx);
    if (byte_idx < 0 || byte_idx + a->elem_size >= a->back) { return NULL; }
    return &a->data[byte_idx];
}

void *pit_arena_alloc_back(pit_arena *a, i64 sz) {
    i64 next_byte = pit_arena_byte_idx(a, a->next);
    i64 back_byte = (i64) pit_align_down((uintptr_t) (a->back - sz), sizeof(void *));
    if (back_byte < next_byte) return NULL;
    a->back = back_byte;
    return &a->data[a->back];
}
