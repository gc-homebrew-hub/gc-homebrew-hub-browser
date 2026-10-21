# Host-side tests for the portable modules (JSON, SHA-256, catalog, file
# helpers). No devkitPPC needed: make test from the top level.
CC      ?= cc
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=undefined -I../source -Istub
SRC     := ../source/json.c ../source/digest.c ../source/catalog.c ../source/fsutil.c test.c
BIN     := build/hbtest

test: $(BIN)
	@rm -rf run && mkdir -p run && cd run && ../$(BIN)

$(BIN): $(SRC) ../source/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@

clean:
	rm -rf build run

.PHONY: test clean
