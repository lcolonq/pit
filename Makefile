EXE ?= pit

CC ?= gcc
AR ?= ar
CHK_SOURCES ?= src/main.c $(SRCS)
CPPFLAGS ?= -MMD -MP
CFLAGS ?= -fPIC --std=c99 -g -Ideps/ -Isrc/ -Iinclude/ -Wall -Wextra -Wpedantic -Wconversion -Wformat-security -Wshadow -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference -Wfloat-equal -Wundef -Wpointer-arith -Wbad-function-cast -Wlogical-op -Wmissing-braces -Wcast-align -Wstrict-overflow=5 -ftrapv
LDFLAGS ?= -g -static

BUILD = build_$(CC)

SRCS_CORE := src/utils.c src/lexer.c src/parser.c src/runtime.c src/library.c
OBJECTS_CORE := $(SRCS_CORE:src/%.c=$(BUILD)/%.o)
LIB_CORE := libcolonq-pit.a
SRCS_NATIVE := src/native.c
OBJECTS_NATIVE := $(SRCS_NATIVE:src/%.c=$(BUILD)/%.o)
LIB_NATIVE := libcolonq-pit-native.a

prefix ?= /usr/local
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin
includedir ?= $(prefix)/include
libdir ?= $(exec_prefix)/lib

.PHONY: all clean install install-bin install-headers install-core install-native check-syntax

all: $(EXE) $(LIB_CORE) $(LIB_NATIVE)

$(EXE): $(BUILD)/main.o $(LIB_NATIVE) $(LIB_CORE)
	$(CC) -o $@ $^ $(LDFLAGS)

$(LIB_CORE): $(OBJECTS_CORE)
	$(AR) rcs $@ $^

$(LIB_NATIVE): $(OBJECTS_NATIVE)
	$(AR) rcs $@ $^

$(BUILD):
	mkdir $(BUILD)/

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

clean:
	-rm $(EXE)
	-rm $(LIB_CORE)
	-rm $(LIB_NATIVE)
	-rm -r $(BUILD)/

TAGS: $(SRCS)
	ctags --output-format=etags $^

install: install-bin install-headers install-core install-native

install-bin: $(EXE)
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(libdir) $(DESTDIR)$(includedir)
	install $(EXE) $(DESTDIR)$(bindir)/$(EXE)

install-headers:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(libdir) $(DESTDIR)$(includedir)
	cp -r include/* $(DESTDIR)$(includedir)

install-core: $(LIB_CORE)
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(libdir) $(DESTDIR)$(includedir)
	install $(LIB_CORE) $(DESTDIR)$(libdir)/$(LIB_CORE)

install-native: $(LIB_NATIVE)
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(libdir) $(DESTDIR)$(includedir)
	install $(LIB_NATIVE) $(DESTDIR)$(libdir)/$(LIB_NATIVE)

check-syntax: TAGS
	gcc $(CFLAGS) -fsyntax-only $(CHK_SOURCES)

-include $(BUILD)/main.d
-include $(OBJECTS_CORE:.o=.d)
-include $(OBJECTS_NATIVE:.o=.d)
