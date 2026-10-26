#---------------------------------------------------------------------------------
# GC Homebrew Hub Browser - a homebrew app store for the GameCube. Builds boot.dol.
# Needs devkitPPC + libogc2 (the Docker image in README.md has everything) and
# mbedTLS 3.6 in third_party/mbedtls (run ./get-deps.sh once).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq (,$(filter test package clean,$(MAKECMDGOALS)))
ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC is not set. Use the Docker command in README.md")
endif
include $(DEVKITPRO)/libogc2/gamecube_rules
endif

MBEDTLS		:=	third_party/mbedtls

VERSION		:=	0.1
TARGET		:=	boot
BUILD		:=	build
SOURCES		:=	source $(MBEDTLS)/library
DATA		:=
INCLUDES	:=	source

ifeq (,$(filter test package clean,$(MAKECMDGOALS)))
ifeq ($(BUILD),$(notdir $(CURDIR)))
else
ifeq ($(wildcard $(MBEDTLS)/library/ssl_tls.c),)
$(error "mbedTLS not found in $(MBEDTLS). Run ./get-deps.sh first")
endif
endif
endif


CFLAGS	=	-g -O2 -Wall -std=gnu11 -ffunction-sections -fdata-sections $(MACHDEP) $(INCLUDE) -DAPP_VERSION='"$(VERSION)"' \
		-DMBEDTLS_USER_CONFIG_FILE='"hb_mbedtls_config.h"'
CXXFLAGS	=	$(CFLAGS)
ASFLAGS	=	-g $(MACHDEP)
LDFLAGS	=	-g $(MACHDEP) -Wl,--gc-sections -Wl,-Map,$(notdir $@).map

# libzip pulls in zlib, bzip2, lzma and zstd; libfat and libbba go before libogc
LIBS	:=	-lzip -lz -lbz2 -llzma -lzstd -lfat -lbba -logc -lm
LIBDIRS	:=	$(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD	:=	$(CC)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES	:=	$(CFILES:.c=.o) $(SFILES:.S=.o)
export OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
			-I$(CURDIR)/source \
			-I$(CURDIR)/$(MBEDTLS)/include \
			-I$(CURDIR)/$(MBEDTLS)/library \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) \
			-I$(LIBOGC_INC)

export LIBPATHS	:=	-L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean test package

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol package

# "make test" builds and runs the host-side tests (no devkitPPC needed)
test:
	@$(MAKE) --no-print-directory -C tests

# What goes on the SD card: package/apps/gc-homebrew-hub-browser/ (build first)
package:
	@test -f $(TARGET).dol || { echo "Build $(TARGET).dol first (see README.md)"; exit 1; }
	@rm -rf package && mkdir -p package/apps/gc-homebrew-hub-browser
	@cp $(TARGET).dol package/apps/gc-homebrew-hub-browser/boot.dol
	@cp data/cacert.pem package/apps/gc-homebrew-hub-browser/cacert.pem
	@echo "package/ is ready: copy its apps folder to the root of the SD card"

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

-include $(DEPENDS)

endif
