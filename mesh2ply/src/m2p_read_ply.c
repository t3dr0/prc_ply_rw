/* Copyright (C) 2026 Ian Curington, Thames 3D Research

    prc_ply_rw is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    prc_ply_rw is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
    General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with prc_ply_rw. If not, see <https://www.gnu.org/licenses/>.
*/

/* mesh2ply: the PLY reader (specification 5.3), ASCII and binary of either
   byte order.

   The header is read here first, for the format name, the element counts
   and the property types: the counts are checked against the file's size
   before anything is allocated, and against RPly's own limit (it holds them
   in a long, 32 bits on Windows). The data is then read by RPly. */

#include "m2p.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rply.h"

#define MAX_HEADER (1u << 20)

typedef struct
{
    char name[64];
    int is_list;
    int type;          /* scalar type, or for a list its value type */
    int count_type;    /* for a list */
} hprop;

typedef struct
{
    char name[64];
    uint64_t count;
    hprop *props;
    size_t nprops, cap;
} helem;

typedef struct
{
    char format[32];
    uint64_t header_bytes;
    helem *elems;
    size_t nelems, cap;
} hdr;

enum { T_BAD = -1, T_I8, T_U8, T_I16, T_U16, T_I32, T_U32, T_F32, T_F64 };

static int
type_of(const char *s)
{
    static const struct { const char *name; int type; } names[] = {
        { "char", T_I8 }, { "int8", T_I8 }, { "uchar", T_U8 }, { "uint8", T_U8 },
        { "short", T_I16 }, { "int16", T_I16 }, { "ushort", T_U16 }, { "uint16", T_U16 },
        { "int", T_I32 }, { "int32", T_I32 }, { "uint", T_U32 }, { "uint32", T_U32 },
        { "float", T_F32 }, { "float32", T_F32 }, { "double", T_F64 }, { "float64", T_F64 }
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp(s, names[i].name) == 0)
            return names[i].type;
    return T_BAD;
}

static unsigned
type_size(int t)
{
    switch (t)
    {
    case T_I8: case T_U8: return 1;
    case T_I16: case T_U16: return 2;
    case T_F64: return 8;
    default: return 4;
    }
}

static void
hdr_free(hdr *h)
{
    size_t i;

    for (i = 0; i < h->nelems; i++)
        free(h->elems[i].props);
    free(h->elems);
    memset(h, 0, sizeof(*h));
}

static int
copy_name(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);

    if (n == 0 || n >= cap)
        return 0;
    memcpy(dst, src, n + 1);
    return 1;
}

/* Reads the header, up to and including "end_header" and its line end */
static int
read_header(const char *path, hdr *h, m2p_err *e)
{
    FILE *f = fopen(path, "rb");
    char line[4096];
    uint64_t used = 0;
    int got_format = 0, lineno = 0;

    memset(h, 0, sizeof(*h));
    if (f == NULL)
        return m2p_fail(e, M2P_EXIT_IO, "cannot open the input");
    while (fgets(line, sizeof(line), f) != NULL)
    {
        size_t n = strlen(line);
        char *tok[6];
        int nt = 0;
        char *p;

        lineno++;
        used += n;
        if (n == sizeof(line) - 1 && line[n - 1] != '\n')
        {
            /* Only a comment may be this long: skip the rest of it */
            int ch;

            if (strncmp(line, "comment", 7) != 0 && strncmp(line, "obj_info", 8) != 0)
            {
                fclose(f);
                return m2p_fail(e, M2P_EXIT_PARSE, "header line %d too long", lineno);
            }
            while ((ch = fgetc(f)) != EOF && ch != '\n')
                used++;
            used++;
            if (used > MAX_HEADER)
                break;
            continue;
        }
        if (used > MAX_HEADER)
        {
            fclose(f);
            return m2p_fail(e, M2P_EXIT_PARSE, "header larger than %u bytes", MAX_HEADER);
        }
        if (lineno == 1)
        {
            if (strncmp(line, "ply", 3) != 0)
            {
                fclose(f);
                return m2p_fail(e, M2P_EXIT_PARSE, "not a PLY file");
            }
            continue;
        }
        for (p = strtok(line, " \t\r\n"); p != NULL && nt < 6; p = strtok(NULL, " \t\r\n"))
            tok[nt++] = p;
        if (nt == 0 || strcmp(tok[0], "comment") == 0 || strcmp(tok[0], "obj_info") == 0)
            continue;
        if (strcmp(tok[0], "end_header") == 0)
        {
            fclose(f);
            h->header_bytes = used;
            if (!got_format)
                return m2p_fail(e, M2P_EXIT_PARSE, "no format line");
            return M2P_OK;
        }
        if (strcmp(tok[0], "format") == 0 && nt >= 2)
        {
            if (strcmp(tok[1], "ascii") != 0 && strcmp(tok[1], "binary_little_endian") != 0 &&
                strcmp(tok[1], "binary_big_endian") != 0)
                break;
            strcpy(h->format, tok[1]);
            got_format = 1;
        }
        else if (strcmp(tok[0], "element") == 0 && nt >= 3)
        {
            helem *el;
            char *end;
            unsigned long long count;

            if (h->nelems == h->cap)
            {
                size_t cap = h->cap ? h->cap * 2 : 8;
                helem *q = (helem *)realloc(h->elems, cap * sizeof(helem));

                if (q == NULL)
                    break;
                h->elems = q;
                h->cap = cap;
            }
            el = &h->elems[h->nelems];
            memset(el, 0, sizeof(*el));
            if (tok[2][0] == '-' || !copy_name(el->name, sizeof(el->name), tok[1]))
                break;
            count = strtoull(tok[2], &end, 10);
            if (*end != '\0')
                break;
            el->count = count;
            h->nelems++;
        }
        else if (strcmp(tok[0], "property") == 0 && nt >= 3 && h->nelems > 0)
        {
            helem *el = &h->elems[h->nelems - 1];
            hprop *pr;

            if (el->nprops == el->cap)
            {
                size_t cap = el->cap ? el->cap * 2 : 8;
                hprop *q = (hprop *)realloc(el->props, cap * sizeof(hprop));

                if (q == NULL)
                    break;
                el->props = q;
                el->cap = cap;
            }
            pr = &el->props[el->nprops];
            memset(pr, 0, sizeof(*pr));
            if (strcmp(tok[1], "list") == 0)
            {
                if (nt < 5)
                    break;
                pr->is_list = 1;
                pr->count_type = type_of(tok[2]);
                pr->type = type_of(tok[3]);
                if (pr->count_type == T_BAD || pr->type == T_BAD || !copy_name(pr->name, sizeof(pr->name), tok[4]))
                    break;
            }
            else
            {
                pr->type = type_of(tok[1]);
                if (pr->type == T_BAD || !copy_name(pr->name, sizeof(pr->name), tok[2]))
                    break;
            }
            el->nprops++;
        }
        else
            break;
    }
    fclose(f);
    return m2p_fail(e, M2P_EXIT_PARSE, "header line %d not understood", lineno);
}

