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

/* mesh2ply: face validation and the clean-up steps (specification 5.5, 6).

   Each step works on the mesh structure only and counts what it removes.
   The weld and the duplicate search use open-addressing hash tables over
   the records themselves, so both run in expected linear time; each keeps
   the first occurrence in its place and moves the rest down in order. */

#include "m2p.h"

#include <stdlib.h>
#include <string.h>

#define EMPTY 0xFFFFFFFFu

static uint64_t
mix(uint64_t h, uint32_t w)
{
    h ^= w;
    h *= UINT64_C(0x9E3779B97F4A7C15);
    h ^= h >> 29;
    return h;
}

/* A float's bits, with -0.0 taken as +0.0 */
static uint32_t
norm_bits(float f)
{
    uint32_t b;

    memcpy(&b, &f, sizeof(b));
    return b == 0x80000000u ? 0u : b;
}

/* The smallest power of two at least 2 * n, and at least 16 */
static uint64_t
table_size(uint64_t n)
{
    uint64_t c = 16;

    while (c < 2 * n)
        c *= 2;
    return c;
}

int
m2p_check_faces(m2p_mesh *m, int drop_invalid, m2p_counts *c, m2p_err *e)
{
    uint64_t f, at = 0, out_f = 0, out_i = 0, bad = 0;

    for (f = 0; f < m->nf; f++)
    {
        uint32_t n = m->fcount[f], k;
        int ok = n >= 3;

        for (k = 0; ok && k < n; k++)
            if (m->idx[at + k] == M2P_BAD_INDEX)
                ok = 0;
        if (!ok)
            bad++;
        if (ok || !drop_invalid)
        {
            if (out_i != at)
                memmove(m->idx + out_i, m->idx + at, (size_t)n * sizeof(uint32_t));
            m->fcount[out_f++] = n;
            out_i += n;
        }
        at += n;
    }
    c->invalid_faces = bad;
    if (bad > 0 && !drop_invalid)
        return m2p_fail(e, M2P_EXIT_INVALID,
                        "%llu faces have an out-of-range index or fewer than 3 vertices (--drop-invalid drops them)",
                        (unsigned long long)bad);
    m->nf = out_f;
    m->nidx = out_i;
    return M2P_OK;
}

static uint64_t
vertex_hash(const m2p_mesh *m, uint64_t i)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    int k;

    for (k = 0; k < 3; k++)
        h = mix(h, norm_bits(m->pos[i * 3 + (uint64_t)k]));
    if (m->nrm != NULL)
        for (k = 0; k < 3; k++)
            h = mix(h, norm_bits(m->nrm[i * 3 + (uint64_t)k]));
    if (m->col != NULL)
    {
        uint32_t w;

        memcpy(&w, m->col + i * 4, 4);
        h = mix(h, w);
    }
    return h;
}

static int
vertex_equal(const m2p_mesh *m, uint64_t a, uint64_t b)
{
    int k;

    for (k = 0; k < 3; k++)
        if (norm_bits(m->pos[a * 3 + (uint64_t)k]) != norm_bits(m->pos[b * 3 + (uint64_t)k]))
            return 0;
    if (m->nrm != NULL)
        for (k = 0; k < 3; k++)
            if (norm_bits(m->nrm[a * 3 + (uint64_t)k]) != norm_bits(m->nrm[b * 3 + (uint64_t)k]))
                return 0;
    if (m->col != NULL && memcmp(m->col + a * 4, m->col + b * 4, 4) != 0)
        return 0;
    return 1;
}

int
m2p_weld(m2p_mesh *m, m2p_counts *c, m2p_err *e)
{
    uint64_t cap = table_size(m->nv), mask = cap - 1, i, kept = 0;
    uint32_t *table, *remap;

    table = (uint32_t *)malloc((size_t)cap * sizeof(uint32_t));
    remap = (uint32_t *)malloc((size_t)(m->nv ? m->nv : 1) * sizeof(uint32_t));
    if (table == NULL || remap == NULL)
    {
        free(table);
        free(remap);
        return m2p_fail(e, M2P_EXIT_IO, "out of memory for the weld");
    }
    memset(table, 0xFF, (size_t)cap * sizeof(uint32_t));

    for (i = 0; i < m->nv; i++)
    {
        uint64_t slot = vertex_hash(m, i) & mask;
        uint32_t found = EMPTY;

        while (table[slot] != EMPTY)
        {
            if (vertex_equal(m, table[slot], i))
            {
                found = table[slot];
                break;
            }
            slot = (slot + 1) & mask;
        }
        if (found != EMPTY)
        {
            remap[i] = found;
            continue;
        }
        /* A new record: it moves down to the next kept place. Places below
           kept are final, so the table's entries stay valid. */
        if (kept != i)
        {
            memcpy(m->pos + kept * 3, m->pos + i * 3, 3 * sizeof(float));
            if (m->nrm != NULL)
                memcpy(m->nrm + kept * 3, m->nrm + i * 3, 3 * sizeof(float));
            if (m->col != NULL)
                memcpy(m->col + kept * 4, m->col + i * 4, 4);
        }
        table[slot] = (uint32_t)kept;
        remap[i] = (uint32_t)kept;
        kept++;
    }
    for (i = 0; i < m->nidx; i++)
        m->idx[i] = remap[m->idx[i]];
    c->welded_merged = m->nv - kept;
    m->nv = kept;
    free(table);
    free(remap);
    return M2P_OK;
}

