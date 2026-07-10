#include <lcq/pit/runtime/dump.h>

typedef bool (*pit_dump_callback)(char *buf, i64 len, void *data);
i64 pit_dump_with_callback(
    pit_runtime *rt, char *start, i64 buf_len, pit_value top, bool readable,
    pit_dump_callback cb, void *data
) {
    i64 traversal_reset = rt->traversal->next;
    char *buf = start;
    char *end = start + buf_len;
    pit_value_heavy *h = NULL;
    pit_traversal_push_value(rt, rt->traversal, top);
    while (rt->traversal->next > traversal_reset) {
        pit_traversal_entry ent;
        i64 len = (i64) (end - buf);
        if (rt->error != PIT_NIL) goto end;
        if (pit_vec_pop(pit_traversal_entry)(rt->traversal, &ent) < 0)
            pit_error(rt, "dump stack underflow");
        if (rt->error != PIT_NIL) goto end;
        switch (ent.sort) {
        case PIT_TRAVERSAL_ENTRY_DUMP_STRING: {
            for (char *s = ent.in.dump_string; *s != 0 && buf < end; ++s) *(buf++) = *s;
            break;
        }
        case PIT_TRAVERSAL_ENTRY_VALUE: {
            pit_value v = ent.in.value;
            switch (pit_value_sort(v)) {
            case PIT_VALUE_SORT_DOUBLE:
#ifndef PIT_NO_DOUBLE
                buf += pit_libc_string_snprintf(buf, (size_t) len, "%lf", pit_value_as_double(rt, v));
#else
                buf += pit_string_snprintf(buf, (size_t) len, "<unsupported double>");
#endif
                break;
            case PIT_VALUE_SORT_INTEGER:
                buf += pit_libc_string_snprintf(buf, (size_t) len, "%ld", pit_value_as_integer(rt, v));
                break;
            case PIT_VALUE_SORT_SYMBOL: {
                pit_symtab_entry *se = pit_symtab_lookup(rt, v);
                if (se
                    && pit_value_sort(se->name) == PIT_VALUE_SORT_REF
                    && (h = pit_value_ref_deref(rt, pit_value_as_ref(rt, se->name)))
                ) {
                    i64 i = 0;
                    for (; i < h->in.bytes.len && i < len - 1; ++i) {
                        buf[i] = (char) h->in.bytes.data[i];
                    }
                    buf += i;
                } else {
                    buf += pit_libc_string_snprintf(buf, (size_t) len, "<broken symbol %ld>", pit_value_as_symbol(rt, v));
                }
                break;
            }
            case PIT_VALUE_SORT_REF: {
                pit_ref r = pit_value_as_ref(rt, v);
                h = pit_value_ref_deref(rt, r);
                if (!h) pit_libc_string_snprintf(buf, (size_t) len, "<ref %ld>", r);
                else {
                    switch (h->hsort) {
                    case PIT_VALUE_HEAVY_SORT_CELL: {
                        pit_traversal_push_dump_string(rt, rt->traversal, "}");
                        pit_traversal_push_value(rt, rt->traversal, h->in.cell);
                        pit_traversal_push_dump_string(rt, rt->traversal, "{");
                        break;
                    }
                    case PIT_VALUE_HEAVY_SORT_CONS: {
                        i64 expr_stack_reset = rt->expr_stack->next;
                        pit_value xs = v;
                        bool first = true;
                        while (xs != PIT_NIL && pit_value_is_cons(rt, xs)) {
                            if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons_car(rt, xs)) < 0) {
                                pit_error(rt, "dump expr stack overflow");
                                goto end;
                            }
                            xs = pit_value_cons_cdr(rt, xs);
                        }
                        pit_traversal_push_dump_string(rt, rt->traversal, ")");
                        if (xs != PIT_NIL) {
                            pit_traversal_push_value(rt, rt->traversal, xs);
                            pit_traversal_push_dump_string(rt, rt->traversal, " . ");
                        }
                        while (rt->expr_stack->next > expr_stack_reset) {
                            if (first) first = false;
                            else pit_traversal_push_dump_string(rt, rt->traversal, " ");
                            pit_value x = PIT_NIL;
                            if (pit_vec_pop(pit_value)(rt->expr_stack, &x) < 0) {
                                pit_error(rt, "dump expr stack underflow");
                                goto end;
                            }
                            pit_traversal_push_value(rt, rt->traversal, x);
                        }
                        pit_traversal_push_dump_string(rt, rt->traversal, "(");
                        rt->expr_stack->next = expr_stack_reset;
                        break;
                    }
                    case PIT_VALUE_HEAVY_SORT_ARRAY: {
                        bool first = true;
                        pit_traversal_push_dump_string(rt, rt->traversal, "]");
                        for (i64 i = h->in.array.len - 1; i >= 0; --i) {
                            if (first) first = false;
                            else pit_traversal_push_dump_string(rt, rt->traversal, " ");
                            pit_traversal_push_value(rt, rt->traversal, h->in.array.data[i]);
                        }
                        pit_traversal_push_dump_string(rt, rt->traversal, "[");
                        break;
                    }
                    case PIT_VALUE_HEAVY_SORT_BYTES: {
                        i64 i = 0;
                        if (readable) { buf[i++] = '"'; }
                        i64 maxlen = len - i;
                        for (i64 j = 0; i < maxlen && j < h->in.bytes.len;) {
                            if (buf[i - 1] != '\\' && (h->in.bytes.data[j] == '\\' || h->in.bytes.data[j] == '"')) {
                                buf[i++] = '\\';
                            }
                            else {
                                buf[i++] = (char) h->in.bytes.data[j++];
                            }
                        }
                        if (readable && i < len - 1) buf[i++] = '"';
                        buf += i;
                        break;
                    }
                    default:
                        buf += pit_libc_string_snprintf(buf, (size_t) len, "<ref %ld>", r);
                    }
                }
                break;
            }
            }
            break;
        }
        default:
            pit_error(rt, "unexpected traversal entry"); goto end;
        }
    }
end:
    rt->traversal->next = traversal_reset;
    return (i64) (buf - start);
}

i64 pit_dump(pit_runtime *rt, char *buf, i64 len, pit_value v, bool readable) {
    return pit_dump_with_callback(rt, buf, len, v, readable, NULL, NULL);
}
