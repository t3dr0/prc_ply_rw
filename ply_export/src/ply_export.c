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
 * ply_export: read the tessellated geometry of a PRC file, or of the PRC
 * stream in a 3D PDF, and write it as one PLY file -- binary little-endian
 * by default, or ASCII or binary big-endian -- through nanoPRC's public
 * read API and RPly.
 *
 * Every surface tessellation (PRC_API_TESS_3D and PRC_API_TESS_3D_Compressed)
 * goes into the one mesh: PLY has no parts. Vertices stay shared as the
 * reader gives them, so the file is indexed rather than one vertex per
 * triangle corner.
 *
 * Each vertex carries:
 *   - x, y, z;
 *   - nx, ny, nz, from the file, or for a vertex without one the
 *     area-weighted average of its triangles' normals;
 *   - red, green, blue (and alpha, when any vertex is not opaque), as the
 *     viewer demo shades it: the vertex's own material colour when the
 *     tessellation styles each vertex, otherwise the face's material
 *     diffuse colour times the vertex colour;
 *   - s, t, when any face is textured.
 *
 * Textures are written beside the PLY as PNG, identical images once, and
 * named by "comment TextureFile" header lines, the convention MeshLab reads
 * and writes. With more than one texture each face also carries texnumber,
 * the index of its TextureFile line.
 *
 * Usage:
 *     nano_prc_ply_export <input.pdf|input.prc> <output.ply>
 *                         [--ascii | --big-endian] [--no-normals]
 *                         [--no-colors] [--no-texture]
 *
 * Each tessellation is written wherever a part of the model tree draws
 * it, moved into place by the transforms from the root down to that part;
 * one drawn by no part is reported and not written. Wire and markup
 * tessellations are not exported.
 * ==================================================================== */

#include <prc_api.h>
#include <rply.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define NO_INDEX 0xFFFFFFFFu

/* stb_image_write v1.16 computes image sizes in int without checking them
   (CVE-2026-102805), and a texture's width, height and channels come from
   the PRC file. A texture is written only when they are sensible and the
   image is at most 2^28 bytes, far inside int. */
#define PLY_EXPORT_MAX_TEXTURE_BYTES (1u << 28)

static int
texture_fits_png(uint32_t width, uint32_t height, uint32_t channels)
{
    if (width == 0 || height == 0 || channels < 1 || channels > 4)
        return 0;
    return (uint64_t)width * height * channels <= PLY_EXPORT_MAX_TEXTURE_BYTES;
}

/* -------------------------------------------------------------------- */
/* The mesh being assembled                                              */
/* -------------------------------------------------------------------- */

typedef struct
{
    float   position[3];
    float   normal[3];
    float   uv[2];
    uint8_t color[4];
    uint8_t has_normal;
} out_vertex;

typedef struct
{
    out_vertex *v;
    uint32_t    nv, cap_v;
    uint32_t   *tri;        /* 3 per triangle */
    uint32_t   *texnum;     /* 1 per triangle */
    uint32_t    nt, cap_t;
} out_mesh;

static int
add_vertex(out_mesh *m, const out_vertex *v)
{
    if (m->nv == m->cap_v)
    {
        uint32_t cap = m->cap_v ? m->cap_v * 2 : 1024;
        out_vertex *g = (out_vertex *)realloc(m->v, (size_t)cap * sizeof(out_vertex));

        if (g == NULL || cap < m->cap_v)
            return -1;
        m->v = g;
        m->cap_v = cap;
    }
    m->v[m->nv++] = *v;
    return 0;
}

static int
add_triangle(out_mesh *m, uint32_t a, uint32_t b, uint32_t c, uint32_t texnum)
{
    if (m->nt == m->cap_t)
    {
        uint32_t cap = m->cap_t ? m->cap_t * 2 : 1024;
        uint32_t *g = (uint32_t *)realloc(m->tri, (size_t)cap * 3 * sizeof(uint32_t));
        uint32_t *h;

        if (g == NULL || cap < m->cap_t)
            return -1;
        m->tri = g;
        h = (uint32_t *)realloc(m->texnum, (size_t)cap * sizeof(uint32_t));
        if (h == NULL)
            return -1;
        m->texnum = h;
        m->cap_t = cap;
    }
    m->tri[m->nt * 3] = a;
    m->tri[m->nt * 3 + 1] = b;
    m->tri[m->nt * 3 + 2] = c;
    m->texnum[m->nt] = texnum;
    m->nt++;
    return 0;
}

static uint8_t
to_byte(double c)
{
    if (!(c > 0.0))
        return 0;
    if (c >= 1.0)
        return 255;
    return (uint8_t)(c * 255.0 + 0.5);
}

