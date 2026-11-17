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

/* ply_colourise: declarations shared by its source files. The PLY data is
   read and written by mesh2ply's reader and writer (m2p.h), whose error
   structure, m2p_err, carries this tool's exit codes as well. */

#ifndef PLYC_H
#define PLYC_H

#include <stddef.h>
#include <stdint.h>

#include "m2p.h"

/* The version of the colourise specification this program implements */
#define PLYC_SPEC_VERSION "0.4"

/* Exit codes (specification section 9). Exit 1 is not in the
   specification: it is a failure to write the output or the report. */
enum
{
    PLYC_OK = 0,
    PLYC_EXIT_WRITE = 1,
    PLYC_EXIT_USAGE = 2,
    PLYC_EXIT_READ = 3,
    PLYC_EXIT_LAYOUT = 4,
    PLYC_EXIT_INVALID = 5
};

/* What the header of an input in the mesh2ply layout declares */
typedef struct
{
    uint64_t nv, nf;
    int normals;        /* float nx ny nz */
    int colour;         /* uchar red green blue */
    int alpha;          /* uchar alpha, after the colour */
} plyc_layout;

/* plyc_layout.c: checks that the file is exactly in the mesh2ply layout and
   that its size is what the header and the face counts imply */
int plyc_check_layout(const char *path, plyc_layout *l, m2p_err *e);

/* plyc_field.c: Moreland's Smooth Cool Warm table and the lookup */
extern const unsigned char plyc_smooth_cool_warm[256][3];
int plyc_table_index(double v, double vmin, double span);

/* plyc_sha256.c */
typedef struct
{
    uint32_t h[8];
    unsigned char block[64];
    size_t fill;
    uint64_t bytes;
} plyc_sha256;

void plyc_sha256_init(plyc_sha256 *s);
void plyc_sha256_update(plyc_sha256 *s, const unsigned char *data, size_t n);
void plyc_sha256_final(plyc_sha256 *s, unsigned char digest[32]);
int plyc_sha256_file(const char *path, char hex[65]);

/* plyc_text.c: a double as Python's repr() writes it */
void plyc_repr(double v, char out[40]);

/* plyc_path.c: whether two paths name the same file */
int plyc_same_file(const char *a, const char *b);

#endif
