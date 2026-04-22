#ifndef LCOLONQ_PIT_UTILS_H
#define LCOLONQ_PIT_UTILS_H

#include <stdarg.h>
#include <lcq/pit/types.h>

/* ctype */
static inline bool pit_ctype_isdigit(int a) { return a >= '0' && a <= '9'; }
static inline bool pit_ctype_islower(int a) { return a >= 'a' && a <= 'z'; }
static inline bool pit_ctype_isupper(int a) { return a >= 'A' && a <= 'Z'; }
static inline bool pit_ctype_isalpha(int a) { return pit_ctype_islower(a) || pit_ctype_isupper(a); }
static inline bool pit_ctype_isprint(int a) { return a >= 0x20 && a <= 0x7f; }
static inline bool pit_ctype_isspace(int a) { return a == ' ' || a == '\r' || a == '\n' || a == '\t'; }

/* string */
static inline size_t pit_string_strlen(char *s) {
    size_t idx = 0;
    while (s[idx] != 0) ++idx;
    return idx;
}
static inline u8 *pit_string_memcpy(u8 *dest, u8 *src, size_t n) {
    size_t i = 0;
    for (; i < n; ++i) dest[i] = src[i];
    return dest;
}
int pit_string_vsnprintf(char *str, size_t size, char *format, va_list ap);
int pit_string_snprintf(char *buf, size_t len, char *format, ...);

/* assorted utilities and debugging tools */
#define pit_mul(result, a, b) *result = (i64) (a) * (i64) (b)

/* arenas */
static inline uintptr_t pit_align_down(uintptr_t addr, uintptr_t align) {
    return addr & ~(align - 1); /* easy! just zero the low bits */
}
static inline uintptr_t pit_align_up(uintptr_t addr, uintptr_t align) {
    return (addr + align - 1) /* increment past the next aligned address... */
        & ~(align - 1); /* ...and then zero the low bits */
}
typedef struct {
    i64 elem_size, capacity, next, back;
    u8 data[];
} pit_arena;
pit_arena *pit_arena_new(u8 *buf, i64 buf_len, i64 elem_size);
void pit_arena_reset(pit_arena *a);
i64 pit_arena_alloc_idx(pit_arena *a);
i64 pit_arena_alloc_bulk_idx(pit_arena *a, i64 num);
void *pit_arena_get(pit_arena *a, i64 idx);
void *pit_arena_alloc(pit_arena *a);
void *pit_arena_alloc_bulk(pit_arena *a, i64 num);
void *pit_arena_alloc_back(pit_arena *a, i64 sz);

#endif
