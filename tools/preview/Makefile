# Renders the program's screens to PNG files on a PC (no GameCube, no
# devkitPPC): "make preview" from the top level. Needs a C compiler and
# Python 3 with Pillow (to save the PNG files). The text is the program's own
# baked font, read from its GX texture format, so it looks as on the console.
CC      ?= cc
CFLAGS  := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Istub -I. -I../../source \
           -DAPP_VERSION='"preview"'
APP     := $(addprefix ../../source/,video.c font.c font_data.c ui.c menu.c screens.c osk.c catalog.c json.c fsutil.c settings.c)
SRC     := $(APP) gx_soft.c preview.c
BIN     := build/preview

preview: $(BIN)
	@rm -rf out && mkdir -p out/sd: && cd out && ../$(BIN) > /dev/null
	@python3 -c "import glob, os; from PIL import Image; \
[(Image.open(p).save(p[:-4] + '.png'), os.remove(p)) for p in sorted(glob.glob('out/*.ppm'))]"
	@rm -rf out/sd:
	@echo "tools/preview/out/ has the screens"

$(BIN): $(SRC) ../../source/*.h stub/*.h gx_soft.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -lm -o $@

clean:
	rm -rf build out

.PHONY: preview clean
