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
"""Distances between an original mesh and its decoded copy, both in mesh2ply's PLY layout.

  python -I mesh2ply/vertex_distance.py ORIGINAL.ply DECODED.ply [--quant-tolerance F] [--metro]
                                        [--metro-samples N] [--csv OUT.csv]

A quick check of the per-vertex error bound of PRC compressed tessellation, written by ply_import and
read back by ply_export, and optionally the Metro surface distance. Needs numpy and scipy (k-d tree).
Reads files with mesh_topology.read_l7, so it must sit beside mesh_topology.py. --csv appends one row,
writing the header when the file is new.

--metro adds the symmetric surface distance (Cignoni, Rocchini and Scopigno, "Metro: measuring error on
simplified surfaces", Computer Graphics Forum 17(2), 1998) from PyMeshLab's
get_hausdorff_distance, run in both directions. Each direction samples the vertices, edges and faces of
one mesh (N samples, default 10 x the larger vertex count; PyMeshLab adds the vertices and edges to N, so
it reports more) and finds the nearest point on the other surface. The search radius is the whole
bounding-box diagonal, so no distance is clipped. PyMeshLab (pip package, not the MeshLab application)
is imported only when --metro is given.

Only vertices used by a triangle are counted. PRC renumbers vertices, so there is no vertex
correspondence: each decoded vertex is matched to the nearest original vertex.
  - forward (decoded to original): max, RMS and mean distance. Because the nearest original vertex can be
    closer than a decoded vertex's true source, this is a lower bound on how far vertices moved, and an
    upper bound on each decoded vertex's distance to the original surface;
  - backward (original to decoded): max distance, which grows if a region of the mesh is lost.
Distances are given as fractions of the original's bounding-box diagonal. With --quant-tolerance F (the
importers' tolerance, as a fraction of the diagonal) they are also given in units of the tolerance
tau = F x diagonal, with the count of decoded vertices further than sqrt(3)/2 tau from any original vertex:
rounding to the nearest multiple of tau on each of three local axes moves a vertex at most that far, so
any such vertex points to edge collapse, a prediction drift or a bug.
"""
import csv, os, sys

import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I leaves the script's folder out
from mesh_topology import read_l7

FIELDS = ["original", "decoded", "orig_vertices", "dec_vertices", "diagonal", "quant_tolerance",
          "fwd_max", "fwd_rms", "fwd_mean", "bwd_max", "fwd_max_tau", "fwd_rms_tau", "bwd_max_tau",
          "over_bound", "metro_samples", "metro_fwd_max", "metro_fwd_rms", "metro_bwd_max", "metro_bwd_rms",
          "metro_hausdorff", "metro_rms", "metro_hausdorff_tau", "metro_rms_tau"]
BOUND = np.sqrt(3) / 2
METRO_FIELDS = FIELDS[FIELDS.index("metro_samples"):]


def metro(orig, dec, n):
    """Symmetric Metro distance with PyMeshLab: (fwd, bwd), each PyMeshLab's result dict."""
    import pymeshlab
    ms = pymeshlab.MeshSet()
    ms.load_new_mesh(os.path.abspath(orig))
    ms.load_new_mesh(os.path.abspath(dec))
    res = []
    for s, t in ((1, 0), (0, 1)):   # forward: decoded sampled against original; backward: the reverse
        res.append(ms.get_hausdorff_distance(sampledmesh=s, targetmesh=t, samplevert=True, sampleedge=True,
                                             sampleface=True, samplenum=n,
                                             maxdist=pymeshlab.PercentageValue(100)))
    return res


def used_positions(path):
    nv, F, P = read_l7(path)
    used = np.zeros(nv, bool)
    used[F.reshape(-1)] = True
    return P[used]


def compare(orig, dec, ftol=None, do_metro=False, samples=None):
    A, B = used_positions(orig), used_positions(dec)
    diag = float(np.linalg.norm(A.max(0) - A.min(0)))
    fwd, _ = cKDTree(A).query(B)
    bwd, _ = cKDTree(B).query(A)
    r = dict(original=os.path.basename(orig), decoded=os.path.basename(dec), orig_vertices=len(A),
             dec_vertices=len(B), diagonal=round(diag, 9), quant_tolerance=ftol if ftol else "",
             fwd_max=float(fwd.max()) / diag, fwd_rms=float(np.sqrt((fwd ** 2).mean())) / diag,
             fwd_mean=float(fwd.mean()) / diag, bwd_max=float(bwd.max()) / diag)
    if ftol:
        tau = ftol * diag
        r.update(fwd_max_tau=round(float(fwd.max()) / tau, 4),
                 fwd_rms_tau=round(float(np.sqrt((fwd ** 2).mean())) / tau, 4),
                 bwd_max_tau=round(float(bwd.max()) / tau, 4),
                 over_bound=int((fwd > BOUND * tau).sum()))
    else:
        r.update(fwd_max_tau="", fwd_rms_tau="", bwd_max_tau="", over_bound="")
    for k in ("fwd_max", "fwd_rms", "fwd_mean", "bwd_max"):
        r[k] = f"{r[k]:.6g}"
    r.update({k: "" for k in METRO_FIELDS})
    if do_metro:
        n = samples or 10 * max(len(A), len(B))
        f, b = metro(orig, dec, n)
        # RMS over both directions, weighting each by its number of samples
        rms = np.sqrt((f["n_samples"] * f["RMS"] ** 2 + b["n_samples"] * b["RMS"] ** 2)
                      / (f["n_samples"] + b["n_samples"]))
        haus = max(f["max"], b["max"])
        r.update(metro_samples=f"{f['n_samples']}+{b['n_samples']}",
                 metro_fwd_max=f"{f['max'] / diag:.6g}", metro_fwd_rms=f"{f['RMS'] / diag:.6g}",
                 metro_bwd_max=f"{b['max'] / diag:.6g}", metro_bwd_rms=f"{b['RMS'] / diag:.6g}",
                 metro_hausdorff=f"{haus / diag:.6g}", metro_rms=f"{rms / diag:.6g}")
        if ftol:
            r.update(metro_hausdorff_tau=round(haus / (ftol * diag), 4),
                     metro_rms_tau=round(float(rms) / (ftol * diag), 4))
    return r


def main(argv):
    out = ftol = None
    if "--csv" in argv:
        i = argv.index("--csv")
        out = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    if "--quant-tolerance" in argv:
        i = argv.index("--quant-tolerance")
        ftol = float(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    samples = None
    if "--metro-samples" in argv:
        i = argv.index("--metro-samples")
        samples = int(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    do_metro = "--metro" in argv
    argv = [a for a in argv if a != "--metro"]
    if len(argv) != 2:
        sys.exit(__doc__)
    r = compare(argv[0], argv[1], ftol, do_metro, samples)
    print(", ".join(f"{k}={r[k]}" for k in FIELDS), flush=True)
    if out:
        new = not os.path.exists(out)
        with open(out, "a", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, FIELDS)
            if new:
                w.writeheader()
            w.writerow(r)


if __name__ == "__main__":
    main(sys.argv[1:])