/* -------------------------------------------------------------------- */
/* Textures                                                              */
/* -------------------------------------------------------------------- */

typedef struct
{
    const unsigned char *data;  /* the reader's pixels, valid until release */
    uint32_t width, height, num_channels;
    char filename[256];         /* as named in the PLY header */
} tracked_texture;

typedef struct
{
    tracked_texture *t;
    uint32_t count, cap;
} texture_list;

/* The TextureFile index of this face's image, writing it as a PNG the first
   time it is seen. NO_INDEX if it could not be written. */
static uint32_t
texture_index(texture_list *tl, const prc_api_texture *tex, const char *stem_path,
    const char *stem_name)
{
    size_t bytes = (size_t)tex->width * tex->height * tex->num_channels;
    char full[600];
    uint32_t i;

    for (i = 0; i < tl->count; i++)
    {
        const tracked_texture *k = &tl->t[i];

        if (k->width == tex->width && k->height == tex->height &&
            k->num_channels == tex->num_channels && memcmp(k->data, tex->data, bytes) == 0)
            return i;
    }
    if (tl->count == tl->cap)
    {
        uint32_t cap = tl->cap ? tl->cap * 2 : 8;
        tracked_texture *g = (tracked_texture *)realloc(tl->t, (size_t)cap * sizeof(*g));

        if (g == NULL)
            return NO_INDEX;
        tl->t = g;
        tl->cap = cap;
    }
    snprintf(full, sizeof(full), "%s_tex_%u.png", stem_path, tl->count);
    if (!texture_fits_png(tex->width, tex->height, tex->num_channels))
    {
        fprintf(stderr, "Warning: texture of %ux%u with %u channels not written (too large, or not "
                "1 to 4 channels); its faces written untextured\n",
                (unsigned)tex->width, (unsigned)tex->height, (unsigned)tex->num_channels);
        return NO_INDEX;
    }
    if (!stbi_write_png(full, (int)tex->width, (int)tex->height, (int)tex->num_channels,
                        tex->data, (int)(tex->width * tex->num_channels)))
    {
        fprintf(stderr, "Warning: could not write texture %s; its faces written untextured\n", full);
        return NO_INDEX;
    }
    tl->t[tl->count].data = tex->data;
    tl->t[tl->count].width = tex->width;
    tl->t[tl->count].height = tex->height;
    tl->t[tl->count].num_channels = tex->num_channels;
    snprintf(tl->t[tl->count].filename, sizeof(tl->t[tl->count].filename),
             "%s_tex_%u.png", stem_name, tl->count);
    return tl->count++;
}

/* -------------------------------------------------------------------- */
/* Where each tessellation is drawn                                      */
/* -------------------------------------------------------------------- */

/* One place a tessellation is drawn: its index and the transform from the
   root of the model tree down to the part that draws it. Geometry is often
   stored around its own origin and placed by these transforms; written
   without them, the parts of an assembly pile up at the origin. */
typedef struct
{
    uint32_t tess;
    prc_api_transform xform;
} tess_instance;

typedef struct
{
    tess_instance *items;
    size_t count, cap;
} instance_list;

static int
add_instance(instance_list *l, uint32_t tess, const prc_api_transform *xform)
{
    if (l->count == l->cap)
    {
        size_t cap = l->cap ? l->cap * 2 : 64;
        tess_instance *g = (tess_instance *)realloc(l->items, cap * sizeof(*g));

        if (g == NULL)
            return -1;
        l->items = g;
        l->cap = cap;
    }
    l->items[l->count].tess = tess;
    l->items[l->count].xform = *xform;
    l->count++;
    return 0;
}

/* PRC_GRAPHICS_Removed (0x2000), as it appears in the high byte of a
   behaviour field, behavior_bit_field2: a product or item that has been
   removed and no longer appears. Adobe Acrobat draws neither it nor anything
   under it (the ten oaks of 3D-PDF-Sample-School in prc-db). */
#define PRC_REMOVED_BYTE2 0x20

/* tess_drawn values: a part draws the tessellation, or only removed ones do */
#define TESS_DRAWN 1
#define TESS_REMOVED_ONLY 2

/* A representation item waiting to be visited, with the transform it is
   placed by (its product's, then the local coordinate systems of the sets
   that hold it), and whether it lies under something removed. */
typedef struct
{
    const prc_api_part *item;
    prc_api_transform xform;
    int removed;
} item_visit;

