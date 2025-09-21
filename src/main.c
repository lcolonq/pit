#include <stdio.h>

#include "utils.h"
#include "lexer.h"
#include "parser.h"
#include "runtime.h"

pit_value test_add(pit_runtime *rt, pit_value args) {
    i64 x = pit_as_integer(rt, pit_car(rt, args));
    i64 y = pit_as_integer(rt, pit_car(rt, pit_cdr(rt, args)));
    return pit_integer_new(rt, x + y);
}

int main(int argc, char **argv) {
    if (argc < 2) pit_panic("usage: %s FILE", argv[0]);
    pit_lexer *lex = pit_lex_file(argv[1]);
    /* pit_lex_token t; */
    /* while ((t = pit_lex_next(lex)) > PIT_LEX_TOKEN_EOF) { */
    /*     printf("%s ", pit_lex_token_name(t)); */
    /* } */
    /* puts(pit_lex_token_name(t)); */

    pit_runtime *rt = pit_runtime_new();
    pit_check_error_maybe_panic(rt);
    // pit_fset(rt, pit_intern_cstr(rt, "add"), pit_nativefunc_new(rt, test_add));
    // pit_trace(rt, pit_list(rt, 2, pit_integer_new(rt, 1), pit_integer_new(rt, 2)));
    // pit_trace(rt, pit_cons(rt, pit_cons(rt, pit_integer_new(rt, 1), pit_bytes_new_cstr(rt, "foobarbaz")), pit_cons(rt, pit_integer_new(rt, 2), pit_integer_new(rt, 3))));
    // pit_check_error_maybe_panic(rt);
    // pit_value res = pit_apply(rt, pit_fget(rt, pit_intern_cstr(rt, "add")), pit_list(rt, 2, pit_integer_new(rt, 1), pit_integer_new(rt, 2)));
    // pit_check_error_maybe_panic(rt);
    // pit_trace(rt, res);

    pit_parser *parse = pit_parser_from_lexer(lex);
    pit_value v = pit_parse(rt, parse);
    pit_check_error_maybe_panic(rt);
    pit_trace(rt, v);
}
