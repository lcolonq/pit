#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "utils.h"

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