/* Walks the model tree as demos/stl_export in nanoPRC does, composing each
   product's own placement (prc_api_product::location) after its parents'
   with prc_api_update_transform, and lists every tessellation each part
   draws with that transform. A part's geometry is on the part itself or on
   its representation items, which can nest in sets to a depth the file
   chooses, so those are visited from an explicit stack. An item with a
   local coordinate system (biased_local_coordinate_index) is placed by it
   within its parent's frame, as nanoPRC returns its vertices in that
   system's frame. Tessellations are looked up by part_detail_index, the
   API's numbering, which differs from the file's wherever parts share a
   file tessellation.

   A product or item marked removed is not written, nor anything under it;
   its tessellations are marked TESS_REMOVED_ONLY unless a part that is not
   removed draws them too. */
static int
collect_instances(prc_context *ctx, prc_api_data data, const prc_api_product *node,
    prc_api_transform parent, int removed, uint32_t total_tess, instance_list *out,
    uint8_t *tess_drawn)
{
    prc_api_transform world = parent;
    size_t k;

    if (node->behavior_bit_field2 & PRC_REMOVED_BYTE2)
        removed = 1;
    prc_api_update_transform(ctx, &world, (prc_api_transform *)&node->location);

    if (node->part != NULL)
    {
        item_visit *stack;
        size_t depth = 0, capacity = 16;

        stack = (item_visit *)malloc(capacity * sizeof(*stack));
        if (stack == NULL)
            return -1;
        stack[depth].item = node->part;
        stack[depth].xform = world;
        stack[depth++].removed = removed;
        while (depth > 0)
        {
            item_visit v = stack[--depth];
            const prc_api_part *item = v.item;
            size_t ri;

            if (item->behavior_bit_field2 & PRC_REMOVED_BYTE2)
                v.removed = 1;
            if (item->biased_local_coordinate_index != 0)
            {
                prc_api_transform own;

                if (prc_api_get_local_coordinate_system(ctx, data, item->tess_file_index,
                        item->biased_local_coordinate_index, &own) == 0)
                    prc_api_update_transform(ctx, &v.xform, &own);
            }
            if (item->biased_tess_index > 0 && item->part_detail_index < total_tess)
            {
                if (v.removed)
                {
                    if (tess_drawn[item->part_detail_index] == 0)
                        tess_drawn[item->part_detail_index] = TESS_REMOVED_ONLY;
                }
                else
                {
                    if (add_instance(out, item->part_detail_index, &v.xform) != 0)
                    {
                        free(stack);
                        return -1;
                    }
                    tess_drawn[item->part_detail_index] = TESS_DRAWN;
                }
            }
            for (ri = item->num_rep_items; ri > 0; ri--)
            {
                if (depth == capacity)
                {
                    item_visit *grown = (item_visit *)realloc(stack, capacity * 2 * sizeof(*stack));

                    if (grown == NULL)
                    {
                        free(stack);
                        return -1;
                    }
                    stack = grown;
                    capacity *= 2;
                }
                stack[depth].item = &item->rep_items[ri - 1];
                stack[depth].xform = v.xform;
                stack[depth++].removed = v.removed;
            }
        }
        free(stack);
    }

    for (k = 0; k < node->num_children; k++)
        if (collect_instances(ctx, data, &node->children[k], world, removed, total_tess, out,
                tess_drawn) != 0)
            return -1;
    return 0;
}

/* Moves a normal into place by a transform, renormalised. A normal takes
   the cofactor matrix of the transform's linear (3x3) part: its inverse
   transpose times its determinant, so with the determinant's sign it points
   the same way and stays perpendicular to the surface under non-uniform
   scale and shear too (local coordinate systems do scale unevenly). */
static void
transform_normal(const prc_api_transform *xform, const float in[3], float out[3])
{
    const double *m;
    double c[9], det;
    float len;
    int i;

    if (xform->is_identity)
    {
        out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
        return;
    }
    m = xform->matrix;
    /* c is column-major, as m is */
    c[0] = m[5] * m[10] - m[6] * m[9];
    c[1] = m[6] * m[8] - m[4] * m[10];
    c[2] = m[4] * m[9] - m[5] * m[8];
    c[3] = m[9] * m[2] - m[10] * m[1];
    c[4] = m[10] * m[0] - m[8] * m[2];
    c[5] = m[8] * m[1] - m[9] * m[0];
    c[6] = m[1] * m[6] - m[2] * m[5];
    c[7] = m[2] * m[4] - m[0] * m[6];
    c[8] = m[0] * m[5] - m[1] * m[4];
    det = m[0] * c[0] + m[4] * c[3] + m[8] * c[6];
    if (det < 0.0)
        for (i = 0; i < 9; i++)
            c[i] = -c[i];
    out[0] = (float)(c[0] * in[0] + c[3] * in[1] + c[6] * in[2]);
    out[1] = (float)(c[1] * in[0] + c[4] * in[1] + c[7] * in[2]);
    out[2] = (float)(c[2] * in[0] + c[5] * in[1] + c[8] * in[2]);
    len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 1e-7f)
    {
        out[0] /= len; out[1] /= len; out[2] /= len;
    }
    else
    {
        out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
    }
}

