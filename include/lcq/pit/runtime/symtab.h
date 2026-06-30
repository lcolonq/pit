#ifndef LCOLONQ_PIT_RUNTIME_SYMTAB_H
#define LCOLONQ_PIT_RUNTIME_SYMTAB_H

#include <lcq/pit/runtime.h>

pit_symtab_entry *pit_symtab_lookup(pit_runtime *rt, pit_value sym);
pit_value pit_symtab_intern(pit_runtime *rt, u8 *nm, i64 len);
pit_value pit_symtab_intern_cstr(pit_runtime *rt, char *nm);
pit_value pit_symtab_symbol_name(pit_runtime *rt, pit_value sym);
bool pit_symtab_symbol_name_match(pit_runtime *rt, pit_value sym, u8 *buf, i64 len);
bool pit_symtab_symbol_name_match_cstr(pit_runtime *rt, pit_value sym, char *s);
pit_value pit_symtab_get_value_cell(pit_runtime *rt, pit_value sym);
pit_value pit_symtab_get_function_cell(pit_runtime *rt, pit_value sym);
pit_value pit_symtab_get(pit_runtime *rt, pit_value sym);
void pit_symtab_set(pit_runtime *rt, pit_value sym, pit_value v);
pit_value pit_symtab_fget(pit_runtime *rt, pit_value sym);
void pit_symtab_fset(pit_runtime *rt, pit_value sym, pit_value v);
bool pit_symtab_is_symbol_macro(pit_runtime *rt, pit_value sym);
void pit_symtab_symbol_mark_macro(pit_runtime *rt, pit_value sym);
void pit_symtab_mset(pit_runtime *rt, pit_value sym, pit_value v);
bool pit_symtab_is_symbol_special_form(pit_runtime *rt, pit_value sym);
void pit_symtab_symbol_mark_special_form(pit_runtime *rt, pit_value sym);
void pit_symtab_sfset(pit_runtime *rt, pit_value sym, pit_value v);
void pit_symtab_bind(pit_runtime *rt, pit_value sym, pit_value v);
pit_value pit_symtab_unbind(pit_runtime *rt, pit_value sym);

#endif
