/* =========================================================================
 * mkdt_sample.c - genera un Apple Device Tree de PRUEBA en formato binario,
 * para validar el parser sin necesitar un IPSW real todavia.
 *
 * Reproduce la estructura: [n_props][n_children][props...][children...]
 * con propiedades padded a 4 bytes y nombres de 32 bytes.
 *
 * Arbol generado:
 *   root { name="device-tree", model="iPhoneXX"
 *     chosen { name="chosen", debug-enabled=<1> }
 *     arm-io { name="arm-io", ranges=[...8 bytes...] }
 *   }
 * ========================================================================= */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static FILE *g;

static void w32(uint32_t v) { fwrite(&v, 4, 1, g); }

/* escribe una propiedad: name[32], length(u32), data padded a 4 */
static void wprop(const char *name, const void *data, uint32_t len) {
    char nm[32]; memset(nm, 0, sizeof(nm));
    strncpy(nm, name, sizeof(nm) - 1);
    fwrite(nm, 1, 32, g);
    w32(len);
    if (len) fwrite(data, 1, len, g);
    uint32_t pad = ((len + 3u) & ~3u) - len;
    while (pad--) fputc(0, g);
}

static void wprop_str(const char *name, const char *s) {
    wprop(name, s, (uint32_t)strlen(s) + 1);
}
static void wprop_u32(const char *name, uint32_t v) {
    wprop(name, &v, 4);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "sample_dt.bin";
    g = fopen(out, "wb");
    if (!g) { perror("fopen"); return 1; }

    /* ---- root: 2 props, 2 children ---- */
    w32(2); w32(2);
    wprop_str("name",  "device-tree");
    wprop_str("model", "iPhoneXX");

    /* ---- child 1: chosen: 2 props, 0 children ---- */
    w32(2); w32(0);
    wprop_str("name", "chosen");
    wprop_u32("debug-enabled", 1);

    /* ---- child 2: arm-io: 2 props, 0 children ---- */
    w32(2); w32(0);
    wprop_str("name", "arm-io");
    uint8_t ranges[8] = { 0x00,0x00,0x00,0x02, 0x00,0x00,0x10,0x00 };
    wprop("ranges", ranges, sizeof(ranges));

    fclose(g);
    printf("[+] Device Tree de prueba escrito en %s\n", out);
    return 0;
}
