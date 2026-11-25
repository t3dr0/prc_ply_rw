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
"""Test mesh_topology.py and vertex_distance.py on small meshes with known answers.

  python tests/mesh_stats/run_tests.py

Needs numpy and scipy. The --metro case runs only if PyMeshLab is installed, and is reported as
skipped otherwise. The meshes are written to a temporary folder and removed afterwards.
"""
import math, os, struct, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "mesh2ply"))
import mesh_topology as MT     # noqa: E402
import vertex_distance as VD   # noqa: E402

TET_V = [(1, 1, 1), (1, -1, -1), (-1, 1, -1), (-1, -1, 1)]
TET_F = [(0, 1, 2), (0, 3, 1), (0, 2, 3), (1, 3, 2)]


def write_ply(path, V, F, sized=False):
    """mesh2ply's layout; sized=True uses the float32 / uint8 / int32 names, as ply_export writes."""
    fl, uc, it = ("float32", "uint8", "int32") if sized else ("float", "uchar", "int")
    with open(path, "wb") as f:
        f.write((f"ply\nformat binary_little_endian 1.0\nelement vertex {len(V)}\n"
                 f"property {fl} x\nproperty {fl} y\nproperty {fl} z\nelement face {len(F)}\n"
                 f"property list {uc} {it} vertex_indices\nend_header\n").encode("ascii"))
        for v in V:
            f.write(struct.pack("<3f", *v))
        for t in F:
            f.write(struct.pack("<B3i", 3, *t))


def meshes():
    # two tetrahedra meeting only at vertex 0 (an hourglass): the second is the first mirrored
    # through (1, 1, 1), so its other three vertices are new
    hv = TET_V + [(2 - x, 2 - y, 2 - z) for (x, y, z) in TET_V[1:]]
    m = {1: 4, 2: 5, 3: 6}
    hf = TET_F + [tuple(m.get(i, 0) for i in t) for t in TET_F]
    return {
        "tet": (TET_V, TET_F),
        "hourglass": (hv, hf),
        "square": ([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)], [(0, 1, 2), (0, 2, 3)]),
        "bowtie": ([(0, 0, 0), (1, 0, 0), (1, 1, 0), (-1, 0, 0), (-1, -1, 0)], [(0, 1, 2), (0, 3, 4)]),
        # a thin triangle (smallest angle atan(0.01 / 0.5)) and a zero-area one
        "sliver": ([(0, 0, 0), (1, 0, 0), (0.5, 0.01, 0), (0.5, 0, 0)], [(0, 1, 2), (0, 3, 1)]),
    }


EXPECT = {
    "tet": dict(vertices=4, edges=6, triangles=4, chi=2, components=1, boundary_edges=0,
                nonmanifold_edges=0, nonmanifold_vertices=0, genus_valid=1, genus_total=0,
                boundary_vertex_fraction=0.0, valence_mean=3.0, valence_std=0.0,
                min_angle_median_deg=60.0, sliver_fraction_lt10=0.0, zero_area_triangles=0),
    "hourglass": dict(vertices=7, components=1, nonmanifold_edges=0, nonmanifold_vertices=1,
                      genus_valid=0, genus_total=""),
    "square": dict(boundary_edges=4, boundary_loops=1, boundary_vertex_fraction=1.0,
                   nonmanifold_vertices=0, valence_mean=2.5, valence_std=0.5, genus_total=0,
                   min_angle_median_deg=45.0),
    "bowtie": dict(nonmanifold_edges=0, nonmanifold_vertices=1, boundary_branch_vertices=1,
                   genus_valid=0, valence_mean=2.4),
    "sliver": dict(min_angle_median_deg=round(math.degrees(math.atan2(0.01, 0.5)), 3),
                   sliver_fraction_lt10=0.5, zero_area_triangles=1),
}

failures = 0


def check(label, got, want, tol=1e-6):
    global failures
    ok = (abs(float(got) - float(want)) <= tol) if isinstance(want, float) else got == want
    if not ok:
        failures += 1
        print(f"FAIL {label}: got {got!r}, want {want!r}")


def main():
    with tempfile.TemporaryDirectory() as d:
        for name, (V, F) in meshes().items():
            p = os.path.join(d, name + ".ply")
            write_ply(p, V, F)
            r = MT.analyse(p)
            for k, want in EXPECT[name].items():
                check(f"mesh_topology {name} {k}", r[k], want, tol=1e-3)
            # the sized type names must give the same answers
            ps = os.path.join(d, name + "_sized.ply")
            write_ply(ps, V, F, sized=True)
            rs = MT.analyse(ps)
            for k in MT.FIELDS[1:]:
                check(f"mesh_topology {name} sized {k}", rs[k], r[k])

        a = os.path.join(d, "tet.ply")
        r = VD.compare(a, a, 0.01)
        for k in ("fwd_max", "fwd_rms", "bwd_max"):
            check(f"vertex_distance identical {k}", float(r[k]), 0.0)
        check("vertex_distance identical over_bound", r["over_bound"], 0)

        # every vertex moved by s along x: distance s, in diagonals and in tolerances
        diag = math.sqrt(12)
        s = 0.05
        b = os.path.join(d, "tet_moved.ply")
        write_ply(b, [(x + s, y, z) for (x, y, z) in TET_V], TET_F)
        ftol = 0.02   # tau = 0.02 diag; bound sqrt(3)/2 tau = 0.06; s = 0.05 is within it
        r = VD.compare(a, b, ftol)
        check("vertex_distance moved fwd_max", float(r["fwd_max"]), s / diag, tol=1e-6)
        check("vertex_distance moved bwd_max", float(r["bwd_max"]), s / diag, tol=1e-6)
        check("vertex_distance moved fwd_max_tau", r["fwd_max_tau"], round(s / (ftol * diag), 4), tol=1e-4)
        check("vertex_distance moved over_bound", r["over_bound"], 0)
        r = VD.compare(a, b, 0.01)   # tau = 0.01 diag; bound 0.03 < s: all four over
        check("vertex_distance moved over_bound, tighter tau", r["over_bound"], 4)

        try:
            import pymeshlab  # noqa: F401
        except ImportError:
            print("SKIP --metro (PyMeshLab not installed)")
        else:
            r = VD.compare(a, a, 0.01, do_metro=True)
            check("vertex_distance --metro identical hausdorff", float(r["metro_hausdorff"]), 0.0, tol=1e-6)
            # a square moved 0.1 along its normal: every surface point is 0.1 from the other surface
            sq = meshes()["square"]
            c, e = os.path.join(d, "sq.ply"), os.path.join(d, "sq_up.ply")
            write_ply(c, *sq)
            write_ply(e, [(x, y, z + 0.1) for (x, y, z) in sq[0]], sq[1])
            r = VD.compare(c, e, None, do_metro=True)
            check("vertex_distance --metro offset hausdorff", float(r["metro_hausdorff"]), 0.1 / math.sqrt(2),
                  tol=1e-5)
            check("vertex_distance --metro offset rms", float(r["metro_rms"]), 0.1 / math.sqrt(2), tol=1e-5)

    print("mesh_stats tests:", "all passed" if failures == 0 else f"{failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
