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
 * mesh2ply: convert an OFF or PLY mesh to binary little-endian PLY in one
 * fixed layout, losslessly or with a stated, counted clean-up.
 *
 *   mesh2ply [options] INPUT OUTPUT.ply
 *
 * Specification: mesh2ply-SPEC.md, version 0.1 (13 Nov 2026). The README
 * beside this file describes the behaviour for citation; --help gives the
 * same in brief.
 * ==================================================================== */

#include "m2p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const USAGE =
    "usage: mesh2ply [options] INPUT OUTPUT.ply\n"
    "\n"
    "Converts an OFF or PLY mesh (ASCII, or binary of either byte order) to binary\n"
    "little-endian PLY: float x y z, float nx ny nz if every vertex has a normal,\n"
    "uchar red green blue [alpha] if every vertex has a colour (alpha only if every\n"
    "vertex has one), and the faces as a list uchar int vertex_indices. No comments\n"
    "are written, and the bytes are the same on every platform.\n"
    "\n"
    "Without --clean the conversion is lossless: the same vertices and faces in the\n"
    "same order, polygons kept; positions and normals stored as float32, colours as\n"
    "uchar (floats in [0,1] as round(v*255), integers clamped to [0,255]); any other\n"
    "property is dropped and named in the report.\n"
    "\n"
    "Options:\n"
    "  --clean            apply the clean-up below, in this order\n"
    "  --triangulate      fan-triangulate polygons (implied by --clean)\n"
    "  --drop-invalid     drop faces with fewer than 3 vertices or an out-of-range\n"
    "                     index, instead of failing\n"
    "  --no-normals       do not write normals\n"
    "  --no-colour        do not write vertex colours\n"
    "  --input-format off|ply   do not detect the format from the first line\n"
    "  --report FILE.csv  append one row for this run (header if the file is new)\n"
    "  --quiet            no summary line on stderr\n"
    "  --version, --help\n"
    "\n"
    "Clean-up (--clean):\n"
    "  1. Invalid faces. A face with fewer than 3 vertices or an index outside\n"
    "     [0, vertices) fails the run, or with --drop-invalid is dropped and counted.\n"
    "  2. Output precision. Positions and normals become float32 (round to nearest)\n"
    "     and colours uchar; the weld compares these values, as nanoPRC reads them.\n"
    "  3. Exact weld. Vertices whose whole output record (position, and normal and\n"
    "     colour where written) is identical are merged; -0.0 equals +0.0. The first\n"
    "     keeps its place. Same position with a different normal or colour: kept.\n"
    "  4. Fan triangulation. Polygon v0 v1 ... vn-1 becomes (v0, vk, vk+1) for\n"
    "     k = 1 ... n-2, in order.\n"
    "  5. Degenerate triangles. A triangle with a repeated index is dropped.\n"
    "  6. Duplicate triangles. A triangle whose three indices, as a set, were seen\n"
    "     before is dropped, whatever its orientation; the first is kept.\n"
    "  7. Unreferenced vertices. Vertices no triangle uses are removed; the rest are\n"
    "     renumbered in their order.\n"
    "  Nothing else: no re-orientation, no tolerance, no repair of non-manifold edges.\n"
    "\n"
    "Exit codes: 0 done; 1 usage; 2 I/O error; 3 parse error; 4 invalid mesh (bad face\n"
    "without --drop-invalid, NaN or infinite value, no vertices or faces); 5 not\n"
    "supported (4D or nD OFF, a polygon of more than 255 vertices without\n"
    "--triangulate, more than 2^31-1 vertices). Nothing is written unless the run\n"
    "succeeds: the output goes to a temporary file that is renamed at the end.\n";

typedef struct
{
    const char *input, *output, *report, *format;
    int clean, triangulate, drop_invalid, no_normals, no_colour, quiet;
} options;

/* What the report and the summary line say. An empty field is a count that
   does not apply, or that the run did not reach. */
typedef struct
{
    int read_done, check_done, clean_done, write_done;
    uint64_t bytes_in, bytes_out;
    uint64_t vertices_out, faces_out, triangles_out;
    int has_normals, has_colour, has_alpha;
    m2p_counts counts;
} result;

