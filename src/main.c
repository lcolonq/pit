#include <stdlib.h>
#include <stdio.h>

#include <lcq/pit/utils.h>
#include <lcq/pit/lexer.h>
#include <lcq/pit/parser.h>
#include <lcq/pit/runtime.h>
#include <lcq/pit/library.h>

int main(int argc, char **argv) {
    i64 sz = 256 * 1024 * 1024;
    u8 *buf = malloc((size_t) sz);
    pit_runtime *rt = pit_runtime_new(buf, sz);
    pit_install_library_essential(rt);
    pit_install_library_io(rt);
    pit_install_library_plist(rt);
    pit_install_library_alist(rt);
    pit_install_library_bytestring(rt);
    if (argc < 2) {
        pit_repl(rt);
    } else {
        pit_load_file(rt, argv[1]);
        if (pit_runtime_print_error(rt)) return -1;
    }
}
