#include <lcq/pit/runtime/macroexpand.h>

pit_value pit_macroexpand(pit_runtime *rt, pit_value top) {
    i64 expr_stack_reset = rt->expr_stack->next;
    i64 result_stack_reset = rt->result_stack->next;
    i64 traversal_reset = rt->traversal->next;
    if (pit_vec_push(pit_value)(rt->expr_stack, top) < 0)
        pit_error(rt, "macro expansion stack overflow");
    while (rt->expr_stack->next > expr_stack_reset) {
        pit_value cur;
        if (rt->error != PIT_NIL) goto end;
        if (pit_vec_pop(pit_value)(rt->expr_stack, &cur) < 0)
            pit_error(rt, "macro expansion stack underflow");
        if (pit_value_is_cons(rt, cur)) {
            pit_value fsym = pit_value_cons_car(rt, cur);
            bool is_symbol = pit_value_is_symbol(rt, fsym);
            pit_annotated_ref *ann = pit_annotation_get(rt, pit_value_as_ref(rt, cur));
            if (is_symbol && pit_symtab_is_symbol_macro(rt, fsym)) {
                pit_value f = pit_symtab_fget(rt, fsym);
                pit_value args = pit_value_cons_cdr(rt, cur);
                pit_value res = pit_value_apply(rt, f, args);
                if (pit_vec_push(pit_value)(rt->expr_stack, res) < 0)
                    pit_error(rt, "macro expansion stack overflow");
            } else if (is_symbol && pit_symtab_symbol_name_match_cstr(rt, fsym, "defer")) {
                pit_value args = pit_value_cons_cdr(rt, cur);
                pit_traversal_push_value(rt, rt->traversal, pit_value_cons_car(rt, args));
            } else if (is_symbol && pit_symtab_symbol_name_match_cstr(rt, fsym, "quote")) {
                pit_traversal_push_value(rt, rt->traversal, cur);
            } else if (is_symbol && pit_symtab_symbol_name_match_cstr(rt, fsym, "lambda")) {
                pit_value args = pit_value_cons_cdr(rt, cur);
                pit_value bindings = pit_value_cons_car(rt, args);
                pit_value body = pit_value_cons_cdr(rt, args);
                i64 argcount = 0;
                if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_list(rt, 2, pit_symtab_intern_cstr(rt, "defer"), bindings)) < 0)
                    pit_error(rt, "macro expansion stack overflow");
                while (body != PIT_NIL) {
                    pit_value a = pit_value_cons_car(rt, body);
                    if (pit_vec_push(pit_value)(rt->expr_stack, a) < 0)
                        pit_error(rt, "macro expansion stack overflow");
                    body = pit_value_cons_cdr(rt, body);
                    argcount += 1;
                }
                pit_traversal_push_application(rt, rt->traversal, argcount + 1, ann);
                pit_traversal_push_value(rt, rt->traversal, fsym);
            } else {
                pit_value args = pit_value_cons_cdr(rt, cur);
                i64 argcount = 0;
                while (args != PIT_NIL) {
                    pit_value a = pit_value_cons_car(rt, args);
                    if (pit_vec_push(pit_value)(rt->expr_stack, a) < 0)
                        pit_error(rt, "macro expansion stack overflow");
                    args = pit_value_cons_cdr(rt, args);
                    argcount += 1;
                }
                if (!is_symbol) {
                    if (pit_vec_push(pit_value)(rt->expr_stack, fsym) < 0)
                        pit_error(rt, "macro expansion stack overflow");
                }
                pit_traversal_push_application(rt, rt->traversal, argcount, ann);
                if (is_symbol) {
                    pit_traversal_push_value(rt, rt->traversal, fsym);
                }
            }
        } else {
            pit_traversal_push_value(rt, rt->traversal, cur);
        }
    }
    for (i64 idx = rt->traversal->next - 1; idx >= traversal_reset; --idx) {
        pit_traversal_entry *ent = pit_vec_get(pit_traversal_entry)(rt->traversal, idx);
        if (ent == NULL) pit_error(rt, "macro expansion traversal invalid");
        if (rt->error != PIT_NIL) goto end;
        switch (ent->sort) {
        case PIT_TRAVERSAL_ENTRY_VALUE:
            if (pit_vec_push(pit_value)(rt->result_stack, ent->in.value) < 0)
                pit_error(rt, "macro expansion stack overflow");
            break;
        case PIT_TRAVERSAL_ENTRY_APPLICATION: {
            pit_value f = PIT_NIL;
            pit_value args = PIT_NIL;
            pit_value app = PIT_NIL;
            if (pit_vec_pop(pit_value)(rt->result_stack, &f) < 0)
                pit_error(rt, "macro expansion stack underflow");
            for (i64 i = 0; i < ent->in.application.arity; ++i) {
                pit_value a;
                if (pit_vec_pop(pit_value)(rt->result_stack, &a) < 0)
                    pit_error(rt, "macro expansion stack underflow");
                args = pit_value_cons(rt, a, args);
            }
            app = pit_value_cons(rt, f, args);
            if (ent->in.application.annotation != NULL) {
                pit_annotation_set(rt, pit_value_as_ref(rt, app), ent->in.application.annotation->annotation);
            }
            if (pit_vec_push(pit_value)(rt->result_stack, app) < 0)
                pit_error(rt, "macro expansion stack overflow");
            break;
        }
        default:
            pit_error(rt, "unknown traversal entry");
            goto end;
        }
    }
end: {
        pit_value ret = PIT_NIL;
        if (pit_vec_pop(pit_value)(rt->result_stack, &ret) < 0)
            pit_error(rt, "macro expansion stack underflow");
        rt->expr_stack->next = expr_stack_reset;
        rt->result_stack->next = result_stack_reset;
        rt->traversal->next = traversal_reset;
        return ret;
    }
}