typedef struct
{
    m2p_mesh *m;
    m2p_err *e;
    int failed;
    int col_float[4];
    char rply_msg[256];
} ply_ctx;

static void
error_cb(p_ply ply, const char *message)
{
    void *pdata = NULL;
    long idata;

    if (ply_get_ply_user_data(ply, &pdata, &idata) && pdata != NULL)
    {
        ply_ctx *c = (ply_ctx *)pdata;

        if (c->rply_msg[0] == '\0')
        {
            strncpy(c->rply_msg, message, sizeof(c->rply_msg) - 1);
            c->rply_msg[sizeof(c->rply_msg) - 1] = '\0';
        }
    }
}

/* Slots 0-2 position, 3-5 normal, 6-9 colour */
static int
vertex_cb(p_ply_argument a)
{
    void *pdata;
    long slot, inst;
    ply_ctx *c;
    m2p_mesh *m;
    double v;

    ply_get_argument_user_data(a, &pdata, &slot);
    ply_get_argument_element(a, NULL, &inst);
    c = (ply_ctx *)pdata;
    m = c->m;
    v = ply_get_argument_value(a);
    if (inst < 0 || (uint64_t)inst >= m->nv)
        return 0;
    if (slot < 3)
    {
        float f = (float)v;

        if (!isfinite(f))
        {
            m2p_fail(c->e, M2P_EXIT_INVALID, "vertex %ld: a coordinate is not finite", inst);
            c->failed = 1;
            return 0;
        }
        m->pos[(size_t)inst * 3 + (size_t)slot] = f;
    }
    else if (slot < 6)
    {
        float f = (float)v;

        if (!isfinite(f))
        {
            m2p_fail(c->e, M2P_EXIT_INVALID, "vertex %ld: a normal is not finite", inst);
            c->failed = 1;
            return 0;
        }
        m->nrm[(size_t)inst * 3 + (size_t)(slot - 3)] = f;
    }
    else
    {
        int ch = (int)(slot - 6);

        if (v != v)
        {
            m2p_fail(c->e, M2P_EXIT_INVALID, "vertex %ld: a colour is not a number", inst);
            c->failed = 1;
            return 0;
        }
        m->col[(size_t)inst * 4 + (size_t)ch] = c->col_float[ch] ? m2p_colour_from_float(v)
                                                                  : m2p_colour_from_int(v);
    }
    return 1;
}

