/* =========================================================================
 * tools/dt_patch.c - CLI para aplicar parches rapidos sobre el Device Tree.
 *
 * Uso:
 *   dt-patch <in.bin> <out.bin> --boot-args "-v debug=0x14e keepsyms=1"
 *   dt-patch <in.bin> <out.bin> --set-prop-str <path> <prop> <valor>
 *
 * Usa upsert (crea-o-modifica): boot-args a menudo NO existe en el DT
 * estatico (lo inyecta iBoot en arranque), asi que hay que poder crearlo.
 * ========================================================================= */
#include "apple_dt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void usage(const char *prog) {
    fprintf(stderr, "Uso: %s <in_dt.bin> <out_dt.bin> [opciones]\n", prog);
    fprintf(stderr, "Opciones:\n");
    fprintf(stderr, "  --boot-args \"<args>\"                Crea/modifica /chosen/boot-args\n");
    fprintf(stderr, "  --set-prop-str <path> <prop> <val>  Crea/modifica una prop de texto\n");
}

int main(int argc, char **argv) {
    if (argc < 4) { usage(argv[0]); return 2; }

    const char *in_path  = argv[1];
    const char *out_path = argv[2];
    const char *opt      = argv[3];

    size_t len = 0;
    uint8_t *buf = slurp(in_path, &len);
    if (!buf) { fprintf(stderr, "[!] No se pudo leer: %s\n", in_path); return 1; }

    dt_node *root = dt_parse(buf, len, NULL);
    if (!root) {
        fprintf(stderr, "[!] No se pudo parsear el Device Tree de entrada.\n");
        free(buf); return 1;
    }

    int applied = 0;

    if (strcmp(opt, "--boot-args") == 0) {
        if (argc < 5) { fprintf(stderr, "[!] --boot-args requiere una cadena.\n"); goto done; }
        const char *val = argv[4];
        dt_node *chosen = (dt_node *)dt_find(root, "/chosen");
        if (!chosen) {
            fprintf(stderr, "[!] No existe el nodo /chosen.\n");
        } else {
            int existed = dt_get_prop(chosen, "boot-args") != NULL;
            if (dt_upsert_prop_str(chosen, "boot-args", val) == 0) {
                printf("[+] /chosen/boot-args %s = \"%s\"\n",
                       existed ? "modificado" : "creado", val);
                applied = 1;
            } else {
                fprintf(stderr, "[!] Fallo al inyectar boot-args.\n");
            }
        }
    }
    else if (strcmp(opt, "--set-prop-str") == 0) {
        if (argc < 7) {
            fprintf(stderr, "[!] --set-prop-str requiere <path> <prop> <valor>.\n");
            goto done;
        }
        const char *path = argv[4], *prop = argv[5], *val = argv[6];
        dt_node *node = (dt_node *)dt_find(root, path);
        if (!node) {
            fprintf(stderr, "[!] Nodo no encontrado: %s\n", path);
        } else {
            int existed = dt_get_prop(node, prop) != NULL;
            if (dt_upsert_prop_str(node, prop, val) == 0) {
                printf("[+] %s/%s %s = \"%s\"\n",
                       path, prop, existed ? "modificado" : "creado", val);
                applied = 1;
            } else {
                fprintf(stderr, "[!] Fallo al mutar '%s' en '%s'.\n", prop, path);
            }
        }
    }
    else {
        fprintf(stderr, "[!] Opcion desconocida: %s\n", opt);
        usage(argv[0]);
    }

    if (applied) {
        if (dt_write(root, out_path) == 0)
            printf("[OK] Device Tree guardado en: %s\n", out_path);
        else {
            fprintf(stderr, "[!] Error al escribir %s\n", out_path);
            applied = 0;
        }
    }

done:
    dt_free(root);
    free(buf);
    return applied ? 0 : 1;
}
