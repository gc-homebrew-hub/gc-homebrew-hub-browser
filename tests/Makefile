# Host-side tests for the portable modules (JSON, SHA-256, catalog, file
# helpers, zip, .tar.xz). No devkitPPC needed: make test from the top level.
CC      ?= cc
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=undefined -I../source -Istub
SRC     := ../source/json.c ../source/digest.c ../source/catalog.c ../source/fsutil.c ../source/tarxz.c ../source/log.c ../source/memstat.c ../source/zipx.c test.c
# liblzma for tarxz.c (on macOS from Homebrew: brew install xz)
XZ      := $(firstword $(wildcard /opt/homebrew/opt/xz /usr/local/opt/xz))
LZMA    := $(if $(XZ),-I$(XZ)/include -L$(XZ)/lib) -llzma
BIN     := build/hbtest

test: $(BIN)
	@rm -rf run && mkdir -p run && cd run && ../$(BIN)

$(BIN): $(SRC) ../source/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LZMA) -lz

clean:
	rm -rf build run

.PHONY: test clean