static int
face_cb(p_ply_argument a)
{
    void *pdata;
    long idata, length, value_index;
    ply_ctx *c;
    m2p_mesh *m;
    double v;

    ply_get_argument_user_data(a, &pdata, &idata);
    ply_get_argument_property(a, NULL, &length, &value_index);
    c = (ply_ctx *)pdata;
    m = c->m;
    v = ply_get_argument_value(a);
    if (value_index < 0)
    {
        if (!(v >= 0.0) || v > 4294967295.0 || v != floor(v))
        {
            m2p_fail(c->e, M2P_EXIT_PARSE, "face %llu: a bad vertex count", (unsigned long long)m->nf);
            c->failed = 1;
            return 0;
        }
        if ((uint32_t)v > m->max_poly)
            m->max_poly = (uint32_t)v;
        if (m2p_mesh_push_face(m, (uint32_t)v, c->e) != M2P_OK)
        {
            c->failed = 1;
            return 0;
        }
        return 1;
    }
    if (m2p_mesh_push_index(m, (v >= 0.0 && v < (double)m->nv && v == floor(v)) ? (uint32_t)v : M2P_BAD_INDEX,
                            c->e) != M2P_OK)
    {
        c->failed = 1;
        return 0;
    }
    return 1;
}

static int
name_in(const char *name, const char *const *list)
{
    int i;

    for (i = 0; list[i] != NULL; i++)
        if (strcmp(name, list[i]) == 0)
            return i;
    return -1;
}

