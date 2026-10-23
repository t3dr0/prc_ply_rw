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
 * ply_import: read a PLY file, ASCII or binary of either byte order, and
 * write it as a .prc file and a minimal 3D-annotated .pdf, through
 * nanoPRC's public write API only (include/prc_api.h).
 *
 * Parsing is done by RPly. What this file adds:
 *
 *   - the vertex element's x/y/z, and when present its normals
 *     (nx/ny/nz), colours (red/green/blue[/alpha], as bytes 0..255 or as
 *     floats 0..1) and texture coordinates (s/t, u/v, texture_u/texture_v
 *     or texture_s/texture_t);
 *   - the face element's vertex_indices (or vertex_index) list, polygons of
 *     more than three corners triangulated by ear clipping; and when
 *     present its colours, its per-corner texcoord list and its texnumber;
 *   - the tristrips element of older Stanford files, -1 restarting a strip;
 *   - "comment TextureFile <image>" header lines, the convention MeshLab
 *     writes, naming the PNG or JPEG the texture coordinates refer to.
 *
 * A PLY file has no parts or materials, so the model is one part. It holds
 * one tessellation, or with several texture files one per texnumber, since
 * a PRC representation item carries one texture.
 *
 * Geometry goes out as compressed tessellation by default. As in
 * obj_import, some meshes are written as plain triangles instead: a
 * textured one and a coloured one, because the compressed encoder has
 * neither a texture nor a colour path. --triangles writes everything that
 * way.
 *
 * Usage:
 *     nano_prc_ply_import <input.ply> [output.prc] [output.pdf]
 *                         [--triangles] [--no-normals] [--no-colors]
 *                         [--no-texture] [--quant-tolerance F]
 *
 * Output paths default to the input's name with .ply replaced.
 *
 * Not yet done: the edge element, and any element or property beyond
 * those listed above, which are read past and ignored.
 * ==================================================================== */

#include <prc_api.h>
#include <rply.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>

#define PLY_NO_INDEX 0xFFFFFFFFu
#define MAX_TEXTURE_FILES 64

/* Quantization tolerance of a compressed tessellation, as a fraction of the
   bounding-box diagonal: the write API's own default. --quant-tolerance
   changes it. */
#define PLY_IMPORT_QUANT_TOLERANCE 1e-6

/* -------------------------------------------------------------------- */
/* Small growable arrays                                                 */
/* -------------------------------------------------------------------- */

typedef struct
{
    uint32_t *data;
    uint32_t count;
    uint32_t capacity;
} u32_array;

static int
u32_push(u32_array *a, uint32_t v)
{
    if (a->count == a->capacity)
    {
        uint32_t cap = a->capacity ? a->capacity * 2 : 64;
        uint32_t *grown;

        if (cap < a->capacity)
            return -1;
        grown = (uint32_t *)realloc(a->data, (size_t)cap * sizeof(uint32_t));
        if (grown == NULL)
            return -1;
        a->data = grown;
        a->capacity = cap;
    }
    a->data[a->count++] = v;
    return 0;
}

typedef struct
{
    double *data;
    uint32_t count;
    uint32_t capacity;
} f64_array;

static int
f64_push(f64_array *a, double v)
{
    if (a->count == a->capacity)
    {
        uint32_t cap = a->capacity ? a->capacity * 2 : 64;
        double *grown;

        if (cap < a->capacity)
            return -1;
        grown = (double *)realloc(a->data, (size_t)cap * sizeof(double));
        if (grown == NULL)
            return -1;
        a->data = grown;
        a->capacity = cap;
    }
    a->data[a->count++] = v;
    return 0;
}

/* -------------------------------------------------------------------- */
/* Triangulation (as in obj_import)                                      */
/* -------------------------------------------------------------------- */

