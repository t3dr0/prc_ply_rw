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

/* mesh2ply: the mesh structure's storage, error messages and small helpers. */

#include "m2p.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
m2p_fail(m2p_err *e, int code, const char *fmt, ...)
{
    va_list ap;

    e->code = code;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
    va_end(ap);
    return code;
}

void
m2p_mesh_init(m2p_mesh *m)
{
    memset(m, 0, sizeof(*m));
}

void
m2p_strbuf_free(m2p_strbuf *s)
{
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

void
m2p_mesh_free(m2p_mesh *m)
{
    free(m->pos);
    free(m->nrm);
    free(m->col);
    free(m->fcount);
    free(m->idx);
    m2p_strbuf_free(&m->dropped);
    memset(m, 0, sizeof(*m));
}

/* n * size, or 0 when that does not fit in size_t */
static size_t
mul_size(uint64_t n, size_t size)
{
    if (size != 0 && n > (uint64_t)(SIZE_MAX / size))
        return 0;
    return (size_t)n * size;
}

int
m2p_mesh_alloc_vertices(m2p_mesh *m, uint64_t nv, int normals, int colours, m2p_err *e)
{
    size_t fbytes = mul_size(nv, 3 * sizeof(float));
    size_t cbytes = mul_size(nv, 4);

    if (nv == 0)
        return M2P_OK;
    if (fbytes == 0 || cbytes == 0)
        return m2p_fail(e, M2P_EXIT_UNSUPPORTED, "%llu vertices do not fit in memory",
                        (unsigned long long)nv);
    m->pos = (float *)malloc(fbytes);
    if (normals)
        m->nrm = (float *)malloc(fbytes);
    if (colours)
        m->col = (unsigned char *)malloc(cbytes);
    if (m->pos == NULL || (normals && m->nrm == NULL) || (colours && m->col == NULL))
        return m2p_fail(e, M2P_EXIT_IO, "out of memory for %llu vertices", (unsigned long long)nv);
    m->nv = nv;
    return M2P_OK;
}

/* Grows *p, holding *cap elements of size bytes, to hold at least need */
static int
grow(void **p, uint64_t *cap, uint64_t need, size_t size)
{
    uint64_t c = *cap ? *cap : 1024;
    size_t bytes;
    void *q;

    while (c < need)
    {
        if (c > UINT64_MAX / 2)
            return 0;
        c *= 2;
    }
    bytes = mul_size(c, size);
    if (bytes == 0)
        return 0;
    q = realloc(*p, bytes);
    if (q == NULL)
        return 0;
    *p = q;
    *cap = c;
    return 1;
}

int
m2p_mesh_push_face(m2p_mesh *m, uint32_t count, m2p_err *e)
{
    if (m->nf + 1 > m->cap_f && !grow((void **)&m->fcount, &m->cap_f, m->nf + 1, sizeof(uint32_t)))
        return m2p_fail(e, M2P_EXIT_IO, "out of memory for faces");
    m->fcount[m->nf++] = count;
    return M2P_OK;
}

int
m2p_mesh_push_index(m2p_mesh *m, uint32_t index, m2p_err *e)
{
    if (m->nidx + 1 > m->cap_i && !grow((void **)&m->idx, &m->cap_i, m->nidx + 1, sizeof(uint32_t)))
        return m2p_fail(e, M2P_EXIT_IO, "out of memory for face indices");
    m->idx[m->nidx++] = index;
    return M2P_OK;
}

/* Adds name to the ';'-separated list unless it is there already */
int
m2p_strlist_add(m2p_strbuf *s, const char *name)
{
    size_t n = strlen(name);
    const char *p = s->data;
    char *q;

    while (p != NULL && *p != '\0')
    {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len == n && strncmp(p, name, n) == 0)
            return 1;
        p = end ? end + 1 : NULL;
    }
    if (s->len + n + 2 > s->cap)
    {
        size_t cap = (s->len + n + 2) * 2;

        q = (char *)realloc(s->data, cap);
        if (q == NULL)
            return 0;
        s->data = q;
        s->cap = cap;
    }
    if (s->len > 0)
        s->data[s->len++] = ';';
    memcpy(s->data + s->len, name, n);
    s->len += n;
    s->data[s->len] = '\0';
    return 1;
}

/* The file's size, counted by reading it: C99 has no portable call for the
   size of a file over 2 GB. Returns 0 if it cannot be read. */
int
m2p_file_size(const char *path, uint64_t *size)
{
    FILE *f = fopen(path, "rb");
    static unsigned char buf[1 << 16];
    size_t got;

    *size = 0;
    if (f == NULL)
        return 0;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
        *size += got;
    if (ferror(f))
    {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

/* A colour component given as a float in [0,1] (section 5.2) */
unsigned char
m2p_colour_from_float(double v)
{
    if (!(v > 0.0))
        return 0;
    if (v >= 1.0)
        return 255;
    return (unsigned char)floor(v * 255.0 + 0.5);
}

/* A colour component given as an integer in [0,255], clamped */
unsigned char
m2p_colour_from_int(double v)
{
    if (!(v > 0.0))
        return 0;
    if (v >= 255.0)
        return 255;
    return (unsigned char)floor(v + 0.5);
}
