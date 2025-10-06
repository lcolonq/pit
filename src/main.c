#include <stdlib.h>
#include <stdio.h>

#include "utils.h"
#include "lexer.h"
#include "parser.h"
#include "runtime.h"

int main(int argc, char **argv) {
    pit_runtime *rt = pit_runtime_new();
    if (argc < 2) { /* run repl */
        char buf[1024] = {0};
        i64 len = 0;
        pit_runtime_freeze(rt);
        setbuf(stdout, NULL);
        printf("> ");
        while (len < (i64) sizeof(buf) && (buf[len++] = (char) getchar()) != EOF) {
            if (buf[len - 1] == '\n') {
                pit_value bs, prog, res;
                buf[len - 1] = 0;
                bs = pit_bytes_new_cstr(rt, buf);
                prog = pit_read_bytes(rt, bs);
                res = pit_eval(rt, prog);
                if (pit_runtime_print_error(rt)) {
                    rt->error = PIT_NIL;
                    printf("> ");
                } else {
                    char dumpbuf[1024] = {0};
                    pit_dump(rt, dumpbuf, sizeof(dumpbuf) - 1, res, true);
                    printf("%s\n> ", dumpbuf);
                }
                len = 0;
            }
        }
    } else { /* run file */
        pit_value bs = pit_bytes_new_file(rt, argv[1]);
        pit_lexer lex;
        pit_parser parse;
        bool eof = false;
        pit_value p = PIT_NIL;
        if (!pit_lexer_from_bytes(rt, &lex, bs)) {
            pit_error(rt, "failed to initialize lexer");
        }
        pit_parser_from_lexer(&parse, &lex);
        while (p = pit_parse(rt, &parse, &eof), !eof) {
            pit_eval(rt, p);
            if (pit_runtime_print_error(rt)) {
                exit(1);
            }
        }
    }
    return 0;
}
