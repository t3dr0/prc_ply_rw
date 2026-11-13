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

/* mesh2ply: the writer, binary little-endian PLY in one fixed layout
   (specification 7). Every value is written byte by byte, least significant
   first, so the output is the same on every platform: no structure is
   written whole and the host's byte order does not matter. The property
   names are those prc_ply_rw's importer reads (ply_import.c). */

#include "m2p.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    FILE *f;
    unsigned char buf[1 << 16];
    size_t n;
    uint64_t total;
    int failed;
} out_t;

static void
flush_out(out_t *o)
{
    if (o->n > 0 && !o->failed && fwrite(o->buf, 1, o->n, o->f) != o->n)
        o->failed = 1;
    o->total += o->n;
    o->n = 0;
}

static void
put_byte(out_t *o, unsigned char b)
{
    if (o->n == sizeof(o->buf))
        flush_out(o);
    o->buf[o->n++] = b;
}

static void
put_u32(out_t *o, uint32_t v)
{
    put_byte(o, (unsigned char)(v & 0xFF));
    put_byte(o, (unsigned char)((v >> 8) & 0xFF));
    put_byte(o, (unsigned char)((v >> 16) & 0xFF));
    put_byte(o, (unsigned char)((v >> 24) & 0xFF));
}

static void
put_float(out_t *o, float f)
{
    uint32_t b;

    memcpy(&b, &f, sizeof(b));
    put_u32(o, b);
}

static void
put_text(out_t *o, const char *s)
{
    while (*s != '\0')
        put_byte(o, (unsigned char)*s++);
}

int
m2p_write_ply(const char *path, const m2p_mesh *m, uint64_t *bytes_out, m2p_err *e)
{
    static out_t o;
    char line[96];
    uint64_t v, f, at = 0;

    memset(&o, 0, sizeof(o));
    for (f = 0; f < m->nf; f++)
        if (m->fcount[f] > 255)
            return m2p_fail(e, M2P_EXIT_UNSUPPORTED,
                            "a face has %u vertices, more than a uchar count holds (--triangulate splits it)",
                            (unsigned)m->fcount[f]);
    o.f = fopen(path, "wb");
    if (o.f == NULL)
        return m2p_fail(e, M2P_EXIT_IO, "cannot create the output");

    put_text(&o, "ply\nformat binary_little_endian 1.0\n");
    sprintf(line, "element vertex %llu\n", (unsigned long long)m->nv);
    put_text(&o, line);
    put_text(&o, "property float x\nproperty float y\nproperty float z\n");
    if (m->nrm != NULL)
        put_text(&o, "property float nx\nproperty float ny\nproperty float nz\n");
    if (m->col != NULL)
    {
        put_text(&o, "property uchar red\nproperty uchar green\nproperty uchar blue\n");
        if (m->has_alpha)
            put_text(&o, "property uchar alpha\n");
    }
    sprintf(line, "element face %llu\n", (unsigned long long)m->nf);
    put_text(&o, line);
    put_text(&o, "property list uchar int vertex_indices\nend_header\n");

    for (v = 0; v < m->nv; v++)
    {
        put_float(&o, m->pos[v * 3]);
        put_float(&o, m->pos[v * 3 + 1]);
        put_float(&o, m->pos[v * 3 + 2]);
        if (m->nrm != NULL)
        {
            put_float(&o, m->nrm[v * 3]);
            put_float(&o, m->nrm[v * 3 + 1]);
            put_float(&o, m->nrm[v * 3 + 2]);
        }
        if (m->col != NULL)
        {
            put_byte(&o, m->col[v * 4]);
            put_byte(&o, m->col[v * 4 + 1]);
            put_byte(&o, m->col[v * 4 + 2]);
            if (m->has_alpha)
                put_byte(&o, m->col[v * 4 + 3]);
        }
    }
    for (f = 0; f < m->nf; f++)
    {
        uint32_t n = m->fcount[f], k;

        put_byte(&o, (unsigned char)n);
        for (k = 0; k < n; k++)
            put_u32(&o, m->idx[at + k]);
        at += n;
    }
    flush_out(&o);
    if (fclose(o.f) != 0)
        o.failed = 1;
    *bytes_out = o.total;
    if (o.failed)
        return m2p_fail(e, M2P_EXIT_IO, "cannot write the output");
    return M2P_OK;
}
