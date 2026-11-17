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

/* ====================================================================
 * ply_colourise: add one synthetic colour to every vertex of a binary PLY
 * in mesh2ply's layout, from the vertex's coordinate along one axis looked
 * up in Moreland's Smooth Cool Warm colour map.
 *
 *   ply_colourise [options] INPUT.ply OUTPUT.ply
 *
 * Specification: colourise-SPEC.md, version 0.4 (17 Nov 2026), which also
 * describes colourise.py, the reference implementation in Python; with the
 * default axis this program writes the same bytes. The README beside this
 * file describes the behaviour; --help gives the same in brief.
 *
 * The PLY is read and written by mesh2ply's reader and writer (RPly
 * underneath), after this program has checked that the input is exactly in
 * mesh2ply's layout.
 * ==================================================================== */

#include "plyc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const USAGE =
    "usage: ply_colourise [options] INPUT.ply OUTPUT.ply\n"
    "\n"
    "Adds one colour to every vertex of a binary PLY in mesh2ply's layout: the\n"
    "vertex's coordinate along one axis (y unless --axis says otherwise), looked\n"
    "up in Moreland's Smooth Cool Warm colour map (256-entry table). With min and\n"
    "max the smallest and largest coordinate on that axis over all vertices,\n"
    "  t = (v - min) / (max - min), clamped to [0, 1]; t = 0 if max = min,\n"
    "and the vertex takes table row floor(t * 255 + 0.5), clamped to [0, 255].\n"
    "Arithmetic is in double precision, from the float32 values in the file.\n"
    "\n"
    "Positions, normals and faces are copied unchanged and in order. The output\n"
    "has uchar red green blue after the normals (or after z), and no alpha.\n"
    "\n"
    "The input must be exactly mesh2ply's layout: binary_little_endian 1.0, no\n"
    "comment or obj_info lines, elements vertex then face, vertex properties\n"
    "float x y z, then optionally float nx ny nz, then optionally uchar red green\n"
    "blue and optionally uchar alpha, and faces as list uchar int vertex_indices.\n"
    "Convert anything else with mesh2ply first.\n"
    "\n"
    "Options:\n"
    "  --axis x|y|z       the coordinate that sets the colour (default y)\n"
    "  --replace          replace the input's vertex colours (and drop its alpha);\n"
    "                     without it, a coloured input is refused\n"
    "  --report FILE.csv  append one row for this run (header if the file is new)\n"
    "  --quiet            no summary line on stderr\n"
    "  --version, --help\n"
    "\n"
    "Exit codes: 0 done; 1 cannot write the output or the report; 2 usage (bad\n"
    "option, INPUT and OUTPUT the same file); 3 cannot read the input, or it is\n"
    "truncated; 4 not mesh2ply's layout, a face index out of range, or already\n"
    "coloured without --replace; 5 a NaN or infinite position or normal. Nothing\n"
    "is written unless the run succeeds: the output goes to a temporary file that\n"
    "is renamed at the end.\n";

typedef struct
{
    const char *input, *output, *report;
    int axis;              /* 0, 1 or 2 */
    int replace, quiet;
} options;

/* What the report and the summary line say. An empty field is one the run
   did not reach. */
typedef struct
{
    int read_done, write_done;
    uint64_t bytes_in, bytes_out;
    int replaced;
    uint64_t vertices, faces;
    double vmin, vmax;
    unsigned distinct;
    char sha256[65];
} result;

static const char AXIS_NAME[3] = { 'x', 'y', 'z' };

/* mesh2ply's exit codes, as this program's */
static int
from_m2p(m2p_err *e)
{
    switch (e->code)
    {
    case M2P_EXIT_INVALID: e->code = PLYC_EXIT_INVALID; break;
    case M2P_EXIT_UNSUPPORTED: e->code = PLYC_EXIT_LAYOUT; break;
    default: e->code = PLYC_EXIT_READ; break;
    }
    return e->code;
}

