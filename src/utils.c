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
int pit_libc_string_vsnprintf(char *buf, size_t len, char *format, va_list ap) {
    // vsnprintf
    size_t idx = 0;
    size_t flen = pit_libc_string_strlen(format);
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
                    if (arg < 0) { WRITE('-'); arg = -arg; }
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
int pit_libc_string_snprintf(char *buf, size_t len, char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = pit_libc_string_vsnprintf(buf, len, format, ap);
    va_end(ap);
    return ret;
}
