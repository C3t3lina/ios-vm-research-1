/* =========================================================================
 * apple_dt.h - Parser del Apple Device Tree (formato binario propio)
 *
 * El Device Tree de Apple NO es Flattened Device Tree (FDT/dtb de Linux).
 * Es un formato recursivo simple usado por iBoot/XNU:
 *
 *   nodo {
 *     uint32_t n_props;          // numero de propiedades
 *     uint32_t n_children;       // numero de hijos
 *     prop[ n_props ];           // propiedades
 *     nodo[ n_children ];        // hijos (recursivo)
 *   }
 *
 *   prop {
 *     char     name[32];         // nombre null-terminado, padded
 *     uint32_t length;           // bit 31 = flag de placeholder (se enmascara)
 *     uint8_t  data[length];     // padded a multiplo de 4 bytes
 *   }
 *
 * Referencias de formato: XNU (IODeviceTreeSupport), iBoot, dt herramientas
 * de la comunidad (alephsecurity / cylance dtetool).
 * ========================================================================= */
#ifndef APPLE_DT_H
#define APPLE_DT_H

#include <stdint.h>
#include <stddef.h>

#define DT_PROP_NAME_LEN   32u
#define DT_PROP_FLAG_MASK  0x80000000u   /* bit de placeholder en length    */
#define DT_LEN_MASK        0x7fffffffu   /* longitud real (sin el flag)     */
#define DT_ALIGN(x)        (((x) + 3u) & ~3u)

typedef struct dt_prop {
    char            name[DT_PROP_NAME_LEN];
    uint32_t        length;     /* longitud real de los datos          */
    uint32_t        flag;       /* 1 si tenia el bit placeholder        */
    const uint8_t  *data;       /* datos: buffer original O heap propio  */
    int             owns_data;  /* 1 = data esta en heap (mutado) y hay
                                   que liberarlo en dt_free; 0 = apunta
                                   al buffer original (no liberar)        */
    struct dt_prop *next;
} dt_prop;

typedef struct dt_node {
    uint32_t        n_props;
    uint32_t        n_children;
    dt_prop        *props;      /* lista enlazada de propiedades        */
    struct dt_node *children;   /* lista enlazada de hijos              */
    struct dt_node *next;       /* siguiente hermano                    */
} dt_node;

/* Parsea un buffer crudo. Devuelve el nodo raiz o NULL en error.
 * 'consumed' (opcional) recibe los bytes leidos. */
dt_node *dt_parse(const uint8_t *buf, size_t size, size_t *consumed);

/* Imprime el arbol en formato legible (estilo dts) por stdout. */
void dt_dump(const dt_node *root);

/* Busca un nodo por path estilo "/chosen/memory-map".
 * Usa la propiedad "name" de cada nodo para emparejar. */
const dt_node *dt_find(const dt_node *root, const char *path);

/* Devuelve una propiedad por nombre dentro de un nodo, o NULL. */
const dt_prop *dt_get_prop(const dt_node *node, const char *name);

/* ---- Mutacion (Mes 2, Semanas 5-8) ----------------------------------- */

/* Serializa el arbol de vuelta al formato binario nativo de Apple.
 * Devuelve 0 en exito. Round-trip garantizado: parse->write == original. */
int dt_write(const dt_node *root, const char *output_path);

/* Variante que escribe a un buffer en memoria (para tests/round-trip).
 * Si out==NULL solo calcula el tamano necesario. Devuelve bytes escritos
 * o (size_t)-1 en error. */
size_t dt_serialize(const dt_node *root, uint8_t *out, size_t cap);

/* Cambia los datos de una propiedad existente. Copia new_data al heap y
 * marca owns_data=1. Seguro de llamar multiples veces (libera lo previo).
 * Devuelve 0 en exito, -1 si la propiedad no existe. */
int dt_set_prop_data(dt_node *node, const char *prop_name,
                     const uint8_t *new_data, uint32_t new_len);

/* Atajo para boot-args y otras propiedades de texto (incluye el '\0'). */
int dt_set_prop_str(dt_node *node, const char *prop_name, const char *str);

/* Crea y AÑADE una propiedad nueva al nodo (incrementa n_props para
 * mantener coherencia con el serializador). Copia los datos al heap.
 * No comprueba duplicados: usa upsert si no estas seguro.
 * Devuelve 0 en exito, -1 en error. */
int dt_add_prop(dt_node *node, const char *prop_name,
                const uint8_t *data, uint32_t len);

/* Update-or-insert: modifica la propiedad si existe, o la crea si no.
 * Es lo que necesita un parcheador de boot-args robusto. */
int dt_upsert_prop_data(dt_node *node, const char *prop_name,
                        const uint8_t *new_data, uint32_t new_len);
int dt_upsert_prop_str(dt_node *node, const char *prop_name, const char *str);

/* Elimina una propiedad del nodo (decrementa n_props para coherencia con
 * el serializador y libera data si era nuestra). Devuelve 0 si la borro,
 * -1 si no existia. */
int dt_delete_prop(dt_node *node, const char *prop_name);

/* Libera el arbol (y los datos mutados con owns_data=1). */
void dt_free(dt_node *root);

#endif /* APPLE_DT_H */