/* Twice the signed area of the 2D triangle (a, b, c). */
static double
cross2(const double *a, const double *b, const double *c)
{
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

/* Is p inside or on triangle (a, b, c), which has orientation sign `s`? */
static int
point_in_triangle(const double *p, const double *a, const double *b,
    const double *c, double s)
{
    return cross2(a, b, p) * s >= 0.0 &&
           cross2(b, c, p) * s >= 0.0 &&
           cross2(c, a, p) * s >= 0.0;
}

/* Triangulate one polygon of `n` corners. `corner` holds the corners'
   positions in polygon order. Triangles are appended to `out` as triples of
   corner numbers 0..n-1, in the polygon's own winding.

   Ear clipping in the plane the polygon most nearly lies in, chosen from
   its Newell normal. If no ear can be found -- a degenerate or
   self-intersecting polygon -- the remaining corners are fanned, so every
   polygon still produces n-2 triangles. */
static int
triangulate_polygon(const double (*corner)[3], uint32_t n, u32_array *out,
    double (*proj)[2], uint32_t *ring)
{
    double nx = 0.0, ny = 0.0, nz = 0.0, s;
    int ax, ay;
    uint32_t i, m;

    if (n == 3)
        return u32_push(out, 0) || u32_push(out, 1) || u32_push(out, 2);

    for (i = 0; i < n; i++)
    {
        const double *p = corner[i];
        const double *q = corner[(i + 1) % n];

        nx += (p[1] - q[1]) * (p[2] + q[2]);
        ny += (p[2] - q[2]) * (p[0] + q[0]);
        nz += (p[0] - q[0]) * (p[1] + q[1]);
    }

    /* Drop the axis the normal is largest along. */
    if (fabs(nx) >= fabs(ny) && fabs(nx) >= fabs(nz))
    {
        ax = 1; ay = 2; s = nx;
    }
    else if (fabs(ny) >= fabs(nz))
    {
        ax = 2; ay = 0; s = ny;
    }
    else
    {
        ax = 0; ay = 1; s = nz;
    }
    s = (s >= 0.0) ? 1.0 : -1.0;

    for (i = 0; i < n; i++)
    {
        proj[i][0] = corner[i][ax];
        proj[i][1] = corner[i][ay];
        ring[i] = i;
    }

    m = n;
    while (m > 3)
    {
        uint32_t k, found = 0;

        for (k = 0; k < m && !found; k++)
        {
            uint32_t ia = ring[(k + m - 1) % m];
            uint32_t ib = ring[k];
            uint32_t ic = ring[(k + 1) % m];
            uint32_t j;
            int blocked = 0;

            /* Convex corner in the polygon's own orientation. */
            if (cross2(proj[ia], proj[ib], proj[ic]) * s <= 0.0)
                continue;

            for (j = 0; j < m && !blocked; j++)
            {
                uint32_t iv = ring[j];

                if (iv == ia || iv == ib || iv == ic)
                    continue;
                if (point_in_triangle(proj[iv], proj[ia], proj[ib], proj[ic], s))
                    blocked = 1;
            }
            if (blocked)
                continue;

            if (u32_push(out, ia) || u32_push(out, ib) || u32_push(out, ic))
                return -1;
            memmove(&ring[k], &ring[k + 1], (size_t)(m - k - 1) * sizeof(uint32_t));
            m--;
            found = 1;
        }

        if (!found)
        {
            /* No ear: fan what remains rather than drop it. */
            for (k = 1; k + 1 < m; k++)
            {
                if (u32_push(out, ring[0]) || u32_push(out, ring[k]) ||
                    u32_push(out, ring[k + 1]))
                    return -1;
            }
            return 0;
        }
    }
    return u32_push(out, ring[0]) || u32_push(out, ring[1]) || u32_push(out, ring[2]);
}

/* -------------------------------------------------------------------- */
/* The PLY data, as read                                                 */
/* -------------------------------------------------------------------- */

typedef struct
{
    long      nv;           /* vertex element instances */
    double   *pos;          /* 3 per vertex */
    double   *nrm;          /* 3 per vertex, or NULL */
    double   *vuv;          /* 2 per vertex, or NULL */
    float    *vcol;         /* 4 per vertex (0..1), or NULL */
    int       vcol_alpha;   /* the vertex colour has an alpha property */

    long      nf;           /* face element instances */
    u32_array face_len;     /* corners of each face */
    u32_array face_idx;     /* the corners, all faces in turn */
    float    *fcol;         /* 4 per face (0..1), or NULL */
    int       fcol_alpha;
    uint32_t *texnum;       /* per face, or NULL */
    double   *texnum_d;     /* texnumber as read, before conversion */
    int       has_wedge_uv; /* the face element has a texcoord list */
    u32_array wedge_len;    /* values in each face's texcoord list */
    f64_array wedge_uv;     /* those values, all faces in turn */

    u32_array strip_idx;    /* tristrips corners, PLY_NO_INDEX = restart */

    char     *texture_files[MAX_TEXTURE_FILES];
    uint32_t  num_texture_files;
} ply_mesh;

static void
ply_mesh_free(ply_mesh *m)
{
    uint32_t i;

    free(m->pos); free(m->nrm); free(m->vuv); free(m->vcol);
    free(m->face_len.data); free(m->face_idx.data);
    free(m->fcol); free(m->texnum); free(m->texnum_d);
    free(m->wedge_len.data); free(m->wedge_uv.data);
    free(m->strip_idx.data);
    for (i = 0; i < m->num_texture_files; i++)
        free(m->texture_files[i]);
}

/* Where one scalar property's values go: dst_d or dst_f, `stride` values
   per instance, this property at `offset`, each value times `scale`. */
typedef struct
{
    double *dst_d;
    float  *dst_f;
    int     stride;
    int     offset;
    double  scale;
} scalar_target;

static int
scalar_cb(p_ply_argument argument)
{
    void *pdata;
    long instance;
    scalar_target *t;
    double v;

    ply_get_argument_user_data(argument, &pdata, NULL);
    ply_get_argument_element(argument, NULL, &instance);
    t = (scalar_target *)pdata;
    v = ply_get_argument_value(argument) * t->scale;
    if (t->dst_d != NULL)
        t->dst_d[instance * t->stride + t->offset] = v;
    else
        t->dst_f[instance * t->stride + t->offset] = (float)v;
    return 1;
}

static uint32_t
to_index(double v)
{
    if (!(v >= 0.0) || v >= 4294967295.0)
        return PLY_NO_INDEX;
    return (uint32_t)v;
}

/* face vertex_indices: the length arrives first (value_index -1), then the
   indices. */
static int
face_index_cb(p_ply_argument argument)
{
    void *pdata;
    long length, value_index;
    ply_mesh *m;

    ply_get_argument_user_data(argument, &pdata, NULL);
    ply_get_argument_property(argument, NULL, &length, &value_index);
    m = (ply_mesh *)pdata;
    if (value_index < 0)
        return u32_push(&m->face_len, length > 0 ? (uint32_t)length : 0) == 0;
    return u32_push(&m->face_idx, to_index(ply_get_argument_value(argument))) == 0;
}

static int
wedge_uv_cb(p_ply_argument argument)
{
    void *pdata;
    long length, value_index;
    ply_mesh *m;

    ply_get_argument_user_data(argument, &pdata, NULL);
    ply_get_argument_property(argument, NULL, &length, &value_index);
    m = (ply_mesh *)pdata;
    if (value_index < 0)
        return u32_push(&m->wedge_len, length > 0 ? (uint32_t)length : 0) == 0;
    return f64_push(&m->wedge_uv, ply_get_argument_value(argument)) == 0;
}

static int
strip_cb(p_ply_argument argument)
{
    void *pdata;
    long length, value_index;
    ply_mesh *m;

    ply_get_argument_user_data(argument, &pdata, NULL);
    ply_get_argument_property(argument, NULL, &length, &value_index);
    m = (ply_mesh *)pdata;
    /* Each tristrips instance starts a new strip; -1 inside one restarts. */
    if (value_index < 0)
        return u32_push(&m->strip_idx, PLY_NO_INDEX) == 0;
    return u32_push(&m->strip_idx, to_index(ply_get_argument_value(argument))) == 0;
}

static void
error_cb(p_ply ply, const char *message)
{
    (void)ply;
    fprintf(stderr, "PLY: %s\n", message);
}

/* -------------------------------------------------------------------- */
/* Header inspection                                                     */
/* -------------------------------------------------------------------- */

static p_ply_element
find_element(p_ply ply, const char *name, long *ninstances)
{
    p_ply_element e = NULL;

    while ((e = ply_get_next_element(ply, e)) != NULL)
    {
        const char *en;

        ply_get_element_info(e, &en, ninstances);
        if (strcmp(en, name) == 0)
            return e;
    }
    return NULL;
}

/* The first of `names` (NULL-terminated) that `element` has, or NULL. */
static const char *
find_property(p_ply_element element, const char *const *names, e_ply_type *type,
    e_ply_type *value_type)
{
    uint32_t n;

    if (element == NULL)
        return NULL;
    for (n = 0; names[n] != NULL; n++)
    {
        p_ply_property p = NULL;

        while ((p = ply_get_next_property(element, p)) != NULL)
        {
            const char *pn;

            ply_get_property_info(p, &pn, type, NULL, value_type);
            if (strcmp(pn, names[n]) == 0)
                return names[n];
        }
    }
    return NULL;
}

static int
is_float_type(e_ply_type t)
{
    return t == PLY_FLOAT32 || t == PLY_FLOAT64 || t == PLY_FLOAT || t == PLY_DOUBLE;
}

/* A colour component is a float 0..1 or an integer 0..255. */
static double
color_scale(e_ply_type t)
{
    return is_float_type(t) ? 1.0 : 1.0 / 255.0;
}

/* The format line of the header, for the summary. RPly reads it but does
   not report it. */
static void
peek_format(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "rb");
    char line[256];
    int n;

    strncpy(out, "unknown", size - 1);
    out[size - 1] = '\0';
    if (f == NULL)
        return;
    for (n = 0; n < 4 && fgets(line, sizeof(line), f) != NULL; n++)
    {
        if (strncmp(line, "format ", 7) == 0)
        {
            size_t len = strcspn(line + 7, "\r\n");

            if (len >= size)
                len = size - 1;
            memcpy(out, line + 7, len);
            out[len] = '\0';
            break;
        }
    }
    fclose(f);
}

