#include <lcq/pit/runtime/eval.h>

pit_value pit_eval(pit_runtime *rt, pit_value top) {
    i64 expr_stack_reset = rt->expr_stack->next;
    i64 result_stack_reset = rt->result_stack->next;
    i64 traversal_reset = rt->traversal->next;
    // pit_vec_reset(pit_annotated_ref)(rt->backtrace);
    if (pit_vec_push(pit_value)(rt->expr_stack, top) < 0)
        pit_error(rt, "evaluation stack overflow");
    /* first, convert the expression tree into "polish notation" in traversal */
    while (rt->expr_stack->next > expr_stack_reset) {
        pit_value cur = PIT_NIL;
        if (rt->error != PIT_NIL) goto end;
        if (pit_vec_pop(pit_value)(rt->expr_stack, &cur) < 0)
            pit_error(rt, "evaluation stack underflow");
        if (pit_value_is_cons(rt, cur)) { /* compound expressions: function/macro application special forms */
            pit_value fsym = pit_value_cons_car(rt, cur);
            bool is_symbol = pit_value_is_symbol(rt, fsym);
            pit_annotated_ref *ann = pit_annotation_get(rt, pit_value_as_ref(rt, cur));
            if (is_symbol && pit_symtab_is_symbol_special_form(rt, fsym)) { /* special forms */
                pit_value f = pit_symtab_fget(rt, fsym);
                pit_value args = pit_value_cons_cdr(rt, cur);
                /* special forms are nativefuncs that directly manipulate the stacks
                   basically macros, but we don't need to evaluate the return value */
                pit_value_apply(rt, f, args);
            } else if (is_symbol && pit_symtab_is_symbol_macro(rt, fsym)) { /* macros */
                pit_value f = pit_symtab_fget(rt, fsym);
                pit_value args = pit_value_cons_cdr(rt, cur);
                pit_value res = pit_value_apply(rt, f, args);
                if (pit_vec_push(pit_value)(rt->expr_stack, res) < 0)
                    pit_error(rt, "evaluation stack overflow");
            } else { /* normal functions */
                pit_value args = pit_value_cons_cdr(rt, cur);
                i64 argcount = 0;
                while (args != PIT_NIL) {
                    if (pit_vec_push(pit_value)(rt->expr_stack, pit_value_cons_car(rt, args)) < 0)
                        pit_error(rt, "evaluation stack overflow");
                    args = pit_value_cons_cdr(rt, args);
                    argcount += 1;
                }
                if (!is_symbol) {
                    if (pit_vec_push(pit_value)(rt->expr_stack, fsym) < 0)
                        pit_error(rt, "evaluation stack overflow");
                }
                pit_traversal_push_application(rt, rt->traversal, argcount, ann);
                if (is_symbol) {
                    pit_value f = pit_symtab_fget(rt, fsym);
                    pit_traversal_push_value(rt, rt->traversal, f);
                }
            }
        } else if (pit_value_is_symbol(rt, cur)) { /* unquoted symbols: variable lookup */
            pit_symtab_entry *ent = pit_symtab_lookup(rt, cur);
            if (ent->is_keyword) {
                pit_traversal_push_value(rt, rt->traversal, cur);
            } else {
                pit_traversal_push_value(rt, rt->traversal, pit_symtab_get(rt, cur));
            }
        } else { /* other expressions evaluate to themselves! */
            pit_traversal_push_value(rt, rt->traversal, cur);
        }
    }
    /* then, execute the polish notation traversal from right to left
       this has the nice consequence of putting the arguments in the right order */
    for (i64 idx = rt->traversal->next - 1; idx >= traversal_reset; --idx) {
        pit_traversal_entry *ent = pit_vec_get(pit_traversal_entry)(rt->traversal, idx);
        if (ent == NULL) pit_error(rt, "evaluation traversal invalid");
        if (rt->error != PIT_NIL) goto end;
        switch (ent->sort) {
        case PIT_TRAVERSAL_ENTRY_VALUE:
            if (pit_vec_push(pit_value)(rt->result_stack, ent->in.value) < 0)
                pit_error(rt, "evaluation result stack overflow");
            break;
        case PIT_TRAVERSAL_ENTRY_APPLICATION: {
            pit_value f = PIT_NIL;
            pit_value args = PIT_NIL;
            if (pit_vec_pop(pit_value)(rt->result_stack, &f) < 0)
                pit_error(rt, "evaluation result stack underflow");
            for (i64 i = 0; i < ent->in.application.arity; ++i) {
                pit_value a = PIT_NIL;
                if (pit_vec_pop(pit_value)(rt->result_stack, &a) < 0)
                    pit_error(rt, "evaluation result stack underflow");
                args = pit_value_cons(rt, a, args);
            }
            if (ent->in.application.annotation != NULL) {
                rt->source_line = ent->in.application.annotation->annotation.line;
                rt->source_column = ent->in.application.annotation->annotation.column;
                pit_vec_push(pit_annotated_ref)(rt->backtrace, *ent->in.application.annotation);
            }
            if (pit_vec_push(pit_value)(rt->result_stack, pit_value_apply(rt, f, args)) < 0)
                pit_error(rt, "evaluation result stack underflow");
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
            pit_error(rt, "evaluation result stack underflow");
        rt->expr_stack->next = expr_stack_reset;
        rt->result_stack->next = result_stack_reset;
        rt->traversal->next = traversal_reset;
        return ret;
    }
}
