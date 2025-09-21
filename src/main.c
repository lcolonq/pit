#include <stdio.h>

#include "utils.h"
#include "lexer.h"
#include "parser.h"
#include "runtime.h"

pit_value test_print(pit_runtime *rt, pit_value args) {
    pit_value x = pit_car(rt, args);
    pit_trace(rt, x);
    return x;
}

pit_value test_if(pit_runtime *rt, pit_value args) {
    pit_value cform = pit_car(rt, args);
    pit_value tform = pit_car(rt, pit_cdr(rt, args));
    pit_value eform = pit_car(rt, pit_cdr(rt, pit_cdr(rt, args)));
    pit_value c = pit_eval(rt, cform);
    if (c != PIT_NIL) {
        return pit_eval(rt, tform);
    } else {
        return pit_eval(rt, eform);
    }
}

pit_value test_add(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_integer_new(rt, x + y);
}

pit_value test_sub(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_integer_new(rt, x - y);
}

int main(int argc, char **argv) {
    if (argc < 2) pit_panic("usage: %s FILE", argv[0]);

    pit_runtime *rt = pit_runtime_new();
    pit_check_error_maybe_panic(rt);
    pit_fset(rt, pit_intern_cstr(rt, "print"), pit_nativefunc_new(rt, test_print));
    pit_fset(rt, pit_intern_cstr(rt, "+"), pit_nativefunc_new(rt, test_add));
    pit_fset(rt, pit_intern_cstr(rt, "-"), pit_nativefunc_new(rt, test_sub));
    pit_mset(rt, pit_intern_cstr(rt, "if"), pit_nativefunc_new(rt, test_if));

    pit_lexer *lex = pit_lex_file(argv[1]);

    pit_parser *parse = pit_parser_from_lexer(lex);
    pit_value program = pit_parse(rt, parse);
    pit_check_error_maybe_panic(rt);
    pit_trace(rt, program);

    pit_value ret = pit_eval(rt, program);
    pit_check_error_maybe_panic(rt);
    pit_trace(rt, ret);
}
