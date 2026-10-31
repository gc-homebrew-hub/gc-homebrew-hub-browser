# Host-side tests for the portable modules (JSON, SHA-256, catalog, file
# helpers, .tar.xz). No devkitPPC needed: make test from the top level.
CC      ?= cc
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=undefined -I../source -Istub
SRC     := ../source/json.c ../source/digest.c ../source/catalog.c ../source/fsutil.c ../source/tarxz.c test.c
# liblzma for tarxz.c (on macOS from Homebrew: brew install xz)
XZ      := $(firstword $(wildcard /opt/homebrew/opt/xz /usr/local/opt/xz))
LZMA    := $(if $(XZ),-I$(XZ)/include -L$(XZ)/lib) -llzma
# libzip, when there is one, for the zip checks (CI installs libzip-dev)
ZIPLIB  := $(firstword $(wildcard /usr/include/zip.h /opt/homebrew/opt/libzip/include/zip.h))
ifneq ($(ZIPLIB),)
SRC     += ../source/zipx.c
LZMA    += -DHAVE_LIBZIP $(if $(findstring homebrew,$(ZIPLIB)),-I/opt/homebrew/opt/libzip/include -L/opt/homebrew/opt/libzip/lib) -lzip
endif
BIN     := build/hbtest

test: $(BIN)
	@rm -rf run && mkdir -p run && cd run && ../$(BIN)

$(BIN): $(SRC) ../source/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LZMA)

clean:
	rm -rf build run

.PHONY: test clean