/* Texture file names from "comment TextureFile <name>" lines, resolved
   against the directory of the PLY file. */
static void
collect_texture_files(p_ply ply, const char *ply_path, ply_mesh *m)
{
    const char *c = NULL;
    const char *slash = strrchr(ply_path, '/');
    const char *bslash = strrchr(ply_path, '\\');
    size_t dir_len;

    if (bslash > slash)
        slash = bslash;
    dir_len = slash ? (size_t)(slash - ply_path) + 1 : 0;

    while ((c = ply_get_next_comment(ply, c)) != NULL)
    {
        const char *name;
        size_t len;
        char *path;
        int i;

        while (*c == ' ')
            c++;
        for (i = 0; i < 11; i++)
            if (tolower((unsigned char)c[i]) != "texturefile"[i])
                break;
        if (i < 11 || c[11] != ' ')
            continue;
        name = c + 12;
        while (*name == ' ')
            name++;
        len = strlen(name);
        while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\r'))
            len--;
        if (len == 0 || m->num_texture_files == MAX_TEXTURE_FILES)
            continue;

        /* An absolute name is taken as it stands. */
        if (name[0] == '/' || name[0] == '\\' || (len > 1 && name[1] == ':'))
            dir_len = 0;
        path = (char *)malloc(dir_len + len + 1);
        if (path == NULL)
            continue;
        memcpy(path, ply_path, dir_len);
        memcpy(path + dir_len, name, len);
        path[dir_len + len] = '\0';
        m->texture_files[m->num_texture_files++] = path;
    }
}

/* -------------------------------------------------------------------- */
/* Reading                                                               */
/* -------------------------------------------------------------------- */

static const char *const NAMES_X[] = { "x", NULL };
static const char *const NAMES_Y[] = { "y", NULL };
static const char *const NAMES_Z[] = { "z", NULL };
static const char *const NAMES_NX[] = { "nx", "normal_x", NULL };
static const char *const NAMES_NY[] = { "ny", "normal_y", NULL };
static const char *const NAMES_NZ[] = { "nz", "normal_z", NULL };
static const char *const NAMES_R[] = { "red", "r", "diffuse_red", NULL };
static const char *const NAMES_G[] = { "green", "g", "diffuse_green", NULL };
static const char *const NAMES_B[] = { "blue", "b", "diffuse_blue", NULL };
static const char *const NAMES_A[] = { "alpha", "a", "diffuse_alpha", NULL };
static const char *const NAMES_FACE_INDICES[] = { "vertex_indices", "vertex_index", NULL };
static const char *const NAMES_TEXCOORD[] = { "texcoord", NULL };
static const char *const NAMES_TEXNUMBER[] = { "texnumber", NULL };

/* Texture coordinate property pairs, in order of preference. */
static const char *const UV_U[] = { "s", "u", "texture_u", "texture_s", NULL };
static const char *const UV_V[] = { "t", "v", "texture_v", "texture_t", NULL };