/* 1 if the transform mirrors (its linear part has a negative determinant).
   A mirrored triangle's corners then run the other way round, so they are
   swapped back to keep the face pointing outward. */
static int
transform_mirrors(const prc_api_transform *xform)
{
    const double *m = xform->matrix;
    double det;

    if (xform->is_identity)
        return 0;
    det = m[0] * (m[5] * m[10] - m[9] * m[6])
        - m[4] * (m[1] * m[10] - m[9] * m[2])
        + m[8] * (m[1] * m[6] - m[5] * m[2]);
    return det < 0.0;
}

/* -------------------------------------------------------------------- */
/* Collecting one face's geometry                                        */
/* -------------------------------------------------------------------- */

typedef struct
{
    const prc_api_tess_vertex_buffer *vb;
    uint32_t *remap;                /* buffer vertex -> output vertex */
    const prc_api_material *material; /* or NULL */
    uint8_t   per_vertex_material;
    uint8_t   textured;
    uint32_t  texnum;
    const prc_api_transform *xform; /* where this instance is drawn */
    int       mirror;               /* see transform_mirrors */
} face_source;

/* The output vertex for buffer vertex `i`, adding it the first time. */
static uint32_t
output_vertex(out_mesh *m, face_source *fs, uint32_t i)
{
    const prc_api_vertex *pv;
    out_vertex v;
    double r, g, b, a;

    if (fs->remap[i] != NO_INDEX)
        return fs->remap[i];
    pv = &fs->vb->vertices[i];
    memset(&v, 0, sizeof(v));
    {
        double in[3], p[3];

        in[0] = pv->position[0]; in[1] = pv->position[1]; in[2] = pv->position[2];
        prc_api_transform_point(fs->xform, in, p);
        v.position[0] = (float)p[0];
        v.position[1] = (float)p[1];
        v.position[2] = (float)p[2];
    }
    if (pv->normal_set)
    {
        transform_normal(fs->xform, pv->normal, v.normal);
        v.has_normal = 1;
    }
    /* The reader returns v flipped (1 - v), for an image uploaded top row
       first; the image itself is written as stored. PLY, like OBJ and like
       PRC itself, puts v = 0 at the bottom of the image, so flip it back --
       otherwise the texture comes out upside down. */
    if (fs->textured && pv->uv_set)
    {
        v.uv[0] = pv->uv[0];
        v.uv[1] = 1.0f - pv->uv[1];
    }

    /* As the viewer demo shades it. */
    if (fs->per_vertex_material)
    {
        r = pv->diffuse[0]; g = pv->diffuse[1]; b = pv->diffuse[2];
        a = pv->alpha;
    }
    else
    {
        r = pv->color[0]; g = pv->color[1]; b = pv->color[2];
        a = 1.0;
        if (fs->material != NULL)
        {
            r *= fs->material->diffuse[0];
            g *= fs->material->diffuse[1];
            b *= fs->material->diffuse[2];
            a = fs->material->diffuse_alpha;
        }
    }
    v.color[0] = to_byte(r);
    v.color[1] = to_byte(g);
    v.color[2] = to_byte(b);
    v.color[3] = to_byte(a);

    if (add_vertex(m, &v) != 0)
        return NO_INDEX;
    fs->remap[i] = m->nv - 1;
    return fs->remap[i];
}

static int
emit_triangle(out_mesh *m, face_source *fs, uint32_t i0, uint32_t i1, uint32_t i2,
    uint32_t *skipped)
{
    uint32_t n = (uint32_t)fs->vb->num_vertices, a, b, c;

    if (i0 >= n || i1 >= n || i2 >= n)
    {
        (*skipped)++;
        return 0;
    }
    a = output_vertex(m, fs, i0);
    b = output_vertex(m, fs, i1);
    c = output_vertex(m, fs, i2);
    if (a == NO_INDEX || b == NO_INDEX || c == NO_INDEX)
        return -1;
    if (fs->mirror)
        return add_triangle(m, a, c, b, fs->textured ? fs->texnum : 0);
    return add_triangle(m, a, b, c, fs->textured ? fs->texnum : 0);
}

/* -------------------------------------------------------------------- */
/* Normals for vertices that came without one                            */
/* -------------------------------------------------------------------- */

