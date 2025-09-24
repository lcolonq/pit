#include <stdio.h>

#include "utils.h"
#include "lexer.h"
#include "parser.h"
#include "runtime.h"

int main(int argc, char **argv) {
    if (argc < 2) pit_panic("usage: %s FILE", argv[0]);

    pit_runtime *rt = pit_runtime_new();
    pit_check_error_maybe_panic(rt);

    pit_lexer *lex = pit_lex_file(argv[1]);

    printf("checking parse...\n");
    pit_parser *parse = pit_parser_from_lexer(lex);
    pit_value program = pit_parse(rt, parse);
    pit_check_error_maybe_panic(rt);
    pit_trace(rt, program);

    printf("checking macro expansion...\n");
    pit_value expanded = pit_expand_macros(rt, program);
    pit_check_error_maybe_panic(rt);
    pit_trace(rt, expanded);

    printf("checking free variables...\n");
    pit_value freevars = pit_free_vars(rt, PIT_NIL, expanded);
    pit_check_error_maybe_panic(rt);
    pit_trace(rt, freevars);

    printf("checking eval...\n");
    pit_value ret = pit_eval(rt, program);
    pit_check_error_maybe_panic(rt);
    pit_trace(rt, ret);
}
