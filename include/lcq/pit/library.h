#ifndef LCOLONQ_PIT_LIBRARY_H
#define LCOLONQ_PIT_LIBRARY_H

#include <lcq/pit/runtime.h>

void pit_install_library_essential(pit_runtime *rt);
void pit_install_library_io(pit_runtime *rt);
void pit_install_library_plist(pit_runtime *rt);
void pit_install_library_bytestring(pit_runtime *rt);

#endif
