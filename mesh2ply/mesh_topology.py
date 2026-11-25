# Copyright (C) 2026 Ian Curington, Thames 3D Research
#
#    prc_ply_rw is free software: you can redistribute it and/or modify it
#    under the terms of the GNU Affero General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or (at
#    your option) any later version.
#
#    prc_ply_rw is distributed in the hope that it will be useful, but
#    WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
#    General Public License for more details.
#
#    You should have received a copy of the GNU Affero General Public License
#    along with prc_ply_rw. If not, see <https://www.gnu.org/licenses/>.
#
#    A commercial licence is also available -- see README.md.
"""Topology, valence and triangle-shape statistics of triangle meshes in mesh2ply's binary PLY layout.

  python -I mesh2ply/mesh_topology.py MESH.ply [MESH.ply ...] [--csv OUT.csv]

Needs numpy and scipy (vectorised, so a mesh of 28 million triangles takes under two minutes); no
MeshLab. Input must be binary little-endian PLY with float or uchar vertex properties, including x, y
and z, and triangles only, as a uchar count and int indices: the layout `mesh2ply` writes (with or
without `--clean`). The sized type names (float32, uint8, int32) are accepted too, so ply_export's
output can be read directly.

Per mesh, on the vertices and triangles exactly as stored (no further welding):
  - V (vertices used by a triangle), E (distinct undirected edges), F (triangles), chi = V - E + F;
  - boundary edges (used by one triangle), non-manifold edges (more than two);
  - misoriented edges: used by two triangles that traverse it in the same direction;
  - components: vertices connected by edges, so two pieces that touch at a single vertex are one
    component (that vertex is then counted as non-manifold, below);
  - boundary loops: connected pieces of the boundary-edge graph; a boundary vertex with other than two
    boundary edges is counted (`boundary_branch_vertices`), and then a loop count is not exact;
  - genus per component, g = (2 - b - chi) / 2, where b is its boundary loops; valid only when the
    mesh is orientable and manifold at every edge and vertex (the CSV says so in `genus_valid`).
Reports: total genus, the largest genus of one component, components with genus > 0, components with a
boundary, and closed components.

Shape and regularity, for relating a coder's rate to the mesh:
  - boundary vertex fraction: vertices on a boundary edge, over V;
  - non-manifold vertices: vertices whose triangles form more than one fan, where two triangles at a
    vertex are in the same fan if they share an edge through that vertex (triangles meeting at a
    non-manifold edge count as joined, since that edge is already counted above);
  - valence: number of distinct edges at each used vertex, mean and population standard deviation;
  - slivers: smallest interior angle of each triangle (degrees), median over the mesh and the fraction
    of triangles below 10 degrees. Zero-area triangles have no defined angles and are counted apart.
"""
import csv, os, sys

import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components

FIELDS = ["mesh", "vertices", "edges", "triangles", "chi", "components", "closed_components",
          "components_with_boundary", "boundary_edges", "boundary_loops", "boundary_branch_vertices",
          "nonmanifold_edges", "misoriented_edges", "genus_valid", "genus_total", "genus_max_component",
          "components_with_genus", "boundary_vertex_fraction", "nonmanifold_vertices", "valence_mean",
          "valence_std", "min_angle_median_deg", "sliver_fraction_lt10", "zero_area_triangles"]

SLIVER_DEG = 10.0
CHUNK = 2_000_000


def read_l7(path):
    data = open(path, "rb").read()
    end = data.index(b"end_header\n") + len(b"end_header\n")
    lines = data[:end].decode("ascii").split("\n")
    nv = nf = None
    props = []
    for ln in lines:
        t = ln.split()
        if t[:2] == ["element", "vertex"]:
            nv = int(t[2])
        elif t[:2] == ["element", "face"]:
            nf = int(t[2])
        elif t[:1] == ["property"] and nf is None and len(t) == 3:
            props.append((t[2], t[1]))
        elif t[:2] == ["property", "list"] and nf is not None:
            if t[2] not in ("uchar", "uint8") or t[3] not in ("int", "int32"):
                raise ValueError(f"{path}: face list is not uchar count and int indices")
    # PLY allows both the old and the sized type names (nano_prc_ply_export writes float32 and uint8)
    kinds = {"float": "<f4", "float32": "<f4", "uchar": "u1", "uint8": "u1"}
    if any(ty not in kinds for _, ty in props):
        raise ValueError(f"{path}: vertex properties other than float and uchar")
    vrec = np.dtype([(name, kinds[ty]) for name, ty in props])
    fstart = end + nv * vrec.itemsize
    rec = np.dtype([("n", "u1"), ("i", "<i4", (3,))])
    if len(data) - fstart != nf * rec.itemsize:
        raise ValueError(f"{path}: not an all-triangle mesh2ply layout file")
    verts = np.frombuffer(data, dtype=vrec, count=nv, offset=end)
    P = np.stack([verts["x"], verts["y"], verts["z"]], axis=1).astype(np.float64)
    faces = np.frombuffer(data, dtype=rec, count=nf, offset=fstart)
    if not (faces["n"] == 3).all():
        raise ValueError(f"{path}: not all faces are triangles")
    return nv, np.ascontiguousarray(faces["i"]).astype(np.int64), P


def min_angles(P, F):
    """Smallest interior angle of each triangle in degrees; NaN for zero-area triangles."""
    out = np.empty(len(F))
    for s in range(0, len(F), CHUNK):
        T = P[F[s:s + CHUNK]]
        angs = []
        for i in range(3):
            u = T[:, (i + 1) % 3] - T[:, i]
            w = T[:, (i + 2) % 3] - T[:, i]
            cr = np.linalg.norm(np.cross(u, w), axis=1)
            dt = (u * w).sum(axis=1)
            angs.append(np.degrees(np.arctan2(cr, dt)))
        m = np.minimum(np.minimum(angs[0], angs[1]), angs[2])
        m[np.linalg.norm(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]), axis=1) == 0] = np.nan
        out[s:s + CHUNK] = m
    return out