static void
fill_missing_normals(out_mesh *m)
{
    uint32_t t, k;

    for (t = 0; t < m->nt; t++)
    {
        const uint32_t *tri = &m->tri[t * 3];
        const float *p0 = m->v[tri[0]].position;
        const float *p1 = m->v[tri[1]].position;
        const float *p2 = m->v[tri[2]].position;
        float u[3], w[3], n[3];

        for (k = 0; k < 3; k++)
        {
            u[k] = p1[k] - p0[k];
            w[k] = p2[k] - p0[k];
        }
        /* Unnormalised: its length is twice the area, so the sum below is
           area-weighted. */
        n[0] = u[1] * w[2] - u[2] * w[1];
        n[1] = u[2] * w[0] - u[0] * w[2];
        n[2] = u[0] * w[1] - u[1] * w[0];
        for (k = 0; k < 3; k++)
        {
            out_vertex *v = &m->v[tri[k]];

            if (!v->has_normal)
            {
                v->normal[0] += n[0];
                v->normal[1] += n[1];
                v->normal[2] += n[2];
            }
        }
    }
    for (t = 0; t < m->nv; t++)
    {
        out_vertex *v = &m->v[t];

        if (!v->has_normal)
        {
            float len = sqrtf(v->normal[0] * v->normal[0] + v->normal[1] * v->normal[1] +
                              v->normal[2] * v->normal[2]);

            if (len > 0.0f)
            {
                v->normal[0] /= len;
                v->normal[1] /= len;
                v->normal[2] /= len;
            }
        }
    }
}

/* -------------------------------------------------------------------- */
/* Writing                                                               */
/* -------------------------------------------------------------------- */

static void
error_cb(p_ply ply, const char *message)
{
    (void)ply;
    fprintf(stderr, "PLY: %s\n", message);
}

static int
write_ply(const char *path, e_ply_storage_mode mode, const out_mesh *m,
    const texture_list *tex, int with_normals, int with_colors, int with_alpha)
{
    int with_uv = tex->count > 0;
    int with_texnum = tex->count > 1;
    p_ply ply = ply_create(path, mode, error_cb, 0, NULL);
    uint32_t i, k;
    int ok = 0;

    if (ply == NULL)
        return -1;

    if (!ply_add_comment(ply, "Generated by prc_ply_rw ply_export (c) Ian Curington, Thames 3D Research,") ||
        !ply_add_comment(ply, "using the nanoPRC PRC library (c) CascadiaVoxel LLC. Both AGPLv3."))
        goto out;
    for (i = 0; i < tex->count; i++)
    {
        char line[300];

        snprintf(line, sizeof(line), "TextureFile %s", tex->t[i].filename);
        if (!ply_add_comment(ply, line))
            goto out;
    }

    if (!ply_add_element(ply, "vertex", (long)m->nv) ||
        !ply_add_scalar_property(ply, "x", PLY_FLOAT32) ||
        !ply_add_scalar_property(ply, "y", PLY_FLOAT32) ||
        !ply_add_scalar_property(ply, "z", PLY_FLOAT32))
        goto out;
    if (with_normals &&
        (!ply_add_scalar_property(ply, "nx", PLY_FLOAT32) ||
         !ply_add_scalar_property(ply, "ny", PLY_FLOAT32) ||
         !ply_add_scalar_property(ply, "nz", PLY_FLOAT32)))
        goto out;
    if (with_colors &&
        (!ply_add_scalar_property(ply, "red", PLY_UINT8) ||
         !ply_add_scalar_property(ply, "green", PLY_UINT8) ||
         !ply_add_scalar_property(ply, "blue", PLY_UINT8) ||
         (with_alpha && !ply_add_scalar_property(ply, "alpha", PLY_UINT8))))
        goto out;
    if (with_uv &&
        (!ply_add_scalar_property(ply, "s", PLY_FLOAT32) ||
         !ply_add_scalar_property(ply, "t", PLY_FLOAT32)))
        goto out;

    if (!ply_add_element(ply, "face", (long)m->nt) ||
        !ply_add_list_property(ply, "vertex_indices", PLY_UINT8, PLY_INT32))
        goto out;
    if (with_texnum && !ply_add_scalar_property(ply, "texnumber", PLY_INT32))
        goto out;

    if (!ply_write_header(ply))
        goto out;

    for (i = 0; i < m->nv; i++)
    {
        const out_vertex *v = &m->v[i];

        for (k = 0; k < 3; k++)
            if (!ply_write(ply, v->position[k]))
                goto out;
        if (with_normals)
            for (k = 0; k < 3; k++)
                if (!ply_write(ply, v->normal[k]))
                    goto out;
        if (with_colors)
            for (k = 0; k < (uint32_t)(with_alpha ? 4 : 3); k++)
                if (!ply_write(ply, v->color[k]))
                    goto out;
        if (with_uv)
            for (k = 0; k < 2; k++)
                if (!ply_write(ply, v->uv[k]))
                    goto out;
    }
    for (i = 0; i < m->nt; i++)
    {
        if (!ply_write(ply, 3))
            goto out;
        for (k = 0; k < 3; k++)
            if (!ply_write(ply, m->tri[i * 3 + k]))
                goto out;
        if (with_texnum && !ply_write(ply, m->texnum[i]))
            goto out;
    }
    ok = 1;

out:
    if (!ply_close(ply))
        ok = 0;
    return ok ? 0 : -1;
}

