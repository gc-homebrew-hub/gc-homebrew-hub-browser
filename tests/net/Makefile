# Host build of net.c + mbedTLS for nettest (see nettest.c). make -C tests/net
CC      ?= cc
MBEDTLS := ../../third_party/mbedtls
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wno-unused-parameter -Istub -I. -I../../source \
           -I$(MBEDTLS)/include -I$(MBEDTLS)/library \
           -DMBEDTLS_USER_CONFIG_FILE='"host_mbedtls_config.h"' -DHB_TLS_DEBUG
LIBSRC  := $(wildcard $(MBEDTLS)/library/*.c)
BIN     := build/nettest

$(BIN): nettest.c ../../source/net.c ../../source/net.h $(LIBSRC)
	@mkdir -p build
	$(CC) $(CFLAGS) nettest.c ../../source/net.c $(LIBSRC) -o $@

clean:
	rm -rf build

.PHONY: clean