static int
read_ply(const char *path, ply_mesh *m)
{
    p_ply ply;
    p_ply_element ve, fe, se;
    e_ply_type type, vtype;
    /* At most 3 position + 3 normal + 2 uv + 4 vertex colour + 4 face
       colour + 1 texnumber targets; RPly holds pointers to them until
       ply_read returns. */
    scalar_target tg[17];
    int ntg = 0, ok = 0, c;
    const char *const *rgb[3];

    rgb[0] = NAMES_R; rgb[1] = NAMES_G; rgb[2] = NAMES_B;

    ply = ply_open(path, error_cb, 0, NULL);
    if (ply == NULL)
        return -1;
    if (!ply_read_header(ply))
        goto out;

    ve = find_element(ply, "vertex", &m->nv);
    if (ve == NULL || m->nv <= 0 ||
        !find_property(ve, NAMES_X, &type, NULL) || !find_property(ve, NAMES_Y, &type, NULL) ||
        !find_property(ve, NAMES_Z, &type, NULL))
    {
        fprintf(stderr, "%s has no vertex element with x, y and z\n", path);
        goto out;
    }
    fe = find_element(ply, "face", &m->nf);
    if (fe == NULL)
        m->nf = 0;
    {
        long ns;

        se = find_element(ply, "tristrips", &ns);
    }

    collect_texture_files(ply, path, m);

    /* Positions, always. */
    m->pos = (double *)calloc((size_t)m->nv * 3, sizeof(double));
    if (m->pos == NULL)
        goto oom;
    for (c = 0; c < 3; c++)
    {
        const char *const *names = (c == 0) ? NAMES_X : (c == 1) ? NAMES_Y : NAMES_Z;
        scalar_target *t = &tg[ntg++];

        t->dst_d = m->pos; t->dst_f = NULL; t->stride = 3; t->offset = c; t->scale = 1.0;
        ply_set_read_cb(ply, "vertex", names[0], scalar_cb, t, 0);
    }

    /* Normals, only when all three components are there. */
    {
        const char *nx = find_property(ve, NAMES_NX, &type, NULL);
        const char *ny = find_property(ve, NAMES_NY, &type, NULL);
        const char *nz = find_property(ve, NAMES_NZ, &type, NULL);

        if (nx && ny && nz)
        {
            const char *nn[3];

            nn[0] = nx; nn[1] = ny; nn[2] = nz;
            m->nrm = (double *)calloc((size_t)m->nv * 3, sizeof(double));
            if (m->nrm == NULL)
                goto oom;
            for (c = 0; c < 3; c++)
            {
                scalar_target *t = &tg[ntg++];

                t->dst_d = m->nrm; t->dst_f = NULL; t->stride = 3; t->offset = c; t->scale = 1.0;
                ply_set_read_cb(ply, "vertex", nn[c], scalar_cb, t, 0);
            }
        }
    }

    /* Texture coordinates: the first pair present. */
    {
        uint32_t k;

        for (k = 0; UV_U[k] != NULL; k++)
        {
            const char *un[2], *vn[2];

            un[0] = UV_U[k]; un[1] = NULL;
            vn[0] = UV_V[k]; vn[1] = NULL;
            if (find_property(ve, un, &type, NULL) && find_property(ve, vn, &type, NULL))
            {
                scalar_target *t;

                m->vuv = (double *)calloc((size_t)m->nv * 2, sizeof(double));
                if (m->vuv == NULL)
                    goto oom;
                t = &tg[ntg++];
                t->dst_d = m->vuv; t->dst_f = NULL; t->stride = 2; t->offset = 0; t->scale = 1.0;
                ply_set_read_cb(ply, "vertex", UV_U[k], scalar_cb, t, 0);
                t = &tg[ntg++];
                t->dst_d = m->vuv; t->dst_f = NULL; t->stride = 2; t->offset = 1; t->scale = 1.0;
                ply_set_read_cb(ply, "vertex", UV_V[k], scalar_cb, t, 0);
                break;
            }
        }
    }

    /* Vertex colours, then face colours: red, green and blue all needed,
       alpha optional, alpha defaulting to opaque. */
    {
        int pass;

        for (pass = 0; pass < 2; pass++)
        {
            p_ply_element el = pass == 0 ? ve : fe;
            const char *elem_name = pass == 0 ? "vertex" : "face";
            long n = pass == 0 ? m->nv : m->nf;
            const char *found[3];
            e_ply_type ftype[3];
            float *col;
            long i;

            if (el == NULL || n <= 0)
                continue;
            for (c = 0; c < 3; c++)
                found[c] = find_property(el, rgb[c], &ftype[c], NULL);
            if (!found[0] || !found[1] || !found[2])
                continue;

            col = (float *)malloc((size_t)n * 4 * sizeof(float));
            if (col == NULL)
                goto oom;
            for (i = 0; i < n * 4; i++)
                col[i] = 1.0f;
            for (c = 0; c < 3; c++)
            {
                scalar_target *t = &tg[ntg++];

                t->dst_d = NULL; t->dst_f = col; t->stride = 4; t->offset = c;
                t->scale = color_scale(ftype[c]);
                ply_set_read_cb(ply, elem_name, found[c], scalar_cb, t, 0);
            }
            {
                const char *an = find_property(el, NAMES_A, &type, NULL);

                if (an)
                {
                    scalar_target *t = &tg[ntg++];

                    t->dst_d = NULL; t->dst_f = col; t->stride = 4; t->offset = 3;
                    t->scale = color_scale(type);
                    ply_set_read_cb(ply, elem_name, an, scalar_cb, t, 0);
                }
                if (pass == 0)
                {
                    m->vcol = col;
                    m->vcol_alpha = an != NULL;
                }
                else
                {
                    m->fcol = col;
                    m->fcol_alpha = an != NULL;
                }
            }
        }
    }

    /* Faces. */
    if (fe != NULL && m->nf > 0)
    {
        const char *fi = find_property(fe, NAMES_FACE_INDICES, &type, &vtype);

        if (fi == NULL || type != PLY_LIST)
        {
            fprintf(stderr, "%s: the face element has no vertex_indices list\n", path);
            goto out;
        }
        ply_set_read_cb(ply, "face", fi, face_index_cb, m, 0);

        if (find_property(fe, NAMES_TEXCOORD, &type, NULL) && type == PLY_LIST)
        {
            m->has_wedge_uv = 1;
            ply_set_read_cb(ply, "face", "texcoord", wedge_uv_cb, m, 0);
        }
        if (find_property(fe, NAMES_TEXNUMBER, &type, NULL) && type != PLY_LIST)
        {
            scalar_target *t = &tg[ntg++];

            m->texnum = (uint32_t *)calloc((size_t)m->nf, sizeof(uint32_t));
            m->texnum_d = (double *)calloc((size_t)m->nf, sizeof(double));
            if (m->texnum == NULL || m->texnum_d == NULL)
                goto oom;
            t->dst_d = m->texnum_d; t->dst_f = NULL; t->stride = 1; t->offset = 0; t->scale = 1.0;
            ply_set_read_cb(ply, "face", "texnumber", scalar_cb, t, 0);
        }
    }
    if (se != NULL && find_property(se, NAMES_FACE_INDICES, &type, NULL) && type == PLY_LIST)
        ply_set_read_cb(ply, "tristrips", "vertex_indices", strip_cb, m, 0);

    if (!ply_read(ply))
    {
        fprintf(stderr, "%s: read failed\n", path);
        goto out;
    }

    if (m->texnum != NULL)
    {
        long i;

        for (i = 0; i < m->nf; i++)
            m->texnum[i] = to_index(m->texnum_d[i]);
    }
    ok = 1;
    goto out;

oom:
    fprintf(stderr, "Out of memory\n");
out:
    ply_close(ply);
    return ok ? 0 : -1;
}

/* -------------------------------------------------------------------- */
/* Triangles                                                             */
/* -------------------------------------------------------------------- */

/* Every triangle of the mesh, faces and strips together. */
typedef struct
{
    u32_array v;    /* 3 per triangle: vertex */
    u32_array w;    /* 3 per triangle: offset into wedge_uv, or PLY_NO_INDEX */
    u32_array f;    /* 1 per triangle: face, or PLY_NO_INDEX for a strip */
} tri_list;

typedef struct
{
    uint32_t polygons_split;
    uint32_t small_faces;       /* fewer than 3 corners */
    uint32_t bad_faces;         /* a corner indexes no vertex */
    uint32_t degenerate;        /* two corners on one vertex */
    uint32_t bad_wedges;        /* texcoord list of the wrong length */
    uint32_t strip_triangles;
} tri_stats;

static int
push_triangle(tri_list *tl, const uint32_t v[3], const uint32_t w[3], uint32_t f)
{
    return u32_push(&tl->v, v[0]) || u32_push(&tl->v, v[1]) || u32_push(&tl->v, v[2]) ||
           u32_push(&tl->w, w[0]) || u32_push(&tl->w, w[1]) || u32_push(&tl->w, w[2]) ||
           u32_push(&tl->f, f);
}

