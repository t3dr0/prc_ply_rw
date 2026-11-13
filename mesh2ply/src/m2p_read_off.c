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

/* mesh2ply: format detection, and the OFF reader (specification 5.1, 5.2).

   OFF is read line by line: one vertex or one face per line, '#' starting a
   comment that runs to the end of the line, blank lines ignored. The header
   keyword is [ST][C][N][4][n]OFF; the counts "nv nf ne" follow it on the
   same line (as ModelNet writes, even with no space: "OFF1234 5678 0") or
   on the next line. */

#include "m2p.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *s;
    size_t n;
} tok;

typedef struct
{
    const char *buf;
    size_t len, pos;
    uint64_t lineno;     /* of the line last tokenised, from 1 */
    tok *t;
    size_t nt, cap;
} lexer;

static int
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

/* Tokenises the next line that has anything on it once its comment is cut
   off. Returns 1 for a line, 0 at the end of the input, -1 out of memory. */
static int
lex_line(lexer *L)
{
    while (L->pos < L->len)
    {
        size_t start = L->pos, end, i;
        const char *nl = (const char *)memchr(L->buf + start, '\n', L->len - start);
        const char *hash;

        end = nl ? (size_t)(nl - L->buf) : L->len;
        L->pos = nl ? end + 1 : L->len;
        L->lineno++;
        hash = (const char *)memchr(L->buf + start, '#', end - start);
        if (hash != NULL)
            end = (size_t)(hash - L->buf);

        L->nt = 0;
        i = start;
        while (i < end)
        {
            size_t j;

            while (i < end && is_space(L->buf[i]))
                i++;
            if (i >= end)
                break;
            j = i;
            while (j < end && !is_space(L->buf[j]))
                j++;
            if (L->nt == L->cap)
            {
                size_t cap = L->cap ? L->cap * 2 : 64;
                tok *t = (tok *)realloc(L->t, cap * sizeof(tok));

                if (t == NULL)
                    return -1;
                L->t = t;
                L->cap = cap;
            }
            L->t[L->nt].s = L->buf + i;
            L->t[L->nt].n = j - i;
            L->nt++;
            i = j;
        }
        if (L->nt > 0)
            return 1;
    }
    return 0;
}

/* A whole token as a double. Returns 0 if it is not a number. */
static int
tok_double(tok t, double *out)
{
    char tmp[128];
    char *end;

    if (t.n == 0 || t.n >= sizeof(tmp))
        return 0;
    memcpy(tmp, t.s, t.n);
    tmp[t.n] = '\0';
    *out = strtod(tmp, &end);
    return *end == '\0';
}

/* A whole token as an integer, optionally signed. *big is set when it does
   not fit (it is then no valid count or index). Returns 0 if not an integer. */
static int
tok_int(tok t, int64_t *out, int *big)
{
    size_t i = 0;
    int neg = 0;
    uint64_t v = 0;

    *big = 0;
    if (t.n > 0 && (t.s[0] == '-' || t.s[0] == '+'))
    {
        neg = t.s[0] == '-';
        i = 1;
    }
    if (i >= t.n)
        return 0;
    for (; i < t.n; i++)
    {
        if (t.s[i] < '0' || t.s[i] > '9')
            return 0;
        if (v > (UINT64_C(1) << 62))
            *big = 1;
        else
            v = v * 10 + (uint64_t)(t.s[i] - '0');
    }
    *out = neg ? -(int64_t)v : (int64_t)v;
    return 1;
}

static int
tok_is_float_text(tok t)
{
    size_t i;

    for (i = 0; i < t.n; i++)
        if (t.s[i] == '.' || t.s[i] == 'e' || t.s[i] == 'E')
            return 1;
    return 0;
}

/* The header keyword's flags; returns the length of the keyword (through
   "OFF") or 0 if the token does not start with one. */