/* The output path without its .ply, and that stem without its directory. */
static void
split_stem(const char *path, char *stem_path, char *stem_name, size_t size)
{
    const char *slash1, *slash2, *slash;
    char *dot;

    strncpy(stem_path, path, size - 1);
    stem_path[size - 1] = '\0';
    dot = strrchr(stem_path, '.');
    slash1 = strrchr(stem_path, '/');
    slash2 = strrchr(stem_path, '\\');
    slash = (slash1 > slash2) ? slash1 : slash2;
    if (dot != NULL && (slash == NULL || dot > slash))
        *dot = '\0';
    strncpy(stem_name, slash ? slash + 1 : stem_path, size - 1);
    stem_name[size - 1] = '\0';
}

static void
usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <input.pdf|input.prc> <output.ply> [options]\n"
        "  Writes the tessellated surfaces as one PLY mesh, binary little-endian\n"
        "  by default, with any textures as PNG files beside it.\n"
        "  --ascii         write ASCII PLY\n"
        "  --big-endian    write binary big-endian PLY\n"
        "  --no-normals    leave out nx, ny, nz\n"
        "  --no-colors     leave out red, green, blue and alpha\n"
        "  --no-texture    leave out s, t and the texture images\n", prog);
}

int
main(int argc, char *argv[])
{
    const char *input_path = NULL, *output_path = NULL;
    e_ply_storage_mode mode = PLY_LITTLE_ENDIAN;
    int with_normals = 1, with_colors = 1, with_texture = 1, with_alpha = 0;
    int a, ret = 1;
    char stem_path[512], stem_name[512];

    prc_context *ctx = NULL;
    prc_api_data data = NULL;
    prc_api_product *model_tree = NULL;
    prc_api_tess *tesses = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t total_tess = 0, total_line_tess = 0, num_extra = 0;
    uint32_t k, surfaces = 0, skipped = 0, missing_normals = 0;
    size_t n;
    out_mesh mesh;
    texture_list textures;
    instance_list instances;
    uint8_t *tess_drawn = NULL;

    memset(&mesh, 0, sizeof(mesh));
    memset(&textures, 0, sizeof(textures));
    memset(&instances, 0, sizeof(instances));

    for (a = 1; a < argc; a++)
    {
        if (strcmp(argv[a], "--ascii") == 0)
            mode = PLY_ASCII;
        else if (strcmp(argv[a], "--big-endian") == 0)
            mode = PLY_BIG_ENDIAN;
        else if (strcmp(argv[a], "--no-normals") == 0)
            with_normals = 0;
        else if (strcmp(argv[a], "--no-colors") == 0)
            with_colors = 0;
        else if (strcmp(argv[a], "--no-texture") == 0)
            with_texture = 0;
        else if (argv[a][0] == '-' && argv[a][1] == '-')
        {
            usage(argv[0]);
            return 1;
        }
        else if (input_path == NULL)
            input_path = argv[a];
        else if (output_path == NULL)
            output_path = argv[a];
        else
        {
            usage(argv[0]);
            return 1;
        }
    }
    if (input_path == NULL || output_path == NULL)
    {
        usage(argv[0]);
        return 1;
    }
    split_stem(output_path, stem_path, stem_name, sizeof(stem_path));

    ctx = prc_api_new_context(NULL);
    if (ctx == NULL)
    {
        fprintf(stderr, "Error: prc_api_new_context failed\n");
        return 1;
    }
    data = prc_api_open_contents(ctx, input_path);
    if (data == NULL)
    {
        fprintf(stderr, "Error: could not open %s\n", input_path);
        prc_api_release_context(ctx);
        return 1;
    }
    if (prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups) < 0 ||
        prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups) < 0 ||
        prc_api_get_number_tessellations(ctx, data, model_tree, &total_tess, &total_line_tess,
                                         &num_extra, 0) < 0)
    {
        fprintf(stderr, "Error: could not build the model tree of %s\n", input_path);
        prc_api_print_error_stack(ctx);
        goto cleanup;
    }
    if (total_tess > 0)
    {
        tesses = (prc_api_tess *)calloc(total_tess, sizeof(prc_api_tess));
        if (tesses == NULL)
        {
            fprintf(stderr, "Error: out of memory\n");
            goto cleanup;
        }
    }

    for (k = 0; k < total_tess; k++)
    {
        prc_api_tess *tess = &tesses[k];
        uint8_t has_lines = 0;
        uint32_t j;

        tess->num_faces = prc_api_get_number_faces(ctx, data, k);
        tess->tess_faces = (prc_api_face *)calloc(tess->num_faces ? tess->num_faces : 1,
                                                  sizeof(prc_api_face));
        if (tess->tess_faces == NULL)
        {
            fprintf(stderr, "Error: out of memory\n");
            goto cleanup;
        }
        if (prc_api_initialize_tessellation(ctx, data, model_tree, k, tess, NULL, &has_lines) < 0)
        {
            fprintf(stderr, "Error: could not initialise tessellation %u\n", k);
            prc_api_print_error_stack(ctx);
            goto cleanup;
        }
        if (tess->type != PRC_API_TESS_3D && tess->type != PRC_API_TESS_3D_Compressed)
            continue;
        for (j = 0; j < tess->num_faces; j++)
        {
            if (prc_api_get_tessellation_vertices(ctx, data, k, j, tess->tess_faces + j, tess) < 0)
            {
                fprintf(stderr, "Error: could not read tessellation %u face %u\n", k, j);
                prc_api_print_error_stack(ctx);
                goto cleanup;
            }
        }
        surfaces++;
    }

    if (total_tess > 0)
    {
        tess_drawn = (uint8_t *)calloc(total_tess, 1);
        if (tess_drawn == NULL)
        {
            fprintf(stderr, "Error: out of memory\n");
            goto cleanup;
        }
    }
    if (model_tree != NULL)
    {
        prc_api_transform root_transform;

        prc_api_set_transform_identity(ctx, &root_transform);
        if (collect_instances(ctx, data, model_tree, root_transform, 0, total_tess, &instances,
                tess_drawn) != 0)
        {
            fprintf(stderr, "Error: out of memory\n");
            goto cleanup;
        }
    }
    /* A surface tessellation that no part draws is reported, not written.
       Adobe Acrobat does not draw one either. Nor one only removed products
       or items draw: those are counted. */
    {
        uint32_t removed_only = 0;

        for (k = 0; k < total_tess; k++)
        {
            if (tess_drawn[k] == TESS_DRAWN ||
                (tesses[k].type != PRC_API_TESS_3D && tesses[k].type != PRC_API_TESS_3D_Compressed))
                continue;
            if (tess_drawn[k] == TESS_REMOVED_ONLY)
                removed_only++;
            else
                fprintf(stderr, "Warning: tessellation %u is not drawn by any part; not exported\n", k);
        }
        if (removed_only > 0)
            printf("%u tessellation(s) left out: only products or items marked removed draw them.\n",
                (unsigned)removed_only);
    }

    for (n = 0; n < instances.count; n++)
    {
        prc_api_tess *tess = &tesses[instances.items[n].tess];
        uint32_t j, faces_to_walk;

        if (tess->type != PRC_API_TESS_3D && tess->type != PRC_API_TESS_3D_Compressed)
            continue;

        /* A compressed tessellation gives every face the same complete
           mesh -- face_index only selects a style there -- so walking every
           face would write the mesh once per face (see obj_export). */
        faces_to_walk = (tess->type == PRC_API_TESS_3D_Compressed) ? 1 : (uint32_t)tess->num_faces;

        for (j = 0; j < faces_to_walk; j++)
        {
            prc_api_face *face = &tess->tess_faces[j];
            face_source fs;
            size_t p;
            uint32_t i;

            memset(&fs, 0, sizeof(fs));
            fs.vb = (tess->type == PRC_API_TESS_3D_Compressed) ? &tess->tess_vertices
                                                              : &face->face_vertices;
            if (fs.vb->vertices == NULL || fs.vb->num_vertices == 0)
                continue;
            fs.remap = (uint32_t *)malloc(fs.vb->num_vertices * sizeof(uint32_t));
            if (fs.remap == NULL)
            {
                fprintf(stderr, "Error: out of memory\n");
                goto cleanup;
            }
            for (i = 0; i < (uint32_t)fs.vb->num_vertices; i++)
                fs.remap[i] = NO_INDEX;
            fs.xform = &instances.items[n].xform;
            fs.mirror = transform_mirrors(fs.xform);

            fs.per_vertex_material = prc_api_vertices_have_material(ctx, tess, j) ? 1 : 0;
            fs.material = face->is_material ? &face->material
                        : tess->is_material ? &tess->tess_material : NULL;
            if (with_texture && face->is_texture && face->texture.data != NULL &&
                face->texture.width > 0 && face->texture.height > 0)
            {
                fs.texnum = texture_index(&textures, &face->texture, stem_path, stem_name);
                fs.textured = fs.texnum != NO_INDEX;
            }

            for (p = 0; p < face->num_graphic_primitives; p++)
            {
                prc_api_graphic_primitive prim;
                size_t m;
                int rc = 0;

                if (prc_api_get_graphics_primitive(ctx, data, tess, j, (uint32_t)p, &prim) < 0)
                    continue;
                if (prim.type == PRC_API_TRIANGLES)
                {
                    for (m = 0; m + 2 < prim.num_indices && rc == 0; m += 3)
                        rc = emit_triangle(&mesh, &fs, prim.indices[m], prim.indices[m + 1],
                                           prim.indices[m + 2], &skipped);
                }
                else if (prim.type == PRC_API_STRIP)
                {
                    for (m = 2; m < prim.num_indices && rc == 0; m++)
                    {
                        if (m % 2 == 0)
                            rc = emit_triangle(&mesh, &fs, prim.indices[m - 2], prim.indices[m - 1],
                                               prim.indices[m], &skipped);
                        else
                            rc = emit_triangle(&mesh, &fs, prim.indices[m - 1], prim.indices[m - 2],
                                               prim.indices[m], &skipped);
                    }
                }
                else if (prim.type == PRC_API_FAN)
                {
                    for (m = 2; m < prim.num_indices && rc == 0; m++)
                        rc = emit_triangle(&mesh, &fs, prim.indices[0], prim.indices[m - 1],
                                           prim.indices[m], &skipped);
                }
                if (rc != 0)
                {
                    free(fs.remap);
                    fprintf(stderr, "Error: out of memory\n");
                    goto cleanup;
                }
            }
            free(fs.remap);
        }
    }

    if (mesh.nt == 0)
    {
        fprintf(stderr, "Error: %s has no surface triangles to export\n", input_path);
        goto cleanup;
    }

    {
        /* nanoPRC's writer gives an item without a material this colour
           (0.72, 0.42, 0.20; prc_write_add_default_style), so a PLY with no
           colours comes back from PRC with every vertex this colour. Writing
           it out would make the round trip add colours the original never
           had, so a mesh that is uniformly this colour is written without
           colour properties. */
        static const uint8_t write_default[4] = { 184, 107, 51, 255 };
        int uniform_default = 1;

        for (k = 0; k < mesh.nv; k++)
        {
            if (!mesh.v[k].has_normal)
                missing_normals++;
            if (mesh.v[k].color[3] != 255)
                with_alpha = 1;
            if (memcmp(mesh.v[k].color, write_default, 4) != 0)
                uniform_default = 0;
        }
        if (uniform_default)
            with_colors = 0;
    }
    if (with_normals && missing_normals > 0)
        fill_missing_normals(&mesh);

    if (write_ply(output_path, mode, &mesh, &textures, with_normals, with_colors, with_alpha) != 0)
    {
        fprintf(stderr, "Error: could not write %s\n", output_path);
        goto cleanup;
    }
    printf("Wrote %s (%s): %u vertices, %u triangles from %u surface tessellations, "
           "%u textures; %u vertices given computed normals, %u triangles with a bad index "
           "skipped\n", output_path,
           mode == PLY_ASCII ? "ascii" : mode == PLY_BIG_ENDIAN ? "binary big-endian"
                                                               : "binary little-endian",
           mesh.nv, mesh.nt, surfaces, textures.count, with_normals ? missing_normals : 0,
           skipped);
    ret = 0;

cleanup:
    free(mesh.v); free(mesh.tri); free(mesh.texnum);
    free(textures.t);
    free(instances.items);
    free(tess_drawn);
    if (ctx != NULL)
    {
        if (data != NULL)
            prc_api_release_data(ctx, data, tesses, total_tess, NULL, 0, NULL, 0, model_tree);
        prc_api_release_context(ctx);
    }
    /* The face arrays and the tessellation array were allocated here, not
       by nanoPRC, so prc_api_release_data leaves them to us. */
    if (tesses != NULL)
    {
        uint32_t k;

        for (k = 0; k < total_tess; k++)
            free(tesses[k].tess_faces);
        free(tesses);
    }
    return ret;
}
