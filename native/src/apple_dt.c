/* =========================================================================
 * apple_dt.c - Implementacion del parser del Apple Device Tree.
 * Ver apple_dt.h para la descripcion del formato binario.
 *
 * Diseno: parser de un solo pase, sin copiar los datos de las propiedades
 * (apuntan al buffer original; por eso el buffer debe seguir vivo).
 * ========================================================================= */
#include "apple_dt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Cursor sobre el buffer con control de limites. */
typedef struct {
    const uint8_t *base;
    size_t         size;
    size_t         off;
    int            err;
} cur_t;

static uint32_t rd_u32(cur_t *c) {
    if (c->err || c->off + 4 > c->size) { c->err = 1; return 0; }
    uint32_t v;
    memcpy(&v, c->base + c->off, 4);   /* little-endian (host = ARM/x86 LE) */
    c->off += 4;
    return v;
}

static const uint8_t *rd_bytes(cur_t *c, size_t n) {
    if (c->err || c->off + n > c->size) { c->err = 1; return NULL; }
    const uint8_t *p = c->base + c->off;
    c->off += n;
    return p;
}

static dt_prop *parse_prop(cur_t *c) {
    const uint8_t *name = rd_bytes(c, DT_PROP_NAME_LEN);
    if (c->err) return NULL;

    uint32_t raw_len = rd_u32(c);
    if (c->err) return NULL;

    dt_prop *p = calloc(1, sizeof(*p));
    if (!p) { c->err = 1; return NULL; }

    memcpy(p->name, name, DT_PROP_NAME_LEN);
    p->name[DT_PROP_NAME_LEN - 1] = '\0';   /* defensivo */
    p->flag      = (raw_len & DT_PROP_FLAG_MASK) ? 1u : 0u;
    p->length    = raw_len & DT_LEN_MASK;
    p->owns_data = 0;   /* apunta al buffer original, no liberar */

    p->data = rd_bytes(c, p->length);
    if (c->err) { free(p); return NULL; }

    /* Los datos van padded a multiplo de 4. */
    size_t pad = DT_ALIGN(p->length) - p->length;
    if (pad) { (void)rd_bytes(c, pad); }

    return p;
}

static dt_node *parse_node(cur_t *c) {
    uint32_t n_props    = rd_u32(c);
    uint32_t n_children = rd_u32(c);
    if (c->err) return NULL;

    /* sanidad: evita explosiones por datos corruptos */
    if (n_props > 4096u || n_children > 65536u) { c->err = 1; return NULL; }

    dt_node *node = calloc(1, sizeof(*node));
    if (!node) { c->err = 1; return NULL; }
    node->n_props    = n_props;
    node->n_children = n_children;

    /* propiedades (mantener orden con tail-insert) */
    dt_prop **pt = &node->props;
    for (uint32_t i = 0; i < n_props && !c->err; i++) {
        dt_prop *p = parse_prop(c);
        if (!p) break;
        *pt = p; pt = &p->next;
    }

    /* hijos (recursivo, mantener orden) */
    dt_node **ct = &node->children;
    for (uint32_t i = 0; i < n_children && !c->err; i++) {
        dt_node *ch = parse_node(c);
        if (!ch) break;
        *ct = ch; ct = &ch->next;
    }

    if (c->err) { dt_free(node); return NULL; }
    return node;
}

dt_node *dt_parse(const uint8_t *buf, size_t size, size_t *consumed) {
    if (!buf || size < 8) return NULL;
    cur_t c = { .base = buf, .size = size, .off = 0, .err = 0 };
    dt_node *root = parse_node(&c);
    if (c.err) { dt_free(root); return NULL; }
    if (consumed) *consumed = c.off;
    return root;
}

/* ---- utilidades de consulta ------------------------------------------- */

const dt_prop *dt_get_prop(const dt_node *node, const char *name) {
    if (!node) return NULL;
    for (const dt_prop *p = node->props; p; p = p->next)
        if (strncmp(p->name, name, DT_PROP_NAME_LEN) == 0)
            return p;
    return NULL;
}

/* nombre legible de un nodo: su propiedad "name" (o "<anon>") */
static const char *node_name(const dt_node *n) {
    const dt_prop *p = dt_get_prop(n, "name");
    if (p && p->length > 0) return (const char *)p->data;
    return "<anon>";
}