static size_t
off_keyword(const char *p, size_t n, int *st, int *c, int *nrm, int *four, int *ndim)
{
    size_t k = 0;

    *st = *c = *nrm = *four = *ndim = 0;
    if (n - k >= 2 && p[k] == 'S' && p[k + 1] == 'T')
    {
        *st = 1;
        k += 2;
    }
    if (k < n && p[k] == 'C')
    {
        *c = 1;
        k++;
    }
    if (k < n && p[k] == 'N')
    {
        *nrm = 1;
        k++;
    }
    if (k < n && p[k] == '4')
    {
        *four = 1;
        k++;
    }
    if (k < n && p[k] == 'n')
    {
        *ndim = 1;
        k++;
    }
    if (n - k < 3 || memcmp(p + k, "OFF", 3) != 0)
        return 0;
    return k + 3;
}

int
m2p_detect_format(const char *path, char fmt[8], m2p_err *e)
{
    FILE *f = fopen(path, "rb");
    char line[4096];
    int st, c, nrm, four, ndim;

    if (f == NULL)
        return m2p_fail(e, M2P_EXIT_IO, "cannot open the input");
    /* The first line with anything on it, comments cut off */
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *p = line, *hash = strchr(line, '#');
        size_t n;

        if (hash != NULL)
            *hash = '\0';
        while (*p != '\0' && (is_space(*p) || *p == '\n'))
            p++;
        n = strlen(p);
        while (n > 0 && (is_space(p[n - 1]) || p[n - 1] == '\n'))
            n--;
        if (n == 0)
            continue;
        fclose(f);
        if (n == 3 && memcmp(p, "ply", 3) == 0)
        {
            strcpy(fmt, "ply");
            return M2P_OK;
        }
        if (off_keyword(p, n, &st, &c, &nrm, &four, &ndim) > 0)
        {
            strcpy(fmt, "off");
            return M2P_OK;
        }
        return m2p_fail(e, M2P_EXIT_PARSE, "not OFF or PLY (give --input-format to override)");
    }
    fclose(f);
    return m2p_fail(e, M2P_EXIT_PARSE, "the input is empty");
}

