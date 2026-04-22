#include <lcq/pit/utils.h>

enum vsnprintf_mode {
    VSNPRINTF_MODE_NORMAL,
    VSNPRINTF_MODE_FLAGS,
    VSNPRINTF_MODE_WIDTH,
    VSNPRINTF_MODE_PRECISION,
    VSNPRINTF_MODE_LENGTH_MOD,
    VSNPRINTF_MODE_CONVERSION_SPEC
};
#define SCRATCH_LEN 256
#define WRITE(c) { buf[idx++] = c; if (idx >= len - 1) goto done; }
#define WRITE_SCRATCH(c) { scratch[sidx++] = c; if (sidx >= SCRATCH_LEN) goto error; }
int pit_string_vsnprintf(char *buf, size_t len, char *format, va_list ap) {
    // vsnprintf
    size_t idx = 0;
    size_t flen = pit_string_strlen(format);
    size_t fidx = 0;
    enum vsnprintf_mode mode = VSNPRINTF_MODE_NORMAL;
    size_t sidx = 0;
    char scratch[SCRATCH_LEN] = {0};
    char length_mod = 0;
    for (; fidx < flen && idx < len - 1; ++fidx) {
        char c = format[fidx];
        sidx = 0;
        switch (mode) {
        case VSNPRINTF_MODE_NORMAL:
            if (c == '%') {
                mode = VSNPRINTF_MODE_FLAGS;
            } else WRITE(c);
            break;
        case VSNPRINTF_MODE_FLAGS:
        case VSNPRINTF_MODE_WIDTH:
        case VSNPRINTF_MODE_PRECISION:
        case VSNPRINTF_MODE_LENGTH_MOD:
            switch (c) {
            case 'l': length_mod = 'l'; mode = VSNPRINTF_MODE_CONVERSION_SPEC; continue;
            }
            // fallthrough
        case VSNPRINTF_MODE_CONVERSION_SPEC:
            switch (c) {
            case '%': WRITE('%'); mode = VSNPRINTF_MODE_NORMAL; break;
            case 'd': {
                long arg = 0;
                if (length_mod == 'l') arg = va_arg(ap, long); else arg = va_arg(ap, int);
                if (arg == 0) { WRITE('0') }
                else {
                    while (arg != 0) { WRITE_SCRATCH('0' + (char) (arg % 10)); arg /= 10; }
                    while (sidx > 0) { WRITE(scratch[sidx - 1]); sidx -= 1; }
                }
                mode = VSNPRINTF_MODE_NORMAL;
                break;
            }
            case 'f': {
                double arg = 0.0;
                if (length_mod == 'l')
                    arg = va_arg(ap, double);
                else
                    arg = va_arg(ap, double);
                long wholepart = (long) arg;
                double fracpart = arg - (double) wholepart;
                if (wholepart == 0) { WRITE('0') }
                else {
                    while (wholepart != 0) { WRITE_SCRATCH('0' + (char) (wholepart % 10)); wholepart /= 10; }
                    while (sidx > 0) { WRITE(scratch[sidx - 1]); sidx -= 1; }
                }
                WRITE('.');
                for (int i = 0; i < 6; ++i) {
                    fracpart *= 10.0;
                    wholepart = (long) fracpart;
                    fracpart -= (double) wholepart;
                    WRITE('0' + (char) wholepart);
                }
                mode = VSNPRINTF_MODE_NORMAL;
                break;
            }
            case 's': {
                char *arg = va_arg(ap, char *);
                while (*arg != 0) { WRITE(*arg); arg += 1; }
                mode = VSNPRINTF_MODE_NORMAL;
                break;
            }
            default: goto error;
            }
            break;
        }
    }
done:
    buf[idx] = 0;
    return (int) idx;
error:
    return -1;
}
int pit_string_snprintf(char *buf, size_t len, char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = pit_string_vsnprintf(buf, len, format, ap);
    va_end(ap);
    return ret;
}

pit_arena *pit_arena_new(u8 *buf, i64 buf_len, i64 elem_size) {
    uintptr_t base = (uintptr_t) buf;
    uintptr_t aligned = pit_align_up(base, sizeof(void *));
    pit_arena *a = (pit_arena *) aligned;
    uintptr_t data = aligned + sizeof(pit_arena);
    i64 offset = (i64) data - (i64) base;
    i64 remaining = (i64) pit_align_down((uintptr_t) (buf_len - offset), sizeof(void *));
    if (!a || remaining <= 0) return NULL;
    a->elem_size = elem_size;
    a->capacity = remaining / elem_size;
    a->next = 0;
    a->back = remaining;
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
