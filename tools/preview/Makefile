# Renders the program's screens to PNG files on a PC (no GameCube, no
# devkitPPC): "make preview" from the top level. Needs a C compiler and
# Python 3 with Pillow; FONT is any TrueType font standing in for the
# console's ROM font.
CC      ?= cc
CFLAGS  := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Istub -I. -I../../source \
           -DAPP_VERSION='"preview"'
FONT    ?= $(firstword $(wildcard /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf \
                                  /Library/Fonts/Arial\ Bold.ttf /System/Library/Fonts/Supplemental/Arial\ Bold.ttf))
APP     := $(addprefix ../../source/,video.c font.c ui.c menu.c screens.c osk.c catalog.c json.c fsutil.c settings.c)
SRC     := $(APP) gx_soft.c preview.c
BIN     := build/preview

preview: $(BIN) build/font.bin
	@rm -rf out && mkdir -p out/sd: && cd out && ../$(BIN) ../build/font.bin > /dev/null
	@python3 -c "import glob, os; from PIL import Image; \
[(Image.open(p).save(p[:-4] + '.png'), os.remove(p)) for p in sorted(glob.glob('out/*.ppm'))]"
	@rm -rf out/sd:
	@echo "tools/preview/out/ has the screens"

$(BIN): $(SRC) ../../source/*.h stub/*.h gx_soft.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -lm -o $@

build/font.bin: font_atlas.py
	@test -n "$(FONT)" || { echo "Set FONT=/path/to/a/font.ttf"; exit 1; }
	@mkdir -p build
	python3 font_atlas.py "$(FONT)" $@

clean:
	rm -rf build out

.PHONY: preview clean
