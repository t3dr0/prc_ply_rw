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

/* ply_colourise: the input check (specification section 7). The input must
   be exactly the layout mesh2ply writes: its header lines, in its order,
   each ending in a line feed, and a file exactly as long as the header, the
   vertex records and the faces it counts. mesh2ply's reader, which reads the
   data afterwards, accepts much more than this, so the check is made here
   first and is the only judge of the layout.

   The first line that differs from the layout is named. A file that ends
   early, in the header or the data, is exit 3; anything else is exit 4. */

#include "plyc.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    FILE *f;
    char line[128];
    int lineno;
    m2p_err *e;
} reader;

/* The next header line, without its line feed. A line too long for the
   layout, or holding a NUL byte, is reported as the line found. */
static int
next_line(reader *r)
{
    size_t n = 0;
    int c, odd = 0;

    r->lineno++;
    while ((c = getc(r->f)) != EOF && c != '\n')
    {
        if (c == '\0')
            odd = 1;
        else if (n + 1 < sizeof(r->line))
            r->line[n++] = (char)c;
        else
            odd = 1;
    }
    r->line[n] = '\0';
    if (c == EOF)
    {
        if (ferror(r->f))
            return m2p_fail(r->e, PLYC_EXIT_READ, "cannot read the input");
        if (r->lineno == 1 && n == 0)
            return m2p_fail(r->e, PLYC_EXIT_READ, "the input is empty");
        return m2p_fail(r->e, PLYC_EXIT_READ, "truncated: the header ends at line %d, before end_header",
                        r->lineno);
    }
    if (odd)
        return m2p_fail(r->e, PLYC_EXIT_LAYOUT, "header line %d: too long, or holds a NUL byte", r->lineno);
    return PLYC_OK;
}

static int
starts_word(const char *line, const char *word)
{
    size_t n = strlen(word);

    return strncmp(line, word, n) == 0 && (line[n] == '\0' || line[n] == ' ' || line[n] == '\t');
}

/* The current line is not the one the layout has here */
static int
mismatch(reader *r, const char *expected)
{
    if (starts_word(r->line, "comment") || starts_word(r->line, "obj_info"))
        return m2p_fail(r->e, PLYC_EXIT_LAYOUT,
                        "header line %d: a %s line; the mesh2ply layout has none (convert with mesh2ply)",
                        r->lineno, starts_word(r->line, "comment") ? "comment" : "obj_info");
    if (r->lineno == 2 && starts_word(r->line, "format"))
        return m2p_fail(r->e, PLYC_EXIT_LAYOUT,
                        "header line 2: \"%s\"; only binary_little_endian 1.0 is accepted (convert with mesh2ply)",
                        r->line);
    return m2p_fail(r->e, PLYC_EXIT_LAYOUT, "header line %d: expected %s, found \"%s\"", r->lineno, expected,
                    r->line);
}

/* Reads the next line, which must be exactly text */
static int
expect(reader *r, const char *text)
{
    char quoted[80];
    int code = next_line(r);

    if (code != PLYC_OK)
        return code;
    if (strcmp(r->line, text) == 0)
        return PLYC_OK;
    sprintf(quoted, "\"%.60s\"", text);
    return mismatch(r, quoted);
}

/* "element <name> <count>", the count in decimal digits only. expected
   names what the layout allows on this line, for the message. */
static int
element(reader *r, const char *name, const char *expected, uint64_t *count)
{
    size_t n = strlen("element ") + strlen(name);
    const char *p;
    uint64_t v = 0;

    if (strncmp(r->line, "element ", 8) != 0 || strncmp(r->line + 8, name, strlen(name)) != 0 ||
        r->line[n] != ' ' || r->line[n + 1] == '\0')
        return mismatch(r, expected);
    for (p = r->line + n + 1; *p != '\0'; p++)
    {
        if (*p < '0' || *p > '9' || v > (UINT64_MAX - 9) / 10)
            return mismatch(r, expected);
        v = v * 10 + (uint64_t)(*p - '0');
    }
    *count = v;
    return PLYC_OK;
}

