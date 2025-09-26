SRCS := src/main.c src/utils.c src/lexer.c src/parser.c src/runtime.c src/library.c
OBJECTS := $(SRCS:src/%.c=build/%.o)
EXE := pit

CC := musl-gcc
CHK_SOURCES ?= $(SRCS)
CPPFLAGS ?= -MMD -MP
CFLAGS ?= -Ideps/ -Isrc/ -Wall -Wextra -Wpedantic -ftrapv --std=c23 -g
LDFLAGS ?= -g -static

.PHONY: all clean check-syntax

all: $(EXE)

$(EXE): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

build:
	mkdir build/

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

clean:
	-rm $(EXE)
	-rm -r build/

TAGS: $(SRCS)
	etags $^

check-syntax: TAGS
	gcc $(CFLAGS) -fsyntax-only $(CHK_SOURCES)

-include $(OBJECTS:.o=.d)
