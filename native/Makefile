# Makefile - parser de Apple Device Tree
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g -Iinclude
SRC      = src/apple_dt.c src/main.c
BIN      = dt-parse

# herramienta auxiliar: genera un DT de prueba para validar el parser
GEN_SRC  = tools/mkdt_sample.c
GEN_BIN  = mkdt-sample

# suite de pruebas (mutador / round-trip)
TEST_SRC = src/apple_dt.c tools/test_dt.c
TEST_BIN = test-dt
ASAN     = -fsanitize=address,undefined -fno-omit-frame-pointer

# parcheador CLI (opcion A)
PATCH_SRC = src/apple_dt.c tools/dt_patch.c
PATCH_BIN = dt-patch

# motor de diffs declarativo (opcion B)
APPLY_SRC = src/apple_dt.c tools/dt_apply.c
APPLY_BIN = dt-apply

all: $(BIN) $(GEN_BIN) $(PATCH_BIN) $(APPLY_BIN)

$(BIN): $(SRC) include/apple_dt.h
	$(CC) $(CFLAGS) $(SRC) -o $@

$(GEN_BIN): $(GEN_SRC)
	$(CC) $(CFLAGS) $(GEN_SRC) -o $@

$(PATCH_BIN): $(PATCH_SRC) include/apple_dt.h
	$(CC) $(CFLAGS) $(PATCH_SRC) -o $@

$(APPLY_BIN): $(APPLY_SRC) include/apple_dt.h
	$(CC) $(CFLAGS) $(APPLY_SRC) -o $@

# prueba de humo del parser/dump
test: all
	./$(GEN_BIN) sample_dt.bin
	./$(BIN) sample_dt.bin
	@echo "---- consulta de subnodo /chosen ----"
	./$(BIN) sample_dt.bin /chosen

# suite del mutador con AddressSanitizer + UBSan (caza leaks/UB)
test-mutator: $(GEN_BIN)
	./$(GEN_BIN) sample_dt.bin
	$(CC) $(CFLAGS) $(ASAN) $(TEST_SRC) -o $(TEST_BIN)
	./$(TEST_BIN) sample_dt.bin

# prueba e2e del parcheador: crea boot-args donde NO existia
test-patch: all
	./$(GEN_BIN) sample_dt.bin
	./$(PATCH_BIN) sample_dt.bin patched_dt.bin --boot-args "-v debug=0x14e keepsyms=1"
	@echo "---- verificacion: /chosen del binario parcheado ----"
	./$(BIN) patched_dt.bin /chosen

# prueba del motor de diffs declarativo
test-apply: all
	./$(GEN_BIN) sample_dt.bin
	./$(APPLY_BIN) sample_dt.bin applied_dt.bin ../examples/lab.patches
	@echo "---- /chosen tras aplicar diffs ----"
	./$(BIN) applied_dt.bin /chosen

clean:
	rm -f $(BIN) $(GEN_BIN) $(TEST_BIN) $(PATCH_BIN) $(APPLY_BIN) \
	      sample_dt.bin patched_dt.bin applied_dt.bin

.PHONY: all test test-mutator test-patch test-apply clean