static int
run(const options *o, m2p_mesh *m, result *r, m2p_err *e)
{
    char fmt[8];
    char *tmp;
    size_t n;
    int code;
    uint64_t f;

    if (!m2p_file_size(o->input, &r->bytes_in))
        return m2p_fail(e, M2P_EXIT_IO, "cannot read the input");
    if (o->format != NULL)
        strcpy(fmt, o->format);
    else if ((code = m2p_detect_format(o->input, fmt, e)) != M2P_OK)
        return code;
    code = strcmp(fmt, "off") == 0 ? m2p_read_off(o->input, r->bytes_in, m, e)
                                   : m2p_read_ply(o->input, r->bytes_in, m, e);
    if (code != M2P_OK)
        return code;
    r->read_done = 1;

    if (o->no_normals && m->nrm != NULL)
    {
        free(m->nrm);
        m->nrm = NULL;
    }
    if (o->no_colour && m->col != NULL)
    {
        free(m->col);
        m->col = NULL;
        m->has_alpha = 0;
    }
    if (m->nv == 0 || m->nf == 0)
        return m2p_fail(e, M2P_EXIT_INVALID, "the mesh has no vertices or no faces");

    code = m2p_check_faces(m, o->drop_invalid, &r->counts, e);
    r->check_done = 1;
    if (code != M2P_OK)
        return code;

    if (o->clean)
    {
        if ((code = m2p_weld(m, &r->counts, e)) != M2P_OK || (code = m2p_triangulate(m, e)) != M2P_OK)
            return code;
        m2p_drop_degenerate(m, &r->counts);
        if ((code = m2p_drop_duplicates(m, &r->counts, e)) != M2P_OK ||
            (code = m2p_drop_unreferenced(m, &r->counts, e)) != M2P_OK)
            return code;
        r->clean_done = 1;
    }
    else if (o->triangulate && (code = m2p_triangulate(m, e)) != M2P_OK)
        return code;
    if (m->nv == 0 || m->nf == 0)
        return m2p_fail(e, M2P_EXIT_INVALID, "no vertices or no faces are left");

    /* Written to a temporary file beside the output, renamed on success */
    n = strlen(o->output);
    tmp = (char *)malloc(n + 16);
    if (tmp == NULL)
        return m2p_fail(e, M2P_EXIT_IO, "out of memory");
    memcpy(tmp, o->output, n);
    strcpy(tmp + n, ".mesh2ply-tmp");
    remove(tmp);
    code = m2p_write_ply(tmp, m, &r->bytes_out, e);
    if (code == M2P_OK && rename(tmp, o->output) != 0)
    {
        /* rename does not replace an existing file on Windows */
        remove(o->output);
        if (rename(tmp, o->output) != 0)
            code = m2p_fail(e, M2P_EXIT_IO, "cannot rename the output into place");
    }
    if (code != M2P_OK)
        remove(tmp);
    free(tmp);
    if (code != M2P_OK)
        return code;

    r->write_done = 1;
    r->vertices_out = m->nv;
    r->faces_out = m->nf;
    for (f = 0; f < m->nf; f++)
        r->triangles_out += m->fcount[f] - 2;
    r->has_normals = m->nrm != NULL;
    r->has_colour = m->col != NULL;
    r->has_alpha = m->col != NULL && m->has_alpha;
    return M2P_OK;
}

/* One CSV field, quoted when it holds a comma, quote or line end */
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
    fputc(last ? '\n' : ',', f);
}

static const char *
num(char *buf, int known, uint64_t v)
{
    if (!known)
        return "";
    sprintf(buf, "%llu", (unsigned long long)v);
    return buf;
}

static int
write_report(const options *o, const m2p_mesh *m, const result *r, int exit_code)
{
    static const char *const COLUMNS =
        "input,input_format,header,bytes_in,vertices,faces,max_poly,out_of_range_faces,degenerate_tris,"
        "duplicate_tris,welded_vertices_merged,unreferenced_vertices,vertices_out,faces_out,triangles_out,"
        "has_normals,has_colour,has_alpha,dropped_face_colours,dropped_properties,clean,bytes_out,exit_code\n";
    char b[23][32];
    uint64_t size = 0;
    int new_file = !m2p_file_size(o->report, &size) || size == 0;
    FILE *f = fopen(o->report, "ab");
    int rd = r->read_done, cl = r->clean_done, wr = r->write_done;

    if (f == NULL)
        return 0;
    if (new_file)
        fputs(COLUMNS, f);
    csv_field(f, o->input, 0);
    csv_field(f, m->input_format, 0);
    csv_field(f, m->header, 0);
    csv_field(f, num(b[0], r->bytes_in > 0, r->bytes_in), 0);
    csv_field(f, num(b[1], rd, m->vertices_in), 0);
    csv_field(f, num(b[2], rd, m->faces_in), 0);
    csv_field(f, num(b[3], rd, m->max_poly), 0);
    csv_field(f, num(b[4], r->check_done, r->counts.invalid_faces), 0);
    csv_field(f, num(b[5], cl, r->counts.degenerate), 0);
    csv_field(f, num(b[6], cl, r->counts.duplicate), 0);
    csv_field(f, num(b[7], cl, r->counts.welded_merged), 0);
    csv_field(f, num(b[8], cl, r->counts.unreferenced), 0);
    csv_field(f, num(b[9], wr, r->vertices_out), 0);
    csv_field(f, num(b[10], wr, r->faces_out), 0);
    csv_field(f, num(b[11], wr, r->triangles_out), 0);
    csv_field(f, num(b[12], wr, (uint64_t)r->has_normals), 0);
    csv_field(f, num(b[13], wr, (uint64_t)r->has_colour), 0);
    csv_field(f, num(b[14], wr, (uint64_t)r->has_alpha), 0);
    csv_field(f, num(b[15], rd, m->dropped_face_colours), 0);
    csv_field(f, rd && m->dropped.data != NULL ? m->dropped.data : "", 0);
    csv_field(f, o->clean ? "1" : "0", 0);
    csv_field(f, num(b[16], wr, r->bytes_out), 0);
    csv_field(f, num(b[17], 1, (uint64_t)exit_code), 1);
    return fclose(f) == 0;
}

