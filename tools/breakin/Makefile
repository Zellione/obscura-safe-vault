# Break-in proof tools (Phase 1) — standalone, built against the vendored
# Monocypher. NOT part of the main osv build or CI; used only to reproduce the
# core-dump break-in (see README.md).
MONO     := ../../vendor/monocypher/src/monocypher.c
MONO_INC := ../../vendor/monocypher/src
CC       ?= cc
CFLAGS   ?= -std=c17 -O2 -Wall -Wextra

all: breakin extract_photo

breakin: breakin.c $(MONO)
	$(CC) $(CFLAGS) -I$(MONO_INC) breakin.c $(MONO) -o $@

extract_photo: extract_photo.c $(MONO)
	$(CC) $(CFLAGS) -I$(MONO_INC) extract_photo.c $(MONO) -o $@

clean:
	rm -f breakin extract_photo index_slot*.bin photo_decrypted.bin

.PHONY: all clean
