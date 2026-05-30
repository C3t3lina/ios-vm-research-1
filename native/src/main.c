/* =========================================================================
 * main.c - CLI del parser de Device Tree de Apple.
 *
 *   dt-parse <device_tree.bin>            -> dump completo
 *   dt-parse <device_tree.bin> <path>     -> dump de un subnodo
 *
 * Ejemplos de paths utiles para diagnostico de boot:
 *   /chosen            (config de arranque que iBoot pasa al kernel)
 *   /arm-io            (mapeo de perifericos del SoC: causa muchos panics)
 *   /cpus
 * ========================================================================= */
#include "apple_dt.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <device_tree.bin> [/path/al/nodo]\n", argv[0]);
        return 2;
    }

    size_t len = 0;
    uint8_t *buf = slurp(argv[1], &len);
    if (!buf) { fprintf(stderr, "No se pudo leer %s\n", argv[1]); return 1; }
    printf("[*] Leidos %zu bytes de %s\n", len, argv[1]);

    size_t consumed = 0;
    dt_node *root = dt_parse(buf, len, &consumed);
    if (!root) {
        fprintf(stderr, "[!] Fallo al parsear. Es un Apple DeviceTree crudo?\n");
        fprintf(stderr, "    (recuerda: debe estar ya extraido del .im4p)\n");
        free(buf);
        return 1;
    }
    printf("[+] Parseado OK. Bytes consumidos: %zu / %zu\n\n", consumed, len);

    if (argc >= 3) {
        const dt_node *n = dt_find(root, argv[2]);
        if (!n) { fprintf(stderr, "[!] Nodo no encontrado: %s\n", argv[2]); }
        else    { dt_dump(n); }
    } else {
        dt_dump(root);
    }

    dt_free(root);
    free(buf);
    return 0;
}
