#ifndef LCOLONQ_PIT_UTILS_H
#define LCOLONQ_PIT_UTILS_H

#include <stdarg.h>
#include <lcq/pit/types.h>

/* macro helpers */
#define PIT_CONCAT(a, b) a ## b
#define PIT_STRSTR(x) #x
#define PIT_STR(x) PIT_STRSTR(x)

/* implementations of needed libc functions */
/* ctype */
static inline bool pit_libc_ctype_isdigit(int a) { return a >= '0' && a <= '9'; }
static inline bool pit_libc_ctype_islower(int a) { return a >= 'a' && a <= 'z'; }
static inline bool pit_libc_ctype_isupper(int a) { return a >= 'A' && a <= 'Z'; }
static inline bool pit_libc_ctype_isalpha(int a) { return pit_libc_ctype_islower(a) || pit_libc_ctype_isupper(a); }
static inline bool pit_libc_ctype_isprint(int a) { return a >= 0x20 && a <= 0x7f; }
static inline bool pit_libc_ctype_isspace(int a) { return a == ' ' || a == '\r' || a == '\n' || a == '\t'; }

/* string */
static inline size_t pit_libc_string_strlen(char *s) {
    size_t idx = 0;
    while (s[idx] != 0) ++idx;
    return idx;
}
static inline u8 *pit_libc_string_memcpy(u8 *dest, u8 *src, size_t n) {
    size_t i = 0;
    for (; i < n; ++i) dest[i] = src[i];
    return dest;
}
int pit_libc_string_vsnprintf(char *str, size_t size, char *format, va_list ap);
int pit_libc_string_snprintf(char *buf, size_t len, char *format, ...);

/* assorted utilities and debugging tools */
#define pit_mul(result, a, b) *result = (i64) (a) * (i64) (b)

#endif