int
main(int argc, char **argv)
{
    options o;
    m2p_mesh m;
    m2p_err e;
    result r;
    int i, code;

    memset(&o, 0, sizeof(o));
    for (i = 1; i < argc; i++)
    {
        const char *a = argv[i];

        if (strcmp(a, "--help") == 0)
        {
            fputs(USAGE, stdout);
            return M2P_OK;
        }
        if (strcmp(a, "--version") == 0)
        {
            printf("mesh2ply %s\n", M2P_VERSION);
            return M2P_OK;
        }
        if (strcmp(a, "--clean") == 0)
            o.clean = o.triangulate = 1;
        else if (strcmp(a, "--triangulate") == 0)
            o.triangulate = 1;
        else if (strcmp(a, "--drop-invalid") == 0)
            o.drop_invalid = 1;
        else if (strcmp(a, "--no-normals") == 0)
            o.no_normals = 1;
        else if (strcmp(a, "--no-colour") == 0)
            o.no_colour = 1;
        else if (strcmp(a, "--quiet") == 0)
            o.quiet = 1;
        else if (strcmp(a, "--report") == 0 && i + 1 < argc)
            o.report = argv[++i];
        else if (strcmp(a, "--input-format") == 0 && i + 1 < argc &&
                 (strcmp(argv[i + 1], "off") == 0 || strcmp(argv[i + 1], "ply") == 0))
            o.format = argv[++i];
        else if (a[0] == '-' && a[1] == '-')
        {
            fprintf(stderr, "mesh2ply: unknown or incomplete option %s\n%s", a, USAGE);
            return M2P_EXIT_USAGE;
        }
        else if (o.input == NULL)
            o.input = a;
        else if (o.output == NULL)
            o.output = a;
        else
        {
            fprintf(stderr, "mesh2ply: one input and one output only\n%s", USAGE);
            return M2P_EXIT_USAGE;
        }
    }
    if (o.input == NULL || o.output == NULL)
    {
        fputs(USAGE, stderr);
        return M2P_EXIT_USAGE;
    }

    m2p_mesh_init(&m);
    memset(&e, 0, sizeof(e));
    memset(&r, 0, sizeof(r));
    code = run(&o, &m, &r, &e);

    if (code != M2P_OK)
        fprintf(stderr, "mesh2ply: %s: %s\n", o.input, e.msg);
    else if (!o.quiet)
    {
        fprintf(stderr, "mesh2ply: %s: %llu vertices, %llu faces -> %llu vertices, %llu faces",
                o.input, (unsigned long long)m.vertices_in, (unsigned long long)m.faces_in,
                (unsigned long long)r.vertices_out, (unsigned long long)r.faces_out);
        if (o.drop_invalid)
            fprintf(stderr, "; dropped %llu invalid faces", (unsigned long long)r.counts.invalid_faces);
        if (o.clean)
            fprintf(stderr, "; welded %llu, degenerate %llu, duplicate %llu, unreferenced %llu",
                    (unsigned long long)r.counts.welded_merged, (unsigned long long)r.counts.degenerate,
                    (unsigned long long)r.counts.duplicate, (unsigned long long)r.counts.unreferenced);
        fputc('\n', stderr);
    }
    if (o.report != NULL && !write_report(&o, &m, &r, code))
    {
        fprintf(stderr, "mesh2ply: cannot write the report %s\n", o.report);
        if (code == M2P_OK)
            code = M2P_EXIT_IO;
    }
    m2p_mesh_free(&m);
    return code;
}