int
m2p_read_off(const char *path, uint64_t file_size, m2p_mesh *m, m2p_err *e)
{
    FILE *f;
    char *buf = NULL;
    lexer L;
    int st, cflag, nflag, four, ndim, r, code = M2P_OK;
    size_t kw;
    tok counts[3];
    size_t ncounts = 0, i;
    uint64_t nv = 0, nf = 0, v, fi, remaining, alpha_vertices = 0;
    double *craw = NULL;
    unsigned char *ncomp = NULL;
    int colours_are_float = 0;

    memset(&L, 0, sizeof(L));
    strcpy(m->input_format, "off");
    if (file_size > (uint64_t)SIZE_MAX - 1)
        return m2p_fail(e, M2P_EXIT_UNSUPPORTED, "the input is too large to read");
    buf = (char *)malloc((size_t)file_size + 1);
    if (buf == NULL)
        return m2p_fail(e, M2P_EXIT_IO, "out of memory reading the input");
    f = fopen(path, "rb");
    if (f == NULL || fread(buf, 1, (size_t)file_size, f) != (size_t)file_size)
    {
        if (f != NULL)
            fclose(f);
        free(buf);
        return m2p_fail(e, M2P_EXIT_IO, "cannot read the input");
    }
    fclose(f);
    L.buf = buf;
    L.len = (size_t)file_size;

    /* Header keyword, and the counts if they share its line */
    r = lex_line(&L);
    if (r <= 0)
    {
        code = r < 0 ? m2p_fail(e, M2P_EXIT_IO, "out of memory")
                     : m2p_fail(e, M2P_EXIT_PARSE, "the input is empty");
        goto done;
    }
    kw = off_keyword(L.t[0].s, L.t[0].n, &st, &cflag, &nflag, &four, &ndim);
    if (kw == 0)
    {
        code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: no OFF header", (unsigned long long)L.lineno);
        goto done;
    }
    {
        size_t n = kw < sizeof(m->header) ? kw : sizeof(m->header) - 1;

        memcpy(m->header, L.t[0].s, n);
        m->header[n] = '\0';
    }
    if (four || ndim)
    {
        code = m2p_fail(e, M2P_EXIT_UNSUPPORTED, "%s: 4D and n-dimensional OFF are not supported", m->header);
        goto done;
    }
    if (kw < L.t[0].n)
    {
        counts[ncounts].s = L.t[0].s + kw;
        counts[ncounts].n = L.t[0].n - kw;
        ncounts++;
    }
    for (i = 1; i < L.nt && ncounts < 3; i++)
        counts[ncounts++] = L.t[i];
    if (ncounts < 2)
    {
        /* Counts on the next line */
        r = lex_line(&L);
        if (r <= 0)
        {
            code = r < 0 ? m2p_fail(e, M2P_EXIT_IO, "out of memory")
                         : m2p_fail(e, M2P_EXIT_PARSE, "no counts line");
            goto done;
        }
        ncounts = 0;
        for (i = 0; i < L.nt && ncounts < 3; i++)
            counts[ncounts++] = L.t[i];
        if (ncounts < 2)
        {
            code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: expected \"nv nf ne\"", (unsigned long long)L.lineno);
            goto done;
        }
    }
    {
        int64_t a, b;
        int big_a, big_b;

        if (!tok_int(counts[0], &a, &big_a) || !tok_int(counts[1], &b, &big_b) || a < 0 || b < 0 ||
            big_a || big_b)
        {
            code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: the vertex and face counts are not counts",
                            (unsigned long long)L.lineno);
            goto done;
        }
        nv = (uint64_t)a;
        nf = (uint64_t)b;
    }
    m->vertices_in = nv;
    m->faces_in = nf;

    /* A vertex line takes at least 6 bytes ("0 0 0" and its line end), a
       face line at least 2 ("0" and its line end). Counts the rest of the
       file cannot hold are refused before anything is allocated. */
    remaining = (uint64_t)(L.len - L.pos) + 1;
    if (nv > remaining / 6 || nf > remaining / 2 || nv * 6 + nf * 2 > remaining + 6)
    {
        code = m2p_fail(e, M2P_EXIT_PARSE, "counts %llu %llu cannot fit in a file of %llu bytes",
                        (unsigned long long)nv, (unsigned long long)nf, (unsigned long long)file_size);
        goto done;
    }
    if (nv > M2P_MAX_VERTICES)
    {
        code = m2p_fail(e, M2P_EXIT_UNSUPPORTED, "%llu vertices: at most 2^31-1 can be written",
                        (unsigned long long)nv);
        goto done;
    }
    code = m2p_mesh_alloc_vertices(m, nv, nflag, cflag, e);
    if (code != M2P_OK)
        goto done;
    if (cflag && nv > 0)
    {
        craw = (double *)malloc((size_t)nv * 4 * sizeof(double));
        ncomp = (unsigned char *)malloc((size_t)nv);
        if (craw == NULL || ncomp == NULL)
        {
            code = m2p_fail(e, M2P_EXIT_IO, "out of memory for colours");
            goto done;
        }
    }
    if (st)
        m2p_strlist_add(&m->dropped, "st");

    /* Vertices: x y z, then nx ny nz with N, then the colour with C, then
       s t with ST */
    for (v = 0; v < nv; v++)
    {
        size_t base = 3 + (nflag ? 3 : 0) + (st ? 2 : 0), ncol = 0, k;
        double d;

        r = lex_line(&L);
        if (r <= 0)
        {
            code = r < 0 ? m2p_fail(e, M2P_EXIT_IO, "out of memory")
                         : m2p_fail(e, M2P_EXIT_PARSE, "truncated: %llu of %llu vertex lines",
                                    (unsigned long long)v, (unsigned long long)nv);
            goto done;
        }
        if (cflag)
        {
            ncol = L.nt >= base ? L.nt - base : 0;
            if (ncol != 3 && ncol != 4)
            {
                code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: %u values on a %s vertex line",
                                (unsigned long long)L.lineno, (unsigned)L.nt, m->header);
                goto done;
            }
        }
        else if (L.nt != base)
        {
            code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: %u values on a %s vertex line, expected %u",
                            (unsigned long long)L.lineno, (unsigned)L.nt, m->header, (unsigned)base);
            goto done;
        }
        for (k = 0; k < 3; k++)
        {
            if (!tok_double(L.t[k], &d))
            {
                code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: not a number", (unsigned long long)L.lineno);
                goto done;
            }
            m->pos[v * 3 + k] = (float)d;
            if (!isfinite(m->pos[v * 3 + k]))
            {
                code = m2p_fail(e, M2P_EXIT_INVALID, "line %llu: a coordinate is not finite",
                                (unsigned long long)L.lineno);
                goto done;
            }
        }
        if (nflag)
            for (k = 0; k < 3; k++)
            {
                if (!tok_double(L.t[3 + k], &d))
                {
                    code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: not a number", (unsigned long long)L.lineno);
                    goto done;
                }
                m->nrm[v * 3 + k] = (float)d;
                if (!isfinite(m->nrm[v * 3 + k]))
                {
                    code = m2p_fail(e, M2P_EXIT_INVALID, "line %llu: a normal is not finite",
                                    (unsigned long long)L.lineno);
                    goto done;
                }
            }
        if (cflag)
        {
            size_t first = 3 + (nflag ? 3 : 0);

            for (k = 0; k < ncol; k++)
            {
                if (!tok_double(L.t[first + k], &d) || d != d)
                {
                    code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: not a colour value", (unsigned long long)L.lineno);
                    goto done;
                }
                craw[v * 4 + k] = d;
                if (tok_is_float_text(L.t[first + k]))
                    colours_are_float = 1;
            }
            ncomp[v] = (unsigned char)ncol;
            if (ncol == 4)
                alpha_vertices++;
        }
    }

    /* Faces: n i1 ... in, then an optional colour (3 or 4 values) */
    for (fi = 0; fi < nf; fi++)
    {
        int64_t n, ix;
        int big;
        size_t extra, k;

        r = lex_line(&L);
        if (r <= 0)
        {
            code = r < 0 ? m2p_fail(e, M2P_EXIT_IO, "out of memory")
                         : m2p_fail(e, M2P_EXIT_PARSE, "truncated: %llu of %llu face lines",
                                    (unsigned long long)fi, (unsigned long long)nf);
            goto done;
        }
        if (!tok_int(L.t[0], &n, &big) || n < 0 || big || (uint64_t)n > (uint64_t)(L.nt - 1) ||
            (uint64_t)n > UINT32_MAX)
        {
            code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: not a face line", (unsigned long long)L.lineno);
            goto done;
        }
        extra = L.nt - 1 - (size_t)n;
        if (extra == 3 || extra == 4)
            m->dropped_face_colours++;
        else if (extra != 0)
        {
            code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: %u values after the face's indices",
                            (unsigned long long)L.lineno, (unsigned)extra);
            goto done;
        }
        if ((uint32_t)n > m->max_poly)
            m->max_poly = (uint32_t)n;
        code = m2p_mesh_push_face(m, (uint32_t)n, e);
        if (code != M2P_OK)
            goto done;
        for (k = 1; k <= (size_t)n; k++)
        {
            uint32_t index;

            if (!tok_int(L.t[k], &ix, &big))
            {
                code = m2p_fail(e, M2P_EXIT_PARSE, "line %llu: not a vertex index", (unsigned long long)L.lineno);
                goto done;
            }
            index = (big || ix < 0 || (uint64_t)ix >= nv) ? M2P_BAD_INDEX : (uint32_t)ix;
            code = m2p_mesh_push_index(m, index, e);
            if (code != M2P_OK)
                goto done;
        }
    }

    /* Colours, now that every token has been seen: floats in [0,1] if any
       colour value is written as one, otherwise integers in [0,255] */
    if (cflag)
    {
        m->has_alpha = nv > 0 && alpha_vertices == nv;
        if (alpha_vertices > 0 && !m->has_alpha)
            m2p_strlist_add(&m->dropped, "alpha");
        for (v = 0; v < nv; v++)
        {
            size_t k;

            for (k = 0; k < 4; k++)
            {
                if (k == 3 && !m->has_alpha)
                    m->col[v * 4 + 3] = 255;
                else if (k < ncomp[v])
                    m->col[v * 4 + k] = colours_are_float ? m2p_colour_from_float(craw[v * 4 + k])
                                                          : m2p_colour_from_int(craw[v * 4 + k]);
            }
        }
    }

done:
    free(craw);
    free(ncomp);
    free(L.t);
    free(buf);
    return code;
}