static int
run(const options *o, m2p_mesh *m, result *r, m2p_err *e)
{
    plyc_layout l;
    unsigned char used[256];
    uint64_t v, i;
    double span;
    char *tmp;
    size_t n;
    int code;

    if (plyc_same_file(o->input, o->output))
        return m2p_fail(e, PLYC_EXIT_USAGE, "INPUT and OUTPUT are the same file");
    if (!m2p_file_size(o->input, &r->bytes_in))
        return m2p_fail(e, PLYC_EXIT_READ, "cannot read the input");
    r->read_done = 1;
    if ((code = plyc_check_layout(o->input, &l, e)) != PLYC_OK)
        return code;
    if (l.colour && !o->replace)
        return m2p_fail(e, PLYC_EXIT_LAYOUT, "the input already has vertex colours (use --replace)");
    if (m2p_read_ply(o->input, r->bytes_in, m, e) != M2P_OK)
        return from_m2p(e);
    for (i = 0; i < m->nidx; i++)
        if (m->idx[i] == M2P_BAD_INDEX)
            return m2p_fail(e, PLYC_EXIT_LAYOUT, "a face index is negative or not below the vertex count");

    /* The field (section 5): min and max as Python's min() and max() find
       them, so that among equal values (0.0 and -0.0) the first is kept */
    r->vmin = r->vmax = 0.0;
    for (v = 0; v < m->nv; v++)
    {
        double c = m->pos[v * 3 + (uint64_t)o->axis];

        if (v == 0 || c < r->vmin)
            r->vmin = c;
        if (v == 0 || c > r->vmax)
            r->vmax = c;
    }
    span = r->vmax - r->vmin;

    /* The colours replace any the input had, and alpha is dropped. The
       writer leaves colours out when there is no array, so an empty mesh
       still gets one, to write the colour properties as the layout has. */
    if (m->col == NULL)
    {
        m->col = (unsigned char *)malloc(m->nv > 0 ? (size_t)m->nv * 4 : 4);
        if (m->col == NULL)
            return m2p_fail(e, PLYC_EXIT_WRITE, "out of memory");
    }
    m->has_alpha = 0;
    memset(used, 0, sizeof(used));
    for (v = 0; v < m->nv; v++)
    {
        int row = plyc_table_index(m->pos[v * 3 + (uint64_t)o->axis], r->vmin, span);

        used[row] = 1;
        memcpy(m->col + v * 4, plyc_smooth_cool_warm[row], 3);
        m->col[v * 4 + 3] = 255;
    }
    r->distinct = 0;
    for (i = 0; i < 256; i++)
        r->distinct += used[i];

    /* Written to a temporary file beside the output, renamed on success */
    n = strlen(o->output);
    tmp = (char *)malloc(n + 24);
    if (tmp == NULL)
        return m2p_fail(e, PLYC_EXIT_WRITE, "out of memory");
    memcpy(tmp, o->output, n);
    strcpy(tmp + n, ".ply_colourise-tmp");
    remove(tmp);
    code = m2p_write_ply(tmp, m, &r->bytes_out, e);
    if (code != M2P_OK)
        code = m2p_fail(e, PLYC_EXIT_WRITE, "%s", e->msg);
    else if (!plyc_sha256_file(tmp, r->sha256))
        code = m2p_fail(e, PLYC_EXIT_WRITE, "cannot read back the output");
    if (code == PLYC_OK && rename(tmp, o->output) != 0)
    {
        /* rename does not replace an existing file on Windows */
        remove(o->output);
        if (rename(tmp, o->output) != 0)
            code = m2p_fail(e, PLYC_EXIT_WRITE, "cannot rename the output into place");
    }
    if (code != PLYC_OK)
        remove(tmp);
    free(tmp);
    if (code != PLYC_OK)
        return code;

    r->write_done = 1;
    r->replaced = l.colour;
    r->vertices = m->nv;
    r->faces = m->nf;
    return PLYC_OK;
}

/* One CSV field, quoted when it holds a comma, quote or line end, as
   Python's csv module writes it */
static void
csv_field(FILE *f, const char *s, int last)
{
    if (strpbrk(s, ",\"\r\n") != NULL)
    {
        fputc('"', f);
        for (; *s != '\0'; s++)
        {
            if (*s == '"')
                fputc('"', f);
            fputc(*s, f);
        }
        fputc('"', f);
    }
    else
        fputs(s, f);
    fputs(last ? "\r\n" : ",", f);
}

static const char *
num(char *buf, int known, uint64_t v)
{
    if (!known)
        return "";
    sprintf(buf, "%llu", (unsigned long long)v);
    return buf;
}