const dt_node *dt_find(const dt_node *root, const char *path) {
    if (!root || !path) return NULL;
    if (path[0] == '/') path++;
    if (*path == '\0') return root;

    char seg[256];
    const char *slash = strchr(path, '/');
    size_t seglen = slash ? (size_t)(slash - path) : strlen(path);
    if (seglen >= sizeof(seg)) return NULL;
    memcpy(seg, path, seglen); seg[seglen] = '\0';

    for (const dt_node *ch = root->children; ch; ch = ch->next) {
        if (strcmp(node_name(ch), seg) == 0)
            return slash ? dt_find(ch, slash + 1) : ch;
    }
    return NULL;
}

/* ---- dump legible ----------------------------------------------------- */

static int prop_is_string(const dt_prop *p) {
    if (p->length == 0 || p->length > 256) return 0;
    for (uint32_t i = 0; i + 1 < p->length; i++)
        if (p->data[i] != '\0' && !isprint(p->data[i])) return 0;
    return p->data[p->length - 1] == '\0';
}

static void indent(int d) { for (int i = 0; i < d; i++) fputs("    ", stdout); }

static void dump_prop(const dt_prop *p, int depth) {
    indent(depth);
    printf("%s%s", p->name, p->flag ? " [placeholder]" : "");
    if (p->length == 0) { printf(";\n"); return; }

    if (prop_is_string(p)) {
        printf(" = \"%s\";\n", (const char *)p->data);
    } else if (p->length == 4) {
        uint32_t v; memcpy(&v, p->data, 4);
        printf(" = <0x%08x>;\n", v);
    } else {
        printf(" = [");
        uint32_t show = p->length < 16 ? p->length : 16;
        for (uint32_t i = 0; i < show; i++) printf("%02x ", p->data[i]);
        printf("%s] (%u bytes);\n", p->length > show ? "..." : "", p->length);
    }
}

static void dump_node(const dt_node *n, int depth) {
    indent(depth);
    printf("%s {  (props=%u children=%u)\n", node_name(n), n->n_props, n->n_children);
    for (const dt_prop *p = n->props; p; p = p->next) dump_prop(p, depth + 1);
    for (const dt_node *c = n->children; c; c = c->next) dump_node(c, depth + 1);
    indent(depth); printf("};\n");
}

void dt_dump(const dt_node *root) {
    if (!root) { printf("(arbol vacio)\n"); return; }
    dump_node(root, 0);
}

void dt_free(dt_node *root) {
    if (!root) return;
    dt_prop *p = root->props;
    while (p) {
        dt_prop *n = p->next;
        /* solo liberamos data si es de nuestra propiedad (mutado) */
        if (p->owns_data) free((void *)p->data);
        free(p);
        p = n;
    }
    dt_node *c = root->children;
    while (c) { dt_node *n = c->next; dt_free(c); c = n; }
    free(root);
}

/* =========================================================================
 * SERIALIZADOR / MUTADOR (Mes 2, Semanas 5-8)
 * ========================================================================= */

/* Escritor a buffer. Patron "dry-run": si out==NULL solo cuenta bytes.
 * Devuelve total de bytes (escritos o necesarios), o (size_t)-1 si no cabe. */
typedef struct { uint8_t *out; size_t cap; size_t off; int err; } wctx_t;

static void w_raw(wctx_t *w, const void *src, size_t n) {
    if (w->err) return;
    if (w->out) {
        if (w->off + n > w->cap) { w->err = 1; return; }
        memcpy(w->out + w->off, src, n);
    }
    w->off += n;
}
static void w_u32(wctx_t *w, uint32_t v) { w_raw(w, &v, 4); }
static void w_zero(wctx_t *w, size_t n) {
    static const uint8_t z[4] = {0,0,0,0};
    while (n) { size_t k = n < 4 ? n : 4; w_raw(w, z, k); n -= k; }
}

static void serialize_node_buf(const dt_node *n, wctx_t *w) {
    if (w->err || !n) return;
    w_u32(w, n->n_props);
    w_u32(w, n->n_children);

    for (const dt_prop *p = n->props; p; p = p->next) {
        char name_padded[DT_PROP_NAME_LEN];
        memset(name_padded, 0, sizeof(name_padded));
        /* p->name ya viene null-terminado del parser; copiamos hasta el
         * largo real sin arriesgar truncado de strncpy. El campo on-disk
         * son 32 bytes con padding de ceros. */
        size_t nlen = 0;
        while (nlen < DT_PROP_NAME_LEN - 1 && p->name[nlen]) nlen++;
        memcpy(name_padded, p->name, nlen);
        w_raw(w, name_padded, DT_PROP_NAME_LEN);

        uint32_t raw_len = p->length;
        if (p->flag) raw_len |= DT_PROP_FLAG_MASK;
        w_u32(w, raw_len);

        if (p->length && p->data) w_raw(w, p->data, p->length);
        w_zero(w, DT_ALIGN(p->length) - p->length);   /* padding */
    }

    for (const dt_node *ch = n->children; ch; ch = ch->next)
        serialize_node_buf(ch, w);
}

