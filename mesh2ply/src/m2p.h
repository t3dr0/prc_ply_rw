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

/* mesh2ply: the in-memory mesh every reader fills, and the declarations of
   the readers, the clean-up steps and the writer. Readers know nothing about
   cleaning or output; the clean-up and the writer see only this structure. */

#ifndef M2P_H
#define M2P_H

#include <stddef.h>
#include <stdint.h>

#define M2P_VERSION "0.1.0"

/* Exit codes (specification section 4) */
enum
{
    M2P_OK = 0,
    M2P_EXIT_USAGE = 1,
    M2P_EXIT_IO = 2,
    M2P_EXIT_PARSE = 3,
    M2P_EXIT_INVALID = 4,
    M2P_EXIT_UNSUPPORTED = 5
};

/* An index that names no vertex: negative, or not below the vertex count.
   No valid index can take this value, as at most 2^31-1 vertices are kept. */
#define M2P_BAD_INDEX 0xFFFFFFFFu
#define M2P_MAX_VERTICES 2147483647u

typedef struct
{
    char *data;      /* NUL-terminated, or NULL while empty */
    size_t len, cap;
} m2p_strbuf;

typedef struct
{
    int code;        /* the exit code the error maps to */
    char msg[512];
} m2p_err;

typedef struct
{
    /* Vertices: positions always, normals and colours when every vertex
       has them. Colours are stored as four bytes; alpha is 255 unless the
       input gave every vertex an alpha. */
    uint64_t nv;
    float *pos;               /* 3 * nv */
    float *nrm;               /* 3 * nv, or NULL */
    unsigned char *col;       /* 4 * nv, or NULL */
    int has_alpha;

    /* Faces: a count per face, and all faces' indices one after another.
       An index that names no vertex is stored as M2P_BAD_INDEX. */
    uint64_t nf;
    uint32_t *fcount;
    uint64_t cap_f;
    uint32_t *idx;
    uint64_t nidx, cap_i;

    /* What the reader saw, for the report */
    char input_format[8];     /* "off" or "ply" */
    char header[64];          /* OFF keyword, or the PLY format name */
    uint64_t vertices_in, faces_in;
    uint32_t max_poly;
    uint64_t dropped_face_colours;
    m2p_strbuf dropped;       /* ';'-separated names of dropped data */
} m2p_mesh;

/* m2p_util.c */
int m2p_fail(m2p_err *e, int code, const char *fmt, ...);
void m2p_mesh_init(m2p_mesh *m);
void m2p_mesh_free(m2p_mesh *m);
int m2p_mesh_alloc_vertices(m2p_mesh *m, uint64_t nv, int normals, int colours, m2p_err *e);
int m2p_mesh_push_face(m2p_mesh *m, uint32_t count, m2p_err *e);
int m2p_mesh_push_index(m2p_mesh *m, uint32_t index, m2p_err *e);
int m2p_strlist_add(m2p_strbuf *s, const char *name);
void m2p_strbuf_free(m2p_strbuf *s);
int m2p_file_size(const char *path, uint64_t *size);
unsigned char m2p_colour_from_float(double v);
unsigned char m2p_colour_from_int(double v);

/* m2p_read_off.c, m2p_read_ply.c */
int m2p_detect_format(const char *path, char fmt[8], m2p_err *e);
int m2p_read_off(const char *path, uint64_t file_size, m2p_mesh *m, m2p_err *e);
int m2p_read_ply(const char *path, uint64_t file_size, m2p_mesh *m, m2p_err *e);

/* m2p_clean.c */
typedef struct
{
    uint64_t invalid_faces;       /* out-of-range index or fewer than 3 vertices */
    uint64_t welded_merged;
    uint64_t degenerate;
    uint64_t duplicate;
    uint64_t unreferenced;
} m2p_counts;

int m2p_check_faces(m2p_mesh *m, int drop_invalid, m2p_counts *c, m2p_err *e);
int m2p_weld(m2p_mesh *m, m2p_counts *c, m2p_err *e);
int m2p_triangulate(m2p_mesh *m, m2p_err *e);
void m2p_drop_degenerate(m2p_mesh *m, m2p_counts *c);
int m2p_drop_duplicates(m2p_mesh *m, m2p_counts *c, m2p_err *e);
int m2p_drop_unreferenced(m2p_mesh *m, m2p_counts *c, m2p_err *e);

/* m2p_write_ply.c */
int m2p_write_ply(const char *path, const m2p_mesh *m, uint64_t *bytes_out, m2p_err *e);

#endif
