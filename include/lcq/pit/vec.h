#ifndef LCOLONQ_PIT_VEC_H
#define LCOLONQ_PIT_VEC_H

#include <lcq/pit/types.h>
#include <lcq/pit/utils.h>

#define pit_vec(ty) pit_vec__ ## ty ## __type
#define pit_vec_new(ty) pit_vec__ ## ty ## __new
#define pit_vec_reset(ty) pit_vec__ ## ty ## __reset
#define pit_vec_get(ty) pit_vec__ ## ty ## __get
#define pit_vec_push(ty) pit_vec__ ## ty ## __push
#define pit_vec_pop(ty) pit_vec__ ## ty ## __pop

#define PIT_DECLARE_VEC(ty) \
    typedef struct { \
        i64 capacity, next; \
        u8 data[]; \
    } pit_vec(ty); \
    static __attribute__ ((unused)) pit_vec(ty) *pit_vec_new(ty)(u8 *buf, i64 buf_len) { \
        uintptr_t base = (uintptr_t) buf; \
        uintptr_t aligned = pit_align_up(base, sizeof(void *)); \
        pit_vec(ty) *ret = (pit_vec(ty) *) aligned; \
        uintptr_t data = aligned + (i64) sizeof(pit_vec(ty)); \
        i64 offset = (i64) data - (i64) base; \
        i64 remaining = (i64) (buf_len - offset); \
        ret->next = 0; \
        ret->capacity = remaining; \
        if ((ret->next + 1) * (i64) sizeof(ty) > ret->capacity) return NULL; \
        return ret; \
    } \
    static __attribute__ ((unused)) void pit_vec_reset(ty)(pit_vec(ty) *s) { \
        s->next = 0; \
    } \
    static __attribute__ ((unused)) ty *pit_vec_get(ty)(pit_vec(ty) *s, i64 i) { \
        i64 idx = i * (i64) sizeof(ty); \
        if (idx + (i64) sizeof(ty) > s->capacity) return NULL; \
        return (ty *) &s->data[idx]; \
    } \
    static __attribute__ ((unused)) i64 pit_vec_push(ty)(pit_vec(ty) *s, ty x) { \
        i64 idx = s->next++ * (i64) sizeof(ty); \
        if (idx + (i64) sizeof(ty) > s->capacity) { return -1; } \
        *((ty *) &s->data[idx]) = x; \
        return s->next - 1; \
    } \
    static __attribute__ ((unused)) i64 pit_vec_pop(ty)(pit_vec(ty) *s, ty *v) { \
        i64 idx = (s->next - 1) * (i64) sizeof(ty); \
        if (s->next == 0 || idx + (i64) sizeof(ty) > s->capacity) return -1; \
        *v = *((ty *) &s->data[idx]); \
        return --s->next; \
    }

#endif