static int
build_triangles(const ply_mesh *m, tri_list *tl, tri_stats *st)
{
    uint32_t max_n = 3, f, pos = 0, wpos = 0;
    double (*corner)[3] = NULL;
    double (*proj)[2] = NULL;
    uint32_t *ring = NULL;
    u32_array tri = { NULL, 0, 0 };
    int rc = -1;

    for (f = 0; f < m->face_len.count; f++)
        if (m->face_len.data[f] > max_n)
            max_n = m->face_len.data[f];
    corner = (double (*)[3])malloc((size_t)max_n * sizeof(*corner));
    proj = (double (*)[2])malloc((size_t)max_n * sizeof(*proj));
    ring = (uint32_t *)malloc((size_t)max_n * sizeof(uint32_t));
    if (corner == NULL || proj == NULL || ring == NULL)
        goto done;

    for (f = 0; f < m->face_len.count; f++)
    {
        uint32_t n = m->face_len.data[f];
        const uint32_t *idx = &m->face_idx.data[pos];
        uint32_t wl = (m->has_wedge_uv && f < m->wedge_len.count) ? m->wedge_len.data[f] : 0;
        uint32_t wbase = wpos;
        int wedge_ok = m->has_wedge_uv && wl == 2 * n && wpos + wl <= m->wedge_uv.count;
        uint32_t c;
        int valid = 1;

        if (pos + n > m->face_idx.count)
            break;
        pos += n;
        wpos += wl;
        if (m->has_wedge_uv && !wedge_ok)
            st->bad_wedges++;

        if (n < 3)
        {
            st->small_faces++;
            continue;
        }
        for (c = 0; c < n; c++)
        {
            if (idx[c] >= (uint32_t)m->nv)
            {
                valid = 0;
                break;
            }
            corner[c][0] = m->pos[idx[c] * 3];
            corner[c][1] = m->pos[idx[c] * 3 + 1];
            corner[c][2] = m->pos[idx[c] * 3 + 2];
        }
        if (!valid)
        {
            st->bad_faces++;
            continue;
        }

        tri.count = 0;
        if (triangulate_polygon((const double (*)[3])corner, n, &tri, proj, ring) != 0)
            goto done;
        if (n > 3)
            st->polygons_split++;

        for (c = 0; c + 2 < tri.count; c += 3)
        {
            uint32_t v[3], w[3], k;

            for (k = 0; k < 3; k++)
            {
                v[k] = idx[tri.data[c + k]];
                w[k] = wedge_ok ? wbase + 2 * tri.data[c + k] : PLY_NO_INDEX;
            }
            if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
            {
                st->degenerate++;
                continue;
            }
            if (push_triangle(tl, v, w, f) != 0)
                goto done;
        }
    }

    /* Strips: each new corner after the first two makes a triangle, the
       winding alternating. Degenerate triangles join strips together and
       are dropped without being counted as faults. */
    {
        uint32_t i, run = 0, a = 0, b = 0;
        const uint32_t none[3] = { PLY_NO_INDEX, PLY_NO_INDEX, PLY_NO_INDEX };

        for (i = 0; i < m->strip_idx.count; i++)
        {
            uint32_t x = m->strip_idx.data[i];

            if (x == PLY_NO_INDEX || x >= (uint32_t)m->nv)
            {
                run = 0;
                continue;
            }
            if (run >= 2 && a != b && b != x && a != x)
            {
                uint32_t v[3];

                if (run % 2 == 0)
                {
                    v[0] = a; v[1] = b;
                }
                else
                {
                    v[0] = b; v[1] = a;
                }
                v[2] = x;
                if (push_triangle(tl, v, none, PLY_NO_INDEX) != 0)
                    goto done;
                st->strip_triangles++;
            }
            a = b;
            b = x;
            run++;
        }
    }
    rc = 0;

done:
    free(corner); free(proj); free(ring); free(tri.data);
    return rc;
}

/* -------------------------------------------------------------------- */
/* One tessellation entry: the triangles that use one texture            */
/* -------------------------------------------------------------------- */

typedef struct
{
    u32_array tris;             /* triangle numbers in tri_list */
    int       textured;
    int       colored;
    uint32_t  texture_file;     /* which TextureFile, when textured */

    /* Built for the writer */
    double   *positions;
    uint32_t  num_positions;
    double   *normals;
    uint32_t *tri_indices;
    double   *tex_coords;
    uint32_t  num_tex_coords;
    uint32_t *tex_indices;
    uint8_t  *colors;
    uint32_t  face_tri_count;
    double    bbox_min[3], bbox_max[3];
} submesh;

static void
submesh_free(submesh *s)
{
    free(s->tris.data);
    free(s->positions); free(s->normals); free(s->tri_indices);
    free(s->tex_coords); free(s->tex_indices); free(s->colors);
}

static uint8_t
to_byte(float c)
{
    if (!(c > 0.0f))
        return 0;
    if (c >= 1.0f)
        return 255;
    return (uint8_t)(c * 255.0f + 0.5f);
}

/* Build the arrays the writer takes. Positions are compacted to those the
   submesh uses, in first-use order; normals and per-vertex UVs follow the
   same numbering. Per-corner UVs (a face texcoord list) are written one
   pair per corner. `color_mode` is 0 for none, 1 vertex, 2 face.
   *has_normals is cleared if a supplied normal has zero length. */