def fans_per_vertex(nv, nf, a, inv):
    """Number of fans at each vertex. Nodes are corners (corner h of half-edge h sits at a[h]); the two
    corners at each end of an edge are joined for each pair of triangles consecutive on that edge."""
    order = np.argsort(inv, kind="stable")
    same = inv[order[:-1]] == inv[order[1:]]
    h1, h2 = order[:-1][same], order[1:][same]
    del order, same
    nxt = lambda h: 3 * (h // 3) + (h % 3 + 1) % 3
    # corner of half-edge h at vertex x: h itself if a[h] == x, else the next corner of its triangle
    c1a, c1b = h1, nxt(h1)
    c2a = np.where(a[h2] == a[h1], h2, nxt(h2))
    c2b = np.where(a[h2] == a[h1], nxt(h2), h2)
    n = 3 * nf
    _, lab = components(n, np.concatenate([c1a, c1b]), np.concatenate([c2a, c2b]))
    _, first = np.unique(lab, return_index=True)
    return np.bincount(a[first], minlength=nv)


def components(n, a, b):
    g = coo_matrix((np.ones(len(a), np.int8), (a, b)), shape=(n, n))
    return connected_components(g, directed=False)


def analyse(path):
    nv, F, P = read_l7(path)
    nf = len(F)
    a = F.reshape(-1)
    b = F[:, [1, 2, 0]].reshape(-1)
    lo, hi = np.minimum(a, b), np.maximum(a, b)
    ukey = lo * nv + hi
    del lo, hi
    uk, inv, ucount = np.unique(ukey, return_inverse=True, return_counts=True)
    del ukey
    fans = fans_per_vertex(nv, nf, a, inv)
    ang = min_angles(P, F)
    del P
    E = len(uk)
    eu, ev = uk // nv, uk % nv
    boundary = ucount == 1
    nonmanifold = int((ucount > 2).sum())
    # misoriented: an edge used twice whose two half-edges have the same direction
    dkey = a * nv + b
    dk, dcount = np.unique(dkey, return_counts=True)
    misoriented = int((dcount >= 2).sum())
    del dkey, dk, dcount, inv
    used = np.zeros(nv, bool)
    used[a] = True
    V = int(used.sum())
    chi = V - E + nf
    # components over vertices, by edges
    nc_all, vlab = components(nv, eu, ev)
    comp_ids = np.unique(vlab[used])
    remap = np.full(nc_all, -1, np.int64)
    remap[comp_ids] = np.arange(len(comp_ids))
    vc = remap[vlab]
    C = len(comp_ids)
    v_per = np.bincount(vc[used], minlength=C)
    e_per = np.bincount(vc[eu], minlength=C)
    f_per = np.bincount(vc[F[:, 0]], minlength=C)
    chi_c = v_per - e_per + f_per
    # boundary loops
    bu, bv = eu[boundary], ev[boundary]
    nb = len(bu)
    bdeg = np.bincount(np.concatenate([bu, bv]), minlength=nv)
    branch = int(((bdeg > 0) & (bdeg != 2)).sum())
    if nb:
        _, blab = components(nv, bu, bv)
        bverts = np.flatnonzero(bdeg > 0)
        loop_ids, first = np.unique(blab[bverts], return_index=True)
        loops = len(loop_ids)
        b_per = np.bincount(vc[bverts[first]], minlength=C)
    else:
        loops = 0
        b_per = np.zeros(C, np.int64)
    valid = nonmanifold == 0 and misoriented == 0 and branch == 0 and not (fans > 1).any()
    g_c = (2 - b_per - chi_c) // 2
    val = (np.bincount(eu, minlength=nv) + np.bincount(ev, minlength=nv))[used]
    good = ang[~np.isnan(ang)]
    extra = dict(boundary_vertex_fraction=round(float((bdeg[used] > 0).sum()) / V, 6),
                 nonmanifold_vertices=int((fans > 1).sum()),
                 valence_mean=round(float(val.mean()), 4), valence_std=round(float(val.std()), 4),
                 min_angle_median_deg=round(float(np.median(good)), 3) if len(good) else "",
                 sliver_fraction_lt10=round(float((good < SLIVER_DEG).sum()) / nf, 6),
                 zero_area_triangles=int(nf - len(good)))
    return dict(mesh=os.path.basename(path), vertices=V, edges=E, triangles=nf, chi=chi, components=C,
                closed_components=int((b_per == 0).sum()), components_with_boundary=int((b_per > 0).sum()),
                boundary_edges=nb, boundary_loops=loops, boundary_branch_vertices=branch,
                nonmanifold_edges=nonmanifold, misoriented_edges=misoriented, genus_valid=int(valid),
                genus_total=int(g_c.sum()) if valid else "", genus_max_component=int(g_c.max()) if valid else "",
                components_with_genus=int((g_c > 0).sum()) if valid else "", **extra)


def main(argv):
    out = None
    if "--csv" in argv:
        i = argv.index("--csv")
        out = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    rows = []
    for p in argv:
        r = analyse(p)
        rows.append(r)
        print(", ".join(f"{k}={r[k]}" for k in FIELDS), flush=True)
    if out:
        with open(out, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, FIELDS)
            w.writeheader()
            w.writerows(rows)


if __name__ == "__main__":
    main(sys.argv[1:])