int
m2p_read_ply(const char *path, uint64_t file_size, m2p_mesh *m, m2p_err *e)
{
    static const char *const POS[] = { "x", "y", "z", NULL };
    static const char *const NRM[] = { "nx", "ny", "nz", NULL };
    static const char *const COL[] = { "red", "green", "blue", "alpha", NULL };
    static const char *const DCOL[] = { "diffuse_red", "diffuse_green", "diffuse_blue", "diffuse_alpha", NULL };
    static const char *const FIDX[] = { "vertex_indices", "vertex_index", NULL };
    hdr h;
    ply_ctx ctx;
    p_ply ply = NULL;
    const helem *ve = NULL, *fe = NULL;
    const char *pos_name[3] = { NULL, NULL, NULL }, *nrm_name[3] = { NULL, NULL, NULL };
    const char *col_name[4] = { NULL, NULL, NULL, NULL };
    const char *fidx_name = NULL;
    int col_type[4] = { 0, 0, 0, 0 };
    int ascii, normals, colours, code;
    uint64_t avail, need = 0;
    size_t i, k;

    memset(&ctx, 0, sizeof(ctx));
    strcpy(m->input_format, "ply");
    code = read_header(path, &h, e);
    if (code != M2P_OK)
        return code;
    strcpy(m->header, h.format);
    ascii = strcmp(h.format, "ascii") == 0;
    avail = file_size > h.header_bytes ? file_size - h.header_bytes : 0;

    for (i = 0; i < h.nelems; i++)
    {
        const helem *el = &h.elems[i];
        uint64_t min_bytes = 0;

        /* The least each instance can take: 1 to 8 bytes a scalar and the
           count of an empty list in binary, 2 bytes ("0 ") in ASCII */
        for (k = 0; k < el->nprops; k++)
            min_bytes += ascii ? 2 : type_size(el->props[k].is_list ? el->props[k].count_type : el->props[k].type);
        if (min_bytes > 0 && el->count > avail / min_bytes)
        {
            /* The message first: el points into h */
            m2p_fail(e, M2P_EXIT_PARSE, "element %s: %llu instances cannot fit in a file of %llu bytes",
                     el->name, (unsigned long long)el->count, (unsigned long long)file_size);
            hdr_free(&h);
            return e->code;
        }
        need += el->count * min_bytes;
        if (need > avail)
        {
            hdr_free(&h);
            return m2p_fail(e, M2P_EXIT_PARSE, "the elements cannot fit in a file of %llu bytes",
                            (unsigned long long)file_size);
        }
        if (strcmp(el->name, "vertex") == 0 && ve == NULL)
            ve = el;
        else if (strcmp(el->name, "face") == 0 && fe == NULL)
            fe = el;
        else
            m2p_strlist_add(&m->dropped, el->name);
        if (el->count > (uint64_t)LONG_MAX)
        {
            m2p_fail(e, M2P_EXIT_UNSUPPORTED, "element %s: %llu instances is more than can be read",
                     el->name, (unsigned long long)el->count);
            hdr_free(&h);
            return e->code;
        }
    }
    if (ve != NULL && ve->count > M2P_MAX_VERTICES)
    {
        code = m2p_fail(e, M2P_EXIT_UNSUPPORTED, "%llu vertices: at most 2^31-1 can be written",
                        (unsigned long long)ve->count);
        hdr_free(&h);
        return code;
    }

    /* Which vertex properties are read; the rest are dropped */
    if (ve != NULL)
    {
        for (k = 0; k < ve->nprops; k++)
        {
            const hprop *pr = &ve->props[k];
            int j;

            if (!pr->is_list && (j = name_in(pr->name, POS)) >= 0 && pos_name[j] == NULL)
                pos_name[j] = pr->name;
            else if (!pr->is_list && (j = name_in(pr->name, NRM)) >= 0 && nrm_name[j] == NULL)
                nrm_name[j] = pr->name;
            else if (!pr->is_list && ((j = name_in(pr->name, COL)) >= 0 || (j = name_in(pr->name, DCOL)) >= 0) &&
                     col_name[j] == NULL)
            {
                col_name[j] = pr->name;
                col_type[j] = pr->type;
            }
            else
                m2p_strlist_add(&m->dropped, pr->name);
        }
        if (pos_name[0] == NULL || pos_name[1] == NULL || pos_name[2] == NULL)
        {
            hdr_free(&h);
            return m2p_fail(e, M2P_EXIT_PARSE, "the vertex element has no x, y and z");
        }
    }
    normals = nrm_name[0] != NULL && nrm_name[1] != NULL && nrm_name[2] != NULL;
    colours = col_name[0] != NULL && col_name[1] != NULL && col_name[2] != NULL;
    for (k = 0; k < 3; k++)
    {
        if (!normals && nrm_name[k] != NULL)
            m2p_strlist_add(&m->dropped, nrm_name[k]);
        if (!colours && col_name[k] != NULL)
            m2p_strlist_add(&m->dropped, col_name[k]);
    }
    if (!colours && col_name[3] != NULL)
        m2p_strlist_add(&m->dropped, col_name[3]);
    m->has_alpha = colours && col_name[3] != NULL;
    for (k = 0; k < 4; k++)
        ctx.col_float[k] = col_type[k] == T_F32 || col_type[k] == T_F64;

    /* The face list; other face properties are dropped */
    if (fe != NULL)
    {
        for (k = 0; k < fe->nprops; k++)
        {
            const hprop *pr = &fe->props[k];

            if (pr->is_list && fidx_name == NULL && name_in(pr->name, FIDX) >= 0)
            {
                if (pr->type == T_F32 || pr->type == T_F64 || pr->count_type == T_F32 || pr->count_type == T_F64)
                {
                    m2p_fail(e, M2P_EXIT_PARSE, "face %s is not a list of integers", pr->name);
                    hdr_free(&h);
                    return e->code;
                }
                fidx_name = pr->name;
            }
            else
            {
                m2p_strlist_add(&m->dropped, pr->name);
                if (name_in(pr->name, COL) >= 0 || name_in(pr->name, DCOL) >= 0)
                    m->dropped_face_colours = fe->count;
            }
        }
        if (fidx_name == NULL && fe->count > 0)
        {
            hdr_free(&h);
            return m2p_fail(e, M2P_EXIT_PARSE, "the face element has no vertex_indices list");
        }
    }

    m->vertices_in = ve != NULL ? ve->count : 0;
    m->faces_in = fe != NULL ? fe->count : 0;
    code = m2p_mesh_alloc_vertices(m, m->vertices_in, normals, colours, e);
    if (code != M2P_OK)
    {
        hdr_free(&h);
        return code;
    }
    if (m->col != NULL)
        memset(m->col, 255, (size_t)m->nv * 4);

    ctx.m = m;
    ctx.e = e;
    ply = ply_open(path, error_cb, 0, &ctx);
    if (ply == NULL)
    {
        hdr_free(&h);
        return m2p_fail(e, M2P_EXIT_IO, "cannot open the input");
    }
    if (!ply_read_header(ply))
    {
        code = m2p_fail(e, M2P_EXIT_PARSE, "%s", ctx.rply_msg[0] ? ctx.rply_msg : "bad header");
        goto done;
    }
    if (ve != NULL)
    {
        for (k = 0; k < 3; k++)
        {
            ply_set_read_cb(ply, "vertex", pos_name[k], vertex_cb, &ctx, (long)k);
            if (normals)
                ply_set_read_cb(ply, "vertex", nrm_name[k], vertex_cb, &ctx, (long)(3 + k));
        }
        if (colours)
            for (k = 0; k < 4; k++)
                if (col_name[k] != NULL)
                    ply_set_read_cb(ply, "vertex", col_name[k], vertex_cb, &ctx, (long)(6 + k));
    }
    if (fidx_name != NULL)
        ply_set_read_cb(ply, "face", fidx_name, face_cb, &ctx, 0);
    if (!ply_read(ply))
    {
        if (!ctx.failed)
            code = m2p_fail(e, M2P_EXIT_PARSE, "%s", ctx.rply_msg[0] ? ctx.rply_msg : "the data could not be read");
        else
            code = e->code;
        goto done;
    }
    code = M2P_OK;

done:
    ply_close(ply);
    hdr_free(&h);
    return code;
}