static int
build_submesh(const ply_mesh *m, const tri_list *tl, submesh *s, int use_normals,
    int color_mode, int alpha, int *has_normals)
{
    uint32_t T = s->tris.count, t, c, d;
    uint32_t *remap = (uint32_t *)malloc((size_t)m->nv * sizeof(uint32_t));
    uint32_t max_pos = (3 * T < (uint32_t)m->nv) ? 3 * T : (uint32_t)m->nv;
    int per_corner_uv = 0;
    uint32_t ncomp = alpha ? 4 : 3;
    long i;

    *has_normals = use_normals && m->nrm != NULL;
    s->positions = (double *)malloc((size_t)max_pos * 3 * sizeof(double));
    s->tri_indices = (uint32_t *)malloc((size_t)T * 3 * sizeof(uint32_t));
    if (*has_normals)
        s->normals = (double *)malloc((size_t)max_pos * 3 * sizeof(double));
    if (remap == NULL || s->positions == NULL || s->tri_indices == NULL ||
        (*has_normals && s->normals == NULL))
    {
        free(remap);
        return -1;
    }
    for (i = 0; i < m->nv; i++)
        remap[i] = PLY_NO_INDEX;

    for (t = 0; t < T; t++)
    {
        uint32_t tri = s->tris.data[t];

        for (c = 0; c < 3; c++)
        {
            uint32_t v = tl->v.data[tri * 3 + c];

            if (remap[v] == PLY_NO_INDEX)
            {
                uint32_t n = s->num_positions++;

                remap[v] = n;
                for (d = 0; d < 3; d++)
                    s->positions[n * 3 + d] = m->pos[v * 3 + d];
                if (*has_normals)
                {
                    double x = m->nrm[v * 3], y = m->nrm[v * 3 + 1], z = m->nrm[v * 3 + 2];
                    double len = sqrt(x * x + y * y + z * z);

                    if (len > 0.0)
                    {
                        s->normals[n * 3] = x / len;
                        s->normals[n * 3 + 1] = y / len;
                        s->normals[n * 3 + 2] = z / len;
                    }
                    else
                        *has_normals = 0;
                }
            }
            s->tri_indices[t * 3 + c] = remap[v];
            if (tl->w.data[tri * 3 + c] != PLY_NO_INDEX)
                per_corner_uv = 1;
        }
    }
    free(remap);
    s->face_tri_count = T;

    if (s->textured)
    {
        if (!per_corner_uv)
        {
            /* Per-vertex UVs share the positions' numbering. */
            s->num_tex_coords = s->num_positions;
            s->tex_coords = (double *)malloc((size_t)s->num_positions * 2 * sizeof(double));
            s->tex_indices = (uint32_t *)malloc((size_t)T * 3 * sizeof(uint32_t));
            if (s->tex_coords == NULL || s->tex_indices == NULL)
                return -1;
            for (t = 0; t < T; t++)
                for (c = 0; c < 3; c++)
                {
                    uint32_t v = tl->v.data[s->tris.data[t] * 3 + c];
                    uint32_t p = s->tri_indices[t * 3 + c];

                    s->tex_coords[p * 2] = m->vuv[v * 2];
                    s->tex_coords[p * 2 + 1] = m->vuv[v * 2 + 1];
                    s->tex_indices[t * 3 + c] = p;
                }
        }
        else
        {
            s->num_tex_coords = 3 * T;
            s->tex_coords = (double *)malloc((size_t)T * 6 * sizeof(double));
            s->tex_indices = (uint32_t *)malloc((size_t)T * 3 * sizeof(uint32_t));
            if (s->tex_coords == NULL || s->tex_indices == NULL)
                return -1;
            for (t = 0; t < T; t++)
                for (c = 0; c < 3; c++)
                {
                    uint32_t tri = s->tris.data[t];
                    uint32_t w = tl->w.data[tri * 3 + c];
                    uint32_t k = t * 3 + c;

                    if (w != PLY_NO_INDEX)
                    {
                        s->tex_coords[k * 2] = m->wedge_uv.data[w];
                        s->tex_coords[k * 2 + 1] = m->wedge_uv.data[w + 1];
                    }
                    else
                    {
                        uint32_t v = tl->v.data[tri * 3 + c];

                        s->tex_coords[k * 2] = m->vuv[v * 2];
                        s->tex_coords[k * 2 + 1] = m->vuv[v * 2 + 1];
                    }
                    s->tex_indices[k] = k;
                }
        }
    }

    /* One colour per corner, which is how the format stores it. */
    if (color_mode != 0)
    {
        static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        s->colors = (uint8_t *)malloc((size_t)T * 3 * ncomp);
        if (s->colors == NULL)
            return -1;
        for (t = 0; t < T; t++)
            for (c = 0; c < 3; c++)
            {
                uint32_t tri = s->tris.data[t];
                const float *src = white;
                uint8_t *dst = &s->colors[(t * 3 + c) * ncomp];

                if (color_mode == 1)
                    src = &m->vcol[tl->v.data[tri * 3 + c] * 4];
                else if (tl->f.data[tri] != PLY_NO_INDEX)
                    src = &m->fcol[tl->f.data[tri] * 4];
                for (d = 0; d < ncomp; d++)
                    dst[d] = to_byte(src[d]);
            }
        s->colored = 1;
    }

    for (d = 0; d < 3; d++)
    {
        s->bbox_min[d] = s->positions[d];
        s->bbox_max[d] = s->positions[d];
    }
    for (t = 1; t < s->num_positions; t++)
        for (d = 0; d < 3; d++)
        {
            double v = s->positions[t * 3 + d];

            if (v < s->bbox_min[d]) s->bbox_min[d] = v;
            if (v > s->bbox_max[d]) s->bbox_max[d] = v;
        }
    return 0;
}

/* -------------------------------------------------------------------- */
/* Files                                                                 */
/* -------------------------------------------------------------------- */

static int
read_file(const char *path, uint8_t **bytes, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *buf;

    if (f == NULL)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return -1;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (buf == NULL || fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *bytes = buf;
    *size = (size_t)len;
    return 0;
}

/* PNG or JPEG, by signature rather than by file extension. */
static int
image_format(const uint8_t *b, size_t n, uint32_t *format, uint32_t *w, uint32_t *h)
{
    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    *w = *h = 0;
    if (n >= 24 && memcmp(b, png_sig, 8) == 0)
    {
        *format = PRC_API_WRITE_TEXTURE_PNG;
        *w = ((uint32_t)b[16] << 24) | ((uint32_t)b[17] << 16) | ((uint32_t)b[18] << 8) | b[19];
        *h = ((uint32_t)b[20] << 24) | ((uint32_t)b[21] << 16) | ((uint32_t)b[22] << 8) | b[23];
        return 0;
    }
    if (n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
    {
        *format = PRC_API_WRITE_TEXTURE_JPEG;
        return 0;
    }
    return -1;
}

typedef struct
{
    uint8_t *bytes;
    size_t   size;
    uint32_t format, width, height;
} texture_image;

static char *
replace_extension(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    size_t stem;
    char *out;

    if (bslash > slash)
        slash = bslash;
    stem = (dot != NULL && (slash == NULL || dot > slash)) ? (size_t)(dot - path) : strlen(path);
    out = (char *)malloc(stem + strlen(ext) + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, path, stem);
    strcpy(out + stem, ext);
    return out;
}

/* The file name without directory or extension, as the part name. */
static char *
file_stem(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base;
    char *out, *dot;

    if (bslash > slash)
        slash = bslash;
    base = slash ? slash + 1 : path;
    out = (char *)malloc(strlen(base) + 1);
    if (out == NULL)
        return NULL;
    strcpy(out, base);
    dot = strrchr(out, '.');
    if (dot != NULL && dot != out)
        *dot = '\0';
    return out;
}

/* -------------------------------------------------------------------- */
/* Main                                                                  */
/* -------------------------------------------------------------------- */

static void
usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <input.ply> [output.prc] [output.pdf] [options]\n"
        "  Writes a PLY file (ASCII or binary) as PRC and a 3D PDF.\n"
        "  --triangles     write uncompressed triangles (default: compressed,\n"
        "                  except textured or coloured meshes, which are\n"
        "                  written as triangles).\n"
        "  --no-normals    ignore the file's normals (the reader computes them)\n"
        "  --no-colors     ignore vertex and face colours\n"
        "  --no-texture    ignore texture coordinates and TextureFile comments\n"
        "  --quant-tolerance F\n"
        "                  compressed tessellation's quantization tolerance, as a\n"
        "                  fraction of the bounding-box diagonal (default %g)\n",
        prog, PLY_IMPORT_QUANT_TOLERANCE);
}

