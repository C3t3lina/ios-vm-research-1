/* =========================================================================
 * test_dt.c - Pruebas del parser + mutador + serializador.
 * Entregable Semana 8. Compilar con -fsanitize=address,undefined para
 * cazar leaks y UB (ver target 'test-mutator' del Makefile).
 *
 * Tests:
 *   T1 round-trip: parse(serialize(parse(x))) reproduce bytes IDENTICOS.
 *   T2 mutacion:   cambiar boot-args en /chosen y verificar re-lectura.
 *   T3 doble mut.: mutar 2 veces la misma prop no fuga (lo valida ASan).
 *   T4 longitud:   serializar a buffer == dry-run size.
 * ========================================================================= */
#include "apple_dt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  \033[1;32mPASS\033[0m %s\n", msg); } \
    else      { printf("  \033[1;31mFAIL\033[0m %s\n", msg); fails++; } \
} while (0)

static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long s = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(s); fread(b, 1, s, f); fclose(f); *n = s; return b;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Uso: %s <sample_dt.bin>\n", argv[0]); return 2; }

    size_t len = 0;
    uint8_t *orig = slurp(argv[1], &len);
    if (!orig) { fprintf(stderr, "no se pudo leer %s\n", argv[1]); return 1; }

    /* ---- T1: round-trip byte-perfect ---- */
    printf("[T1] Round-trip byte-perfect\n");
    dt_node *root = dt_parse(orig, len, NULL);
    CHECK(root != NULL, "parse inicial");

    size_t need = dt_serialize(root, NULL, 0);
    CHECK(need == len, "tamano serializado == original");

    uint8_t *reser = malloc(need);
    size_t wrote = dt_serialize(root, reser, need);
    CHECK(wrote == need, "bytes escritos == tamano");
    CHECK(memcmp(orig, reser, len) == 0, "contenido identico al original");

    /* ---- T4: dry-run == real ---- */
    printf("[T4] dry-run size == real size\n");
    CHECK(dt_serialize(root, NULL, 0) == wrote, "dry-run coincide");

    /* ---- T2: mutacion de boot-args en /chosen ---- */
    printf("[T2] Mutacion de propiedad\n");
    dt_node *chosen = (dt_node *)dt_find(root, "/chosen");
    CHECK(chosen != NULL, "encontrado /chosen");

    /* la muestra trae debug-enabled=<1>; lo mutamos a un valor nuevo */
    uint32_t newval = 0xDEADBEEF;
    int r = dt_set_prop_data(chosen, "debug-enabled",
                             (uint8_t *)&newval, sizeof(newval));
    CHECK(r == 0, "set_prop_data devolvio 0");

    const dt_prop *p = dt_get_prop(chosen, "debug-enabled");
    CHECK(p && p->length == 4 && memcmp(p->data, &newval, 4) == 0,
          "valor mutado se relee correctamente");
    CHECK(p && p->owns_data == 1, "owns_data marcado tras mutar");

    /* ---- T3: mutacion repetida no fuga (ASan lo verifica) ---- */
    printf("[T3] Mutacion repetida (sin leaks)\n");
    for (int i = 0; i < 5; i++) {
        char buf[64]; snprintf(buf, sizeof(buf), "iter-%d", i);
        r = dt_set_prop_str(chosen, "debug-enabled", buf);
    }
    const dt_prop *p2 = dt_get_prop(chosen, "debug-enabled");
    CHECK(p2 && strcmp((const char *)p2->data, "iter-4") == 0,
          "ultima mutacion (string) prevalece");

    /* mutar /chosen cambia el tamano total -> serializa de nuevo sin crash */
    size_t need2 = dt_serialize(root, NULL, 0);
    CHECK(need2 != (size_t)-1, "serializa OK tras mutaciones");

    /* ---- T5: upsert crea boot-args (no existia) y round-trips ---- */
    printf("[T5] Upsert crea propiedad nueva + round-trip\n");
    CHECK(dt_get_prop(chosen, "boot-args") == NULL, "boot-args no existia");
    uint32_t props_before = chosen->n_props;
    int up = dt_upsert_prop_str(chosen, "boot-args", "-v debug=0x14e");
    CHECK(up == 0, "upsert devolvio 0");
    CHECK(chosen->n_props == props_before + 1, "n_props incrementado (coherencia serializador)");

    const dt_prop *ba = dt_get_prop(chosen, "boot-args");
    CHECK(ba && strcmp((const char *)ba->data, "-v debug=0x14e") == 0,
          "boot-args creado y legible");

    /* round-trip del arbol con la prop creada: re-parsear debe encontrarla */
    size_t need3 = dt_serialize(root, NULL, 0);
    uint8_t *bin3 = malloc(need3);
    CHECK(dt_serialize(root, bin3, need3) == need3, "serializa arbol con prop nueva");
    dt_node *root3 = dt_parse(bin3, need3, NULL);
    const dt_node *chosen3 = dt_find(root3, "/chosen");
    const dt_prop *ba3 = chosen3 ? dt_get_prop(chosen3, "boot-args") : NULL;
    CHECK(ba3 && strcmp((const char *)ba3->data, "-v debug=0x14e") == 0,
          "boot-args sobrevive al round-trip binario");
    dt_free(root3);
    free(bin3);

    /* ---- T6: upsert sobre prop existente => modifica, no duplica ---- */
    printf("[T6] Upsert sobre existente no duplica\n");
    uint32_t n_now = chosen->n_props;
    dt_upsert_prop_str(chosen, "boot-args", "-v");
    CHECK(chosen->n_props == n_now, "n_props no cambia al modificar existente");

    /* ---- T7: delete decrementa n_props y libera sin leak ---- */
    printf("[T7] Delete de propiedad\n");
    uint32_t n_pre_del = chosen->n_props;
    int del = dt_delete_prop(chosen, "boot-args");
    CHECK(del == 0, "delete devolvio 0");
    CHECK(chosen->n_props == n_pre_del - 1, "n_props decrementado");
    CHECK(dt_get_prop(chosen, "boot-args") == NULL, "propiedad ya no existe");
    CHECK(dt_delete_prop(chosen, "no-existe") == -1, "delete de inexistente da -1");

    /* tras borrar, el arbol debe seguir serializando y re-parseando bien */
    size_t need4 = dt_serialize(root, NULL, 0);
    uint8_t *bin4 = malloc(need4);
    CHECK(dt_serialize(root, bin4, need4) == need4, "serializa tras delete");
    dt_node *root4 = dt_parse(bin4, need4, NULL);
    CHECK(root4 != NULL, "re-parsea tras delete");
    dt_free(root4);
    free(bin4);

    free(reser);
    free(orig);
    dt_free(root);   /* ASan: no debe reportar leaks ni double-free */

    printf("\n%s (%d fallos)\n", fails ? "RESULTADO: FALLOS" : "RESULTADO: TODO OK", fails);
    return fails ? 1 : 0;
}
