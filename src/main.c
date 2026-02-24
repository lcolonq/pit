#include <stdlib.h>
#include <stdio.h>

#include <lcq/pit/utils.h>
#include <lcq/pit/lexer.h>
#include <lcq/pit/parser.h>
#include <lcq/pit/runtime.h>
#include <lcq/pit/library.h>

int main(int argc, char **argv) {
    pit_runtime *rt = pit_runtime_new();
    pit_install_library_essential(rt);
    pit_install_library_io(rt);
    pit_install_library_plist(rt);
    pit_install_library_alist(rt);
    pit_install_library_bytestring(rt);
    PIT_DEFAULT_MAIN(rt);
}