int
m2p_triangulate(m2p_mesh *m, m2p_err *e)
{
    uint64_t f, ntri = 0, at = 0, t = 0;
    uint32_t *idx, *fc;

    for (f = 0; f < m->nf; f++)
        ntri += m->fcount[f] >= 3 ? m->fcount[f] - 2 : 0;
    if (ntri > (uint64_t)(SIZE_MAX / (3 * sizeof(uint32_t))) || ntri >= EMPTY)
        return m2p_fail(e, M2P_EXIT_UNSUPPORTED, "%llu triangles are too many", (unsigned long long)ntri);
    idx = (uint32_t *)malloc((size_t)(ntri ? ntri : 1) * 3 * sizeof(uint32_t));
    fc = (uint32_t *)malloc((size_t)(ntri ? ntri : 1) * sizeof(uint32_t));
    if (idx == NULL || fc == NULL)
    {
        free(idx);
        free(fc);
        return m2p_fail(e, M2P_EXIT_IO, "out of memory for triangles");
    }
    /* Polygon v0 v1 ... vn-1 -> (v0, vk, vk+1) for k = 1 ... n-2 */
    for (f = 0; f < m->nf; f++)
    {
        uint32_t n = m->fcount[f], k;

        for (k = 1; k + 1 < n; k++)
        {
            idx[t * 3 + 0] = m->idx[at];
            idx[t * 3 + 1] = m->idx[at + k];
            idx[t * 3 + 2] = m->idx[at + k + 1];
            fc[t] = 3;
            t++;
        }
        at += n;
    }
    free(m->idx);
    free(m->fcount);
    m->idx = idx;
    m->fcount = fc;
    m->nf = ntri;
    m->nidx = ntri * 3;
    m->cap_f = ntri ? ntri : 1;
    m->cap_i = (ntri ? ntri : 1) * 3;
    return M2P_OK;
}

/* Triangles only (after m2p_triangulate) */
void
m2p_drop_degenerate(m2p_mesh *m, m2p_counts *c)
{
    uint64_t t, kept = 0;

    for (t = 0; t < m->nf; t++)
    {
        uint32_t a = m->idx[t * 3], b = m->idx[t * 3 + 1], d = m->idx[t * 3 + 2];

        if (a == b || b == d || a == d)
            continue;
        m->idx[kept * 3] = a;
        m->idx[kept * 3 + 1] = b;
        m->idx[kept * 3 + 2] = d;
        kept++;
    }
    c->degenerate = m->nf - kept;
    m->nf = kept;
    m->nidx = kept * 3;
}

static void
sorted3(const uint32_t *t, uint32_t s[3])
{
    uint32_t a = t[0], b = t[1], d = t[2], x;

    if (a > b) { x = a; a = b; b = x; }
    if (b > d) { x = b; b = d; d = x; }
    if (a > b) { x = a; a = b; b = x; }
    s[0] = a;
    s[1] = b;
    s[2] = d;
}

/* Triangles only. A triangle whose three indices, as a set, were seen
   before goes, whatever its order or orientation. */
int
m2p_drop_duplicates(m2p_mesh *m, m2p_counts *c, m2p_err *e)
{
    uint64_t cap = table_size(m->nf), mask = cap - 1, t, kept = 0;
    uint32_t *table = (uint32_t *)malloc((size_t)cap * sizeof(uint32_t));

    if (table == NULL)
        return m2p_fail(e, M2P_EXIT_IO, "out of memory for the duplicate search");
    memset(table, 0xFF, (size_t)cap * sizeof(uint32_t));
    for (t = 0; t < m->nf; t++)
    {
        uint32_t s[3], u[3];
        uint64_t slot;
        int dup = 0;

        sorted3(m->idx + t * 3, s);
        slot = mix(mix(mix(UINT64_C(0xcbf29ce484222325), s[0]), s[1]), s[2]) & mask;
        while (table[slot] != EMPTY)
        {
            sorted3(m->idx + (uint64_t)table[slot] * 3, u);
            if (u[0] == s[0] && u[1] == s[1] && u[2] == s[2])
            {
                dup = 1;
                break;
            }
            slot = (slot + 1) & mask;
        }
        if (dup)
            continue;
        if (kept != t)
            memcpy(m->idx + kept * 3, m->idx + t * 3, 3 * sizeof(uint32_t));
        table[slot] = (uint32_t)kept;
        kept++;
    }
    free(table);
    c->duplicate = m->nf - kept;
    m->nf = kept;
    m->nidx = kept * 3;
    return M2P_OK;
}

int
m2p_drop_unreferenced(m2p_mesh *m, m2p_counts *c, m2p_err *e)
{
    uint32_t *map = (uint32_t *)malloc((size_t)(m->nv ? m->nv : 1) * sizeof(uint32_t));
    uint64_t i, kept = 0;

    if (map == NULL)
        return m2p_fail(e, M2P_EXIT_IO, "out of memory");
    for (i = 0; i < m->nv; i++)
        map[i] = EMPTY;
    for (i = 0; i < m->nidx; i++)
        map[m->idx[i]] = 0;
    for (i = 0; i < m->nv; i++)
    {
        if (map[i] == EMPTY)
            continue;
        if (kept != i)
        {
            memcpy(m->pos + kept * 3, m->pos + i * 3, 3 * sizeof(float));
            if (m->nrm != NULL)
                memcpy(m->nrm + kept * 3, m->nrm + i * 3, 3 * sizeof(float));
            if (m->col != NULL)
                memcpy(m->col + kept * 4, m->col + i * 4, 4);
        }
        map[i] = (uint32_t)kept++;
    }
    for (i = 0; i < m->nidx; i++)
        m->idx[i] = map[m->idx[i]];
    c->unreferenced = m->nv - kept;
    m->nv = kept;
    free(map);
    return M2P_OK;
}