int
main(int argc, char *argv[])
{
    const char *in_path = NULL;
    const char *prc_path = NULL;
    const char *pdf_path = NULL;
    char *prc_default = NULL, *pdf_default = NULL, *part_name = NULL;
    int force_triangles = 0, ignore_normals = 0, ignore_colors = 0, ignore_texture = 0;
    int a, rc = 1, color_mode = 0, color_alpha = 0, want_texture;
    double quant_tolerance = PLY_IMPORT_QUANT_TOLERANCE;
    uint32_t dropped_normals = 0;
    char format[64];

    ply_mesh mesh;
    tri_list tl;
    tri_stats st;
    texture_image tex[MAX_TEXTURE_FILES];
    prc_context *ctx = NULL;

    submesh *subs = NULL;
    uint32_t num_groups = 1, num_subs = 0;
    prc_api_write_tessellation *tess = NULL;
    prc_api_write_rep_item *items = NULL;
    prc_api_write_node node, root;
    prc_api_write_node *children[1];
    uint8_t *prc_buf = NULL;
    size_t prc_size = 0;
    uint32_t i, total_tris = 0;

    memset(&mesh, 0, sizeof(mesh));
    memset(&tl, 0, sizeof(tl));
    memset(&st, 0, sizeof(st));
    memset(tex, 0, sizeof(tex));

    for (a = 1; a < argc; a++)
    {
        if (strcmp(argv[a], "--triangles") == 0)
            force_triangles = 1;
        else if (strcmp(argv[a], "--no-normals") == 0)
            ignore_normals = 1;
        else if (strcmp(argv[a], "--no-colors") == 0)
            ignore_colors = 1;
        else if (strcmp(argv[a], "--no-texture") == 0)
            ignore_texture = 1;
        else if (strcmp(argv[a], "--quant-tolerance") == 0 && a + 1 < argc)
        {
            quant_tolerance = atof(argv[++a]);
            if (!(quant_tolerance > 0.0))
            {
                usage(argv[0]);
                return 1;
            }
        }
        else if (argv[a][0] == '-' && argv[a][1] == '-')
        {
            usage(argv[0]);
            return 1;
        }
        else if (in_path == NULL)
            in_path = argv[a];
        else if (prc_path == NULL)
            prc_path = argv[a];
        else if (pdf_path == NULL)
            pdf_path = argv[a];
        else
        {
            usage(argv[0]);
            return 1;
        }
    }
    if (in_path == NULL)
    {
        usage(argv[0]);
        return 1;
    }
    if (prc_path == NULL)
        prc_path = prc_default = replace_extension(in_path, ".prc");
    if (pdf_path == NULL)
        pdf_path = pdf_default = replace_extension(in_path, ".pdf");
    part_name = file_stem(in_path);
    if (prc_path == NULL || pdf_path == NULL || part_name == NULL)
        goto done;

    peek_format(in_path, format, sizeof(format));
    if (read_ply(in_path, &mesh) != 0)
    {
        fprintf(stderr, "Could not read %s\n", in_path);
        goto done;
    }
    printf("%s: format %s, %ld vertices%s%s%s%s, %ld faces%s%s, %u strip corners, "
           "%u texture files\n", in_path, format, mesh.nv,
           mesh.nrm ? ", normals" : "", mesh.vcol ? ", colours" : "",
           mesh.vcol_alpha ? " (with alpha)" : "", mesh.vuv ? ", UVs" : "",
           mesh.nf, mesh.fcol ? ", face colours" : "",
           mesh.has_wedge_uv ? ", per-corner UVs" : "",
           mesh.strip_idx.count, mesh.num_texture_files);

    if (build_triangles(&mesh, &tl, &st) != 0)
    {
        fprintf(stderr, "Out of memory\n");
        goto done;
    }
    if (tl.f.count == 0)
    {
        fprintf(stderr, "%s contains no triangles\n", in_path);
        goto done;
    }

    /* Textures: the TextureFile images that load as PNG or JPEG. */
    want_texture = !ignore_texture && mesh.num_texture_files > 0 &&
                   (mesh.vuv != NULL || mesh.has_wedge_uv);
    if (!ignore_texture && mesh.num_texture_files > 0 && !want_texture)
        fprintf(stderr, "warning: the file names a texture but has no texture coordinates; "
                "written untextured\n");
    if (!ignore_texture && mesh.num_texture_files == 0 && (mesh.vuv != NULL || mesh.has_wedge_uv))
        fprintf(stderr, "warning: the file has texture coordinates but no "
                "\"comment TextureFile\" line naming an image; written untextured\n");
    if (want_texture)
    {
        for (i = 0; i < mesh.num_texture_files; i++)
        {
            const char *img = mesh.texture_files[i];
            texture_image *ti = &tex[i];

            if (read_file(img, &ti->bytes, &ti->size) != 0)
                fprintf(stderr, "warning: texture %s not found, its faces left untextured\n", img);
            else if (image_format(ti->bytes, ti->size, &ti->format, &ti->width, &ti->height) != 0)
            {
                fprintf(stderr, "warning: texture %s is not PNG or JPEG, its faces left untextured\n", img);
                free(ti->bytes);
                ti->bytes = NULL;
            }
        }
        num_groups = mesh.num_texture_files;
    }

    if (!ignore_colors)
    {
        if (mesh.vcol != NULL)
        {
            color_mode = 1;
            color_alpha = mesh.vcol_alpha;
        }
        else if (mesh.fcol != NULL)
        {
            color_mode = 2;
            color_alpha = mesh.fcol_alpha;
        }
    }

    subs = (submesh *)calloc(num_groups, sizeof(submesh));
    tess = (prc_api_write_tessellation *)calloc(num_groups, sizeof(*tess));
    items = (prc_api_write_rep_item *)calloc(num_groups, sizeof(*items));
    if (subs == NULL || tess == NULL || items == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        goto done;
    }

    /* Group the triangles by the texture they use: texnumber where the
       file gives one, else the first texture. */
    for (i = 0; i < tl.f.count; i++)
    {
        uint32_t g = 0;

        if (want_texture && mesh.texnum != NULL && tl.f.data[i] != PLY_NO_INDEX &&
            mesh.texnum[tl.f.data[i]] < num_groups)
            g = mesh.texnum[tl.f.data[i]];
        if (u32_push(&subs[g].tris, i) != 0)
        {
            fprintf(stderr, "Out of memory\n");
            goto done;
        }
    }

    ctx = prc_api_new_context(NULL);
    if (ctx == NULL)
    {
        fprintf(stderr, "prc_api_new_context failed\n");
        goto done;
    }

    for (i = 0; i < num_groups; i++)
    {
        submesh *s = &subs[i];
        prc_api_write_tessellation *t;
        prc_api_write_rep_item *ri;
        int has_normals, group_colors = color_mode;
        uint32_t k;

        if (s->tris.count == 0)
            continue;

        if (want_texture && tex[i].bytes != NULL)
        {
            int uv_everywhere = 1;

            /* Every corner needs a UV: its face's texcoord, or its vertex's. */
            if (mesh.vuv == NULL)
                for (k = 0; k < s->tris.count && uv_everywhere; k++)
                    if (tl.w.data[s->tris.data[k] * 3] == PLY_NO_INDEX)
                        uv_everywhere = 0;
            if (uv_everywhere)
            {
                s->textured = 1;
                s->texture_file = i;
            }
            else
                fprintf(stderr, "warning: some faces using %s have no texture coordinates; "
                        "that tessellation written untextured\n", mesh.texture_files[i]);
        }
        if (s->textured && group_colors)
        {
            fprintf(stderr, "warning: colours ignored on the faces textured by %s\n",
                    mesh.texture_files[i]);
            group_colors = 0;
        }

        if (build_submesh(&mesh, &tl, s, !ignore_normals, group_colors, color_alpha,
                          &has_normals) != 0)
        {
            fprintf(stderr, "Out of memory\n");
            goto done;
        }
        if (!ignore_normals && mesh.nrm != NULL && !has_normals)
            dropped_normals++;

        t = &tess[num_subs];
        ri = &items[num_subs];
        t->kind = (force_triangles || s->textured || s->colored)
            ? PRC_API_WRITE_TESS_KIND_TRIANGLES : PRC_API_WRITE_TESS_KIND_COMPRESSED;
        t->positions = s->positions;
        t->num_positions = s->num_positions;
        t->tri_indices = s->tri_indices;
        t->num_triangles = s->face_tri_count;

        if (t->kind == PRC_API_WRITE_TESS_KIND_COMPRESSED)
            t->tolerance = prc_write_tol_relative(quant_tolerance);
        t->face_tri_counts = &s->face_tri_count;
        t->num_faces = 1;
        if (has_normals)
        {
            /* One normal per position, so the normal indices are the
               position indices. */
            t->normals = s->normals;
            t->num_normals = s->num_positions;
            t->norm_indices = s->tri_indices;
        }
        else if (t->kind == PRC_API_WRITE_TESS_KIND_TRIANGLES)
        {
            /* Without supplied normals TRIANGLES would give the whole face
               one flat normal; have the reader compute per-vertex ones. */
            t->must_calculate_normals = 1;
        }
        if (s->textured)
        {
            t->tex_coords = s->tex_coords;
            t->num_tex_coords = s->num_tex_coords;
            t->tex_indices = s->tex_indices;
        }
        if (s->colored)
        {
            t->vertex_colors = s->colors;
            t->num_vertex_colors = 3 * s->face_tri_count;
            t->vertex_colors_have_alpha = color_alpha;
        }

        ri->kind = PRC_API_WRITE_RI_SURFACE;
        ri->biased_tessellation_index = num_subs + 1;
        if (s->textured)
        {
            const texture_image *ti = &tex[s->texture_file];

            /* White, so the image shows as it is. */
            ri->has_material = 1;
            ri->material_color[0] = ri->material_color[1] = ri->material_color[2] = 1.0;
            ri->material_alpha = 1.0;
            ri->has_texture = 1;
            ri->texture_image = ti->bytes;
            ri->texture_image_size = ti->size;
            ri->texture_format = ti->format;
            ri->texture_width = ti->width;
            ri->texture_height = ti->height;
        }
        else if (s->colored)
        {
            /* Without a material the writer supplies its default colour,
               which a viewer may combine with the vertex colours. White
               leaves them as they are however it combines them. */
            ri->has_material = 1;
            ri->material_color[0] = ri->material_color[1] = ri->material_color[2] = 1.0;
            ri->material_alpha = 1.0;
        }
        total_tris += s->face_tri_count;
        num_subs++;
    }

    /* Tree: one root, one part holding every tessellation. */
    memset(&node, 0, sizeof(node));
    memset(&root, 0, sizeof(root));
    {
        uint32_t d, g;
        int first = 1;

        for (g = 0; g < num_groups; g++)
        {
            if (subs[g].tris.count == 0)
                continue;
            for (d = 0; d < 3; d++)
            {
                if (first || subs[g].bbox_min[d] < node.bbox_min[d]) node.bbox_min[d] = subs[g].bbox_min[d];
                if (first || subs[g].bbox_max[d] > node.bbox_max[d]) node.bbox_max[d] = subs[g].bbox_max[d];
            }
            first = 0;
        }
    }
    node.rep_items = items;
    node.num_rep_items = num_subs;
    node.name = part_name;
    node.part_name = part_name;
    memcpy(root.bbox_min, node.bbox_min, sizeof(root.bbox_min));
    memcpy(root.bbox_max, node.bbox_max, sizeof(root.bbox_max));
    children[0] = &node;
    root.children = children;
    root.num_children = 1;
    /* The root is an empty part. Its box is still written, so it is given the
       part's box. */
    root.has_empty_part = 1;

    if (prc_api_write_prc_buffer(ctx, NULL, &root, tess, num_subs, &prc_buf, &prc_size) != 0)
    {
        fprintf(stderr, "Writing PRC failed\n");
        prc_api_print_error_stack(ctx);
        goto done;
    }
    {
        FILE *out = fopen(prc_path, "wb");

        if (out == NULL || fwrite(prc_buf, 1, prc_size, out) != prc_size)
        {
            fprintf(stderr, "Could not write %s\n", prc_path);
            if (out != NULL)
                fclose(out);
            goto done;
        }
        fclose(out);
    }
    if (prc_api_pdf_embed_prc(ctx, pdf_path, prc_buf, prc_size, NULL) != 0)
    {
        fprintf(stderr, "Writing PDF failed\n");
        prc_api_print_error_stack(ctx);
        goto done;
    }

    if (dropped_normals)
        fprintf(stderr, "warning: a zero-length normal in the file; the reader computes "
                "normals for %u tessellation(s) instead\n", dropped_normals);
    {
        uint32_t textured = 0, compressed = 0, colored = 0;

        for (i = 0; i < num_subs; i++)
        {
            if (items[i].has_texture) textured++;
            if (tess[i].vertex_colors != NULL) colored++;
            if (tess[i].kind == PRC_API_WRITE_TESS_KIND_COMPRESSED) compressed++;
        }
        printf("Wrote %s (%lu bytes) and %s: %u tessellations (%u compressed, %u textured, "
               "%u coloured), %u triangles "
               "(%u from strips); %u polygons triangulated; skipped %u faces of under 3 "
               "corners, %u with a bad vertex index, %u degenerate triangles; "
               "%u faces with a bad texcoord list\n",
               prc_path, (unsigned long)prc_size, pdf_path, num_subs, compressed, textured,
               colored, total_tris, st.strip_triangles, st.polygons_split,
               st.small_faces, st.bad_faces, st.degenerate, st.bad_wedges);
    }
    rc = 0;

done:
    if (ctx != NULL)
    {
        if (prc_buf != NULL)
            prc_api_write_prc_buffer_free(ctx, prc_buf);
        prc_api_release_context(ctx);
    }
    if (subs != NULL)
        for (i = 0; i < num_groups; i++)
            submesh_free(&subs[i]);
    for (i = 0; i < MAX_TEXTURE_FILES; i++)
        free(tex[i].bytes);
    free(subs); free(tess); free(items);
    free(tl.v.data); free(tl.w.data); free(tl.f.data);
    ply_mesh_free(&mesh);
    free(prc_default); free(pdf_default); free(part_name);
    return rc;
}
