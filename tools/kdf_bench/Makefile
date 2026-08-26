# Break-in effort Phase 5 — Argon2id cost benchmark (standalone). Built
# against the vendored Monocypher. NOT part of the main osv build or CI;
# measures the per-guess KDF cost for the cold-attack estimate (README.md).
MONO     := ../../vendor/monocypher/src/monocypher.c
MONO_INC := ../../vendor/monocypher/src
CC       ?= cc
CFLAGS   ?= -std=c17 -O2 -Wall -Wextra

all: kdf_bench

kdf_bench: kdf_bench.c $(MONO)
	$(CC) $(CFLAGS) -I$(MONO_INC) kdf_bench.c $(MONO) -o $@

clean:
	rm -f kdf_bench

.PHONY: all clean