/* Section 8. Rows end in CR LF, as Python's csv module writes them. */
static int
write_report(const options *o, const result *r, int exit_code)
{
    static const char *const COLUMNS =
        "input,output,spec_version,axis,replaced,vertices,faces,min,max,distinct_colours,"
        "bytes_in,bytes_out,sha256_out,exit_code\r\n";
    char b[8][32], vmin[40], vmax[40], axis[2];
    uint64_t size = 0;
    int new_file = !m2p_file_size(o->report, &size) || size == 0;
    FILE *f = fopen(o->report, "ab");
    int wr = r->write_done;

    if (f == NULL)
        return 0;
    if (new_file)
        fputs(COLUMNS, f);
    plyc_repr(r->vmin, vmin);
    plyc_repr(r->vmax, vmax);
    axis[0] = AXIS_NAME[o->axis];
    axis[1] = '\0';
    csv_field(f, o->input, 0);
    csv_field(f, o->output, 0);
    csv_field(f, PLYC_SPEC_VERSION, 0);
    csv_field(f, axis, 0);
    csv_field(f, num(b[0], wr, (uint64_t)r->replaced), 0);
    csv_field(f, num(b[1], wr, r->vertices), 0);
    csv_field(f, num(b[2], wr, r->faces), 0);
    csv_field(f, wr ? vmin : "", 0);
    csv_field(f, wr ? vmax : "", 0);
    csv_field(f, num(b[3], wr, r->distinct), 0);
    csv_field(f, num(b[4], r->read_done, r->bytes_in), 0);
    csv_field(f, num(b[5], wr, r->bytes_out), 0);
    csv_field(f, wr ? r->sha256 : "", 0);
    csv_field(f, num(b[6], 1, (uint64_t)exit_code), 1);
    return fclose(f) == 0;
}

/* --name VALUE or --name=VALUE; NULL if a is not that option */
static const char *
option_value(int argc, char **argv, int *i, const char *name)
{
    size_t n = strlen(name);
    const char *a = argv[*i];

    if (a == NULL || strncmp(a, name, n) != 0)
        return NULL;
    if (a[n] == '=')
        return a + n + 1;
    if (a[n] != '\0')
        return NULL;
    if (*i + 1 >= argc)
        return "";
    return argv[++*i];
}

static int
usage_error(const char *what, const char *arg)
{
    fprintf(stderr, "ply_colourise: %s%s\n%s", what, arg, USAGE);
    return PLYC_EXIT_USAGE;
}

int
main(int argc, char **argv)
{
    options o;
    m2p_mesh m;
    m2p_err e;
    result r;
    int i, code, only_files = 0;

    memset(&o, 0, sizeof(o));
    o.axis = 1;
    for (i = 1; i < argc; i++)
    {
        const char *a = argv[i], *val;

        if (!only_files && a[0] == '-' && a[1] != '\0')
        {
            if (strcmp(a, "--") == 0)
                only_files = 1;
            else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0)
            {
                fputs(USAGE, stdout);
                return PLYC_OK;
            }
            else if (strcmp(a, "--version") == 0)
            {
                printf("ply_colourise (colourise specification %s)\n", PLYC_SPEC_VERSION);
                return PLYC_OK;
            }
            else if (strcmp(a, "--replace") == 0)
                o.replace = 1;
            else if (strcmp(a, "--quiet") == 0)
                o.quiet = 1;
            else if ((val = option_value(argc, argv, &i, "--axis")) != NULL)
            {
                if (strcmp(val, "x") == 0)
                    o.axis = 0;
                else if (strcmp(val, "y") == 0)
                    o.axis = 1;
                else if (strcmp(val, "z") == 0)
                    o.axis = 2;
                else
                    return usage_error("--axis takes x, y or z, not ", *val ? val : "nothing");
            }
            else if ((val = option_value(argc, argv, &i, "--report")) != NULL)
            {
                if (*val == '\0')
                    return usage_error("--report needs a file name", "");
                o.report = val;
            }
            else
                return usage_error("unknown option ", a);
        }
        else if (o.input == NULL)
            o.input = a;
        else if (o.output == NULL)
            o.output = a;
        else
            return usage_error("one input and one output only; extra argument ", a);
    }
    if (o.input == NULL || o.output == NULL)
        return usage_error("INPUT.ply and OUTPUT.ply are both needed", "");

    m2p_mesh_init(&m);
    memset(&e, 0, sizeof(e));
    memset(&r, 0, sizeof(r));
    code = run(&o, &m, &r, &e);

    if (code != PLYC_OK)
        fprintf(stderr, "ply_colourise: %s: %s\n", o.input, e.msg);
    else if (!o.quiet)
    {
        char vmin[40], vmax[40];

        plyc_repr(r.vmin, vmin);
        plyc_repr(r.vmax, vmax);
        fprintf(stderr, "%s: %llu vertices, %c %s to %s, %u distinct colours -> %s\n", o.input,
                (unsigned long long)r.vertices, AXIS_NAME[o.axis], vmin, vmax, r.distinct, o.output);
    }
    if (o.report != NULL && !write_report(&o, &r, code))
    {
        fprintf(stderr, "ply_colourise: cannot write the report %s\n", o.report);
        if (code == PLYC_OK)
            code = PLYC_EXIT_WRITE;
    }
    m2p_mesh_free(&m);
    return code;
}
