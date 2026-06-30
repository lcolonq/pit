#include <lcq/pit/runtime/symtab.h>

pit_symtab_entry *pit_symtab_lookup(pit_runtime *rt, pit_value sym) {
    pit_symbol s = pit_value_as_symbol(rt, sym);
    return pit_vec_get(pit_symtab_entry)(rt->symtab, s);
}
pit_value pit_symtab_intern(pit_runtime *rt, u8 *nm, i64 len) {
    if (rt->error != PIT_NIL) return PIT_NIL;
    for (i64 sidx = 0; sidx < rt->symtab->next; ++sidx) {
        pit_symtab_entry *sent = pit_vec_get(pit_symtab_entry)(rt->symtab, sidx);
        if (sent == NULL) { pit_error(rt, "corrupted symbol table"); return PIT_NIL; }
        if (pit_value_bytes_match(rt, sent->name, nm, len)) return pit_value_symbol_new(rt, sidx);
    }
    pit_symtab_entry ent;
    ent.name = pit_value_bytes_new(rt, nm, len);
    ent.value = PIT_NIL;
    ent.function = PIT_NIL;
    ent.is_macro = false;
    ent.is_special_form = false;
    ent.is_keyword = len >= 1 && nm[0] == ':';
    i64 idx = pit_vec_push(pit_symtab_entry)(rt->symtab, ent);
    if (idx < 0) { pit_error(rt, "failed to allocate symtab entry"); return PIT_NIL; }
    return pit_value_symbol_new(rt, idx);
}
pit_value pit_symtab_intern_cstr(pit_runtime *rt, char *nm) {
    return pit_symtab_intern(rt, (u8 *) nm, (i64) pit_libc_string_strlen(nm));
}
pit_value pit_symtab_symbol_name(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return ent->name;
}
bool pit_symtab_symbol_name_match(pit_runtime *rt, pit_value sym, u8 *buf, i64 len) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return pit_value_bytes_match(rt, ent->name, buf, len);
}
bool pit_symtab_symbol_name_match_cstr(pit_runtime *rt, pit_value sym, char *s) {
    return pit_symtab_symbol_name_match(rt, sym, (u8 *) s, (i64) pit_libc_string_strlen(s));
}
pit_value pit_symtab_get_value_cell(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return ent->value;
}
pit_value pit_symtab_get_function_cell(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    return ent->function;
}
pit_value pit_symtab_get(pit_runtime *rt, pit_value sym) {
    return pit_value_cell_get(rt, pit_symtab_get_value_cell(rt, sym), sym);
}
void pit_symtab_set(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symbol idx = pit_value_as_symbol(rt, sym);
    if (idx < rt->frozen_symtab) { pit_error(rt, "attempted to modify frozen symbol"); return; }
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    if (pit_value_sort(ent->value) != PIT_VALUE_SORT_REF) {
        ent->value = pit_value_cell_new(rt, PIT_NIL);
    }
    pit_value_cell_set(rt, ent->value, v, sym);
}
pit_value pit_symtab_fget(pit_runtime *rt, pit_value sym) {
    return pit_value_cell_get(rt, pit_symtab_get_function_cell(rt, sym), sym);
}
void pit_symtab_fset(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symbol idx = pit_value_as_symbol(rt, sym);
    if (idx < rt->frozen_symtab) { pit_error(rt, "attempted to modify frozen symbol"); return; }
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    if (pit_value_sort(ent->function) != PIT_VALUE_SORT_REF) {
        ent->function = pit_value_cell_new(rt, PIT_NIL);
    }
    pit_value_cell_set(rt, ent->function, v, sym);
}
bool pit_symtab_is_symbol_macro(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return false; }
    return ent->is_macro;
}
void pit_symtab_symbol_mark_macro(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    ent->is_macro = true;
}
void pit_symtab_mset(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symtab_fset(rt, sym, v);
    pit_symtab_symbol_mark_macro(rt, sym);
}
bool pit_symtab_is_symbol_special_form(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return false; }
    return ent->is_special_form;
}
void pit_symtab_symbol_mark_special_form(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    ent->is_special_form = true;
}
void pit_symtab_sfset(pit_runtime *rt, pit_value sym, pit_value v) {
    pit_symtab_fset(rt, sym, v);
    pit_symtab_symbol_mark_special_form(rt, sym);
}
void pit_symtab_bind(pit_runtime *rt, pit_value sym, pit_value cell) {
    /* although we cannot set frozen symbols, we can still bind them temporarily - no need to check */
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return; }
    if (pit_vec_push(pit_value)(rt->saved_bindings, ent->value) < 0) pit_error(rt, "binding stack overflow");
    ent->value = cell;
}
pit_value pit_symtab_unbind(pit_runtime *rt, pit_value sym) {
    pit_symtab_entry *ent = pit_symtab_lookup(rt, sym);
    if (!ent) { pit_error(rt, "bad symbol"); return PIT_NIL; }
    pit_value old = ent->value;
    if (pit_vec_pop(pit_value)(rt->saved_bindings, &ent->value) < 0) pit_error(rt, "binding stack underflow");
    return old;
}
