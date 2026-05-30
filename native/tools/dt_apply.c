/* =========================================================================
 * tools/dt_apply.c - Motor de diffs DECLARATIVO para Device Tree (Semana 9).
 *
 * Aplica mutaciones masivas en un solo pase desde un fichero de parches,
 * inspirado en el dtetool de Cylance. Reusa el upsert blindado del nucleo.
 *
 * Uso:
 *   dt-apply <in.bin> <out.bin> <patches.txt>
 *
 * Formato de patches.txt (una directiva por linea, '#' = comentario):
 *   /chosen/boot-args      = "-v debug=0x14e"     # string (con comillas)
 *   /chosen/debug-enabled  = <0x1>                # u32 (entre <>)
 *   /arm-io/ranges         = [00 00 00 02]        # bytes hex (entre [])
 *   /chosen/delete-me      = !delete              # elimina la propiedad
 *
 * El path es <ruta-al-nodo>/<nombre-propiedad>. Se separa por el ultimo '/'.
 * ========================================================================= */
#include "apple_dt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint8_t *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f); *out_len = (size_t)n; return buf;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

/* separa "/a/b/prop" en nodo="/a/b" y prop="prop" (ultimo '/') */
static int split_path(const char *full, char *node_out, size_t ncap,
                      const char **prop_out) {
    const char *slash = strrchr(full, '/');
    if (!slash || slash == full) {
        /* propiedad en la raiz: "/prop" -> nodo="/" */
        if (slash == full) {
            if (ncap < 2) return -1;
            node_out[0] = '/'; node_out[1] = '\0';
            *prop_out = full + 1;
            return 0;
        }
        return -1;
    }
    size_t nlen = (size_t)(slash - full);
    if (nlen >= ncap) return -1;
    memcpy(node_out, full, nlen); node_out[nlen] = '\0';
    *prop_out = slash + 1;
    return 0;
}

/* aplica una directiva. Devuelve 0 OK, -1 error de aplicacion. */
static int apply_directive(dt_node *root, const char *path, const char *value,
                           int line) {
    char node_path[512];
    const char *prop;
    if (split_path(path, node_path, sizeof(node_path), &prop) != 0) {
        fprintf(stderr, "[L%d] path invalido: %s\n", line, path);
        return -1;
    }

    dt_node *node = (dt_node *)dt_find(root, node_path);
    if (!node) {
        fprintf(stderr, "[L%d] nodo no encontrado: %s\n", line, node_path);
        return -1;
    }

    /* --- string: "..." --- */
    if (value[0] == '"') {
        const char *end = strrchr(value, '"');
        if (end == value) { fprintf(stderr, "[L%d] string sin cerrar\n", line); return -1; }
        size_t len = (size_t)(end - value - 1);
        char *s = malloc(len + 1);
        memcpy(s, value + 1, len); s[len] = '\0';
        int r = dt_upsert_prop_str(node, prop, s);
        free(s);
        if (r == 0) printf("  [+] %s = \"%.*s\"\n", path, (int)len, value + 1);
        return r;
    }

    /* --- u32: <0x...> o <decimal> --- */
    if (value[0] == '<') {
        uint32_t v = (uint32_t)strtoul(value + 1, NULL, 0);
        int r = dt_upsert_prop_data(node, prop, (uint8_t *)&v, sizeof(v));
        if (r == 0) printf("  [+] %s = <0x%08x>\n", path, v);
        return r;
    }

    /* --- bytes: [hh hh hh ...] --- */
    if (value[0] == '[') {
        uint8_t buf[4096]; size_t n = 0;
        const char *p = value + 1;
        while (*p && *p != ']' && n < sizeof(buf)) {
            while (*p == ' ' || *p == ',') p++;
            if (!isxdigit((unsigned char)*p)) break;
            buf[n++] = (uint8_t)strtoul(p, (char **)&p, 16);
        }
        int r = dt_upsert_prop_data(node, prop, buf, (uint32_t)n);
        if (r == 0) printf("  [+] %s = [%zu bytes]\n", path, n);
        return r;
    }

    /* --- delete: !delete --- */
    if (strcmp(value, "!delete") == 0) {
        int r = dt_delete_prop(node, prop);
        if (r == 0) printf("  [-] %s eliminado\n", path);
        else fprintf(stderr, "[L%d] no se pudo eliminar %s\n", line, path);
        return r;
    }

    fprintf(stderr, "[L%d] valor no reconocido: %s\n", line, value);
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <in.bin> <out.bin> <patches.txt>\n", argv[0]);
        return 2;
    }
    const char *in = argv[1], *out = argv[2], *patches = argv[3];

    size_t len = 0;
    uint8_t *buf = slurp(in, &len);
    if (!buf) { fprintf(stderr, "[!] no se pudo leer %s\n", in); return 1; }

    dt_node *root = dt_parse(buf, len, NULL);
    if (!root) { fprintf(stderr, "[!] no se pudo parsear %s\n", in); free(buf); return 1; }

    FILE *pf = fopen(patches, "r");
    if (!pf) { perror("fopen patches"); dt_free(root); free(buf); return 1; }

    printf("[*] Aplicando parches de %s ...\n", patches);
    char line[1024];
    int lineno = 0, applied = 0, failed = 0;
    while (fgets(line, sizeof(line), pf)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) { fprintf(stderr, "[L%d] falta '=': %s\n", lineno, s); failed++; continue; }
        *eq = '\0';
        char *path = trim(s);
        char *val  = trim(eq + 1);

        if (apply_directive(root, path, val, lineno) == 0) applied++;
        else failed++;
    }
    fclose(pf);

    printf("[*] %d aplicados, %d fallidos\n", applied, failed);

    if (applied > 0) {
        if (dt_write(root, out) == 0)
            printf("[OK] Device Tree guardado en: %s\n", out);
        else { fprintf(stderr, "[!] error al escribir %s\n", out); failed++; }
    } else {
        fprintf(stderr, "[!] nada aplicado; no se escribe salida.\n");
    }

    dt_free(root);
    free(buf);
    return failed ? 1 : 0;
}
