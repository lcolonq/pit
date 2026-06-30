#include <lcq/pit/runtime/dump.h>

#define CHECK_BUF if (buf >= end) { return buf - start; }
#define CHECK_BUF_LABEL(label) if (buf >= end) { goto label; }
i64 pit_dump(pit_runtime *rt, char *buf, i64 len, pit_value v, bool readable) {
    pit_value_heavy *h = NULL;
    if (len <= 0) return 0;
    switch (pit_value_sort(v)) {
    case PIT_VALUE_SORT_DOUBLE:
        #ifndef PIT_NO_DOUBLE
        return pit_libc_string_snprintf(buf, (size_t) len, "%lf", pit_value_as_double(rt, v));
        #else
        return pit_string_snprintf(buf, (size_t) len, "<unsupported double>");
        #endif
    case PIT_VALUE_SORT_INTEGER:
        return pit_libc_string_snprintf(buf, (size_t) len, "%ld", pit_value_as_integer(rt, v));
    case PIT_VALUE_SORT_SYMBOL: {
        pit_symtab_entry *ent = pit_symtab_lookup(rt, v);
        if (ent
            && pit_value_sort(ent->name) == PIT_VALUE_SORT_REF
            && (h = pit_value_ref_deref(rt, pit_value_as_ref(rt, ent->name)))
        ) {
            i64 i = 0;
            for (; i < h->in.bytes.len && i < len - 1; ++i) {
                buf[i] = (char) h->in.bytes.data[i];
            }
            return i;
        } else {
            return pit_libc_string_snprintf(buf, (size_t) len, "<broken symbol %ld>", pit_value_as_symbol(rt, v));
        }
    }
    case PIT_VALUE_SORT_REF: {
        pit_ref r = pit_value_as_ref(rt, v);
        char *end = buf + len;
        char *start = buf;
        h = pit_value_ref_deref(rt, r);
        if (!h) pit_libc_string_snprintf(buf, (size_t) len, "<ref %ld>", r);
        else {
            switch (h->hsort) {
            case PIT_VALUE_HEAVY_SORT_CELL: {
                CHECK_BUF; *(buf++) = '{';
                CHECK_BUF; buf += pit_dump(rt, buf, end - buf, pit_value_cons_car(rt, h->in.cell), readable);
                CHECK_BUF; *(buf++) = '}';
                return buf - start;
            }
            case PIT_VALUE_HEAVY_SORT_CONS: {
                pit_value cur = v;
                CHECK_BUF_LABEL(list_end);
                do {
                    if (pit_value_is_cons(rt, cur)) {
                        CHECK_BUF_LABEL(list_end); *(buf++) = ' ';
                        CHECK_BUF_LABEL(list_end); buf += pit_dump(rt, buf, end - buf, pit_value_cons_car(rt, cur), readable);
                    } else {
                        CHECK_BUF_LABEL(list_end); buf += pit_libc_string_snprintf(buf, (size_t) (end - buf), " . ");
                        CHECK_BUF_LABEL(list_end); buf += pit_dump(rt, buf, end - buf, cur, readable);
                    }
                } while (!pit_value_eq((cur = pit_value_cons_cdr(rt, cur)), PIT_NIL));
                CHECK_BUF_LABEL(list_end); *(buf++) = ')';
                list_end:
                *start = '(';
                return buf - start;
            }
            case PIT_VALUE_HEAVY_SORT_ARRAY: {
                i64 i = 0;
                CHECK_BUF_LABEL(array_end);
                if (h->in.array.len == 0) {
                    CHECK_BUF_LABEL(array_end); *(buf++) = '[';
                } else for (; i < h->in.array.len; ++i) {
                    CHECK_BUF_LABEL(array_end); *(buf++) = ' ';
                    CHECK_BUF_LABEL(array_end); buf += pit_dump(rt, buf, end - buf, h->in.array.data[i], readable);
                }
                CHECK_BUF_LABEL(array_end); *(buf++) = ']';
                array_end:
                *start = '[';
                return buf - start;
            }
            case PIT_VALUE_HEAVY_SORT_BYTES: {
                i64 i = 0;
                if (readable) { CHECK_BUF; buf[i++] = '"'; }
                i64 maxlen = len - i;
                for (i64 j = 0; i < maxlen && j < h->in.bytes.len;) {
                    if (buf[i - 1] != '\\' && (h->in.bytes.data[j] == '\\' || h->in.bytes.data[j] == '"')) {
                        CHECK_BUF; buf[i++] = '\\';
                    }
                    else {
                        CHECK_BUF; buf[i++] = (char) h->in.bytes.data[j++];
                    }
                }
                if (readable && i < len - 1) buf[i++] = '"';
                return i;
            }
            default:
                return pit_libc_string_snprintf(buf, (size_t) len, "<ref %ld>", r);
            }
        }
        break;
    }
    }
    return 0;
}