size_t dt_serialize(const dt_node *root, uint8_t *out, size_t cap) {
    if (!root) return (size_t)-1;
    wctx_t w = { .out = out, .cap = cap, .off = 0, .err = 0 };
    serialize_node_buf(root, &w);
    return w.err ? (size_t)-1 : w.off;
}

int dt_write(const dt_node *root, const char *output_path) {
    if (!root || !output_path) return -1;
    /* 1) dry-run para conocer el tamano */
    size_t need = dt_serialize(root, NULL, 0);
    if (need == (size_t)-1) return -1;
    uint8_t *buf = malloc(need);
    if (!buf) return -1;
    if (dt_serialize(root, buf, need) != need) { free(buf); return -1; }

    FILE *f = fopen(output_path, "wb");
    if (!f) { perror("dt_write: fopen"); free(buf); return -1; }
    size_t wn = fwrite(buf, 1, need, f);
    fclose(f);
    free(buf);
    return wn == need ? 0 : -1;
}

int dt_set_prop_data(dt_node *node, const char *prop_name,
                     const uint8_t *new_data, uint32_t new_len) {
    dt_prop *p = (dt_prop *)dt_get_prop(node, prop_name);
    if (!p) return -1;   /* (un mutador avanzado podria crear la prop) */

    uint8_t *copy = NULL;
    if (new_len) {
        copy = malloc(new_len);
        if (!copy) return -1;
        memcpy(copy, new_data, new_len);
    }
    /* libera datos previos SOLO si eran nuestros (evita tocar el buffer) */
    if (p->owns_data) free((void *)p->data);

    p->data      = copy;
    p->length    = new_len;
    p->owns_data = 1;
    return 0;
}

int dt_set_prop_str(dt_node *node, const char *prop_name, const char *str) {
    return dt_set_prop_data(node, prop_name,
                            (const uint8_t *)str,
                            (uint32_t)(strlen(str) + 1));
}

int dt_add_prop(dt_node *node, const char *prop_name,
                const uint8_t *data, uint32_t len) {
    if (!node || !prop_name) return -1;

    dt_prop *p = calloc(1, sizeof(*p));
    if (!p) return -1;

    /* nombre: copiar truncando a 31 chars + '\0' (formato on-disk = 32) */
    size_t nlen = strlen(prop_name);
    if (nlen > DT_PROP_NAME_LEN - 1) nlen = DT_PROP_NAME_LEN - 1;
    memcpy(p->name, prop_name, nlen);
    p->name[nlen] = '\0';

    if (len) {
        uint8_t *copy = malloc(len);
        if (!copy) { free(p); return -1; }
        memcpy(copy, data, len);
        p->data = copy;
    }
    p->length    = len;
    p->flag      = 0;
    p->owns_data = 1;   /* siempre nuestro: lo libera dt_free */

    /* insertar al final de la lista para preservar el orden de aparicion */
    dt_prop **tail = &node->props;
    while (*tail) tail = &(*tail)->next;
    *tail = p;

    /* CRITICO: el serializador escribe n_props en la cabecera y luego
     * recorre la lista. Si no incrementamos, el binario queda corrupto. */
    node->n_props++;
    return 0;
}

int dt_upsert_prop_data(dt_node *node, const char *prop_name,
                        const uint8_t *new_data, uint32_t new_len) {
    if (dt_get_prop(node, prop_name))
        return dt_set_prop_data(node, prop_name, new_data, new_len);
    return dt_add_prop(node, prop_name, new_data, new_len);
}

int dt_upsert_prop_str(dt_node *node, const char *prop_name, const char *str) {
    return dt_upsert_prop_data(node, prop_name,
                               (const uint8_t *)str,
                               (uint32_t)(strlen(str) + 1));
}

int dt_delete_prop(dt_node *node, const char *prop_name) {
    if (!node || !prop_name) return -1;
    dt_prop **pp = &node->props;
    while (*pp) {
        dt_prop *cur = *pp;
        if (strncmp(cur->name, prop_name, DT_PROP_NAME_LEN) == 0) {
            *pp = cur->next;                 /* desenlazar */
            if (cur->owns_data) free((void *)cur->data);
            free(cur);
            node->n_props--;                 /* coherencia con serializador */
            return 0;
        }
        pp = &cur->next;
    }
    return -1;  /* no existia */
}