static int
check_header(reader *r, plyc_layout *l)
{
    static const char *const POS[] = { "property float x", "property float y", "property float z" };
    static const char *const NRM[] = { "property float ny", "property float nz" };
    static const char *const COL[] = { "property uchar green", "property uchar blue" };
    int code, k;

    if ((code = next_line(r)) != PLYC_OK)
        return code;
    if (strcmp(r->line, "ply") != 0)
        return m2p_fail(r->e, PLYC_EXIT_LAYOUT, "not a PLY file (the first line is not \"ply\")");
    if ((code = expect(r, "format binary_little_endian 1.0")) != PLYC_OK)
        return code;
    if ((code = next_line(r)) != PLYC_OK || (code = element(r, "vertex", "\"element vertex <count>\"", &l->nv)) != PLYC_OK)
        return code;
    for (k = 0; k < 3; k++)
        if ((code = expect(r, POS[k])) != PLYC_OK)
            return code;
    if ((code = next_line(r)) != PLYC_OK)
        return code;
    if (strcmp(r->line, "property float nx") == 0)
    {
        l->normals = 1;
        for (k = 0; k < 2; k++)
            if ((code = expect(r, NRM[k])) != PLYC_OK)
                return code;
        if ((code = next_line(r)) != PLYC_OK)
            return code;
    }
    if (strcmp(r->line, "property uchar red") == 0)
    {
        l->colour = 1;
        for (k = 0; k < 2; k++)
            if ((code = expect(r, COL[k])) != PLYC_OK)
                return code;
        if ((code = next_line(r)) != PLYC_OK)
            return code;
        if (strcmp(r->line, "property uchar alpha") == 0)
        {
            l->alpha = 1;
            if ((code = next_line(r)) != PLYC_OK)
                return code;
        }
    }
    if ((code = element(r, "face",
                        l->colour ? (l->alpha ? "\"element face <count>\""
                                              : "\"property uchar alpha\" or \"element face <count>\"")
                        : l->normals ? "\"property uchar red\" or \"element face <count>\""
                                     : "\"property float nx\", \"property uchar red\" or \"element face <count>\"",
                        &l->nf)) != PLYC_OK)
        return code;
    if ((code = expect(r, "property list uchar int vertex_indices")) != PLYC_OK ||
        (code = expect(r, "end_header")) != PLYC_OK)
        return code;
    if (l->nv > M2P_MAX_VERTICES)
        return m2p_fail(r->e, PLYC_EXIT_LAYOUT, "%llu vertices: at most 2^31-1 can be read",
                        (unsigned long long)l->nv);
    return PLYC_OK;
}

/* The data: the vertex records, then each face's count byte and its
   indices, then nothing */
typedef struct
{
    FILE *f;
    unsigned char buf[1 << 16];
    size_t at, len;
} breader;

static int
fill(breader *b)
{
    b->at = 0;
    b->len = fread(b->buf, 1, sizeof(b->buf), b->f);
    return b->len > 0;
}

/* Skips n bytes; 0 if the file ends first */
static int
skip(breader *b, uint64_t n)
{
    while (n > 0)
    {
        size_t have;

        if (b->at == b->len && !fill(b))
            return 0;
        have = b->len - b->at;
        if ((uint64_t)have > n)
            have = (size_t)n;
        b->at += have;
        n -= have;
    }
    return 1;
}

static int
next_byte(breader *b)
{
    if (b->at == b->len && !fill(b))
        return EOF;
    return b->buf[b->at++];
}

int
plyc_check_layout(const char *path, plyc_layout *l, m2p_err *e)
{
    static breader b;
    reader r;
    uint64_t stride, fi, extra = 0;
    int code, c;

    memset(l, 0, sizeof(*l));
    memset(&r, 0, sizeof(r));
    r.e = e;
    r.f = fopen(path, "rb");
    if (r.f == NULL)
        return m2p_fail(e, PLYC_EXIT_READ, "cannot read the input");
    code = check_header(&r, l);
    if (code != PLYC_OK)
    {
        fclose(r.f);
        return code;
    }

    b.f = r.f;
    b.at = b.len = 0;
    stride = 12 + (l->normals ? 12 : 0) + (l->colour ? 3 : 0) + (l->alpha ? 1 : 0);
    if (!skip(&b, l->nv * stride))
        code = m2p_fail(e, PLYC_EXIT_READ, "truncated: the file ends in the vertex data");
    for (fi = 0; code == PLYC_OK && fi < l->nf; fi++)
    {
        c = next_byte(&b);
        if (c == EOF || !skip(&b, (uint64_t)c * 4))
            code = m2p_fail(e, PLYC_EXIT_READ, "truncated: the file ends in face %llu of %llu",
                            (unsigned long long)fi, (unsigned long long)l->nf);
    }
    if (code == PLYC_OK)
    {
        while (next_byte(&b) != EOF)
            extra++;
        if (extra > 0)
            code = m2p_fail(e, PLYC_EXIT_LAYOUT, "%llu bytes follow the last face the header counts",
                            (unsigned long long)extra);
    }
    if (code == PLYC_OK && ferror(r.f))
        code = m2p_fail(e, PLYC_EXIT_READ, "cannot read the input");
    fclose(r.f);
    return code;
}
