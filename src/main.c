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
    pit_parser *parse = pit_parser_from_lexer(lex);

    bool eof = false;
    while (!eof) {
        pit_value program = pit_parse(rt, parse, &eof);
        pit_check_error_maybe_panic(rt);
        pit_eval(rt, program);
        pit_check_error_maybe_panic(rt);
    }
}
