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
"""Scan a folder of OFF meshes (e.g. one split of the Princeton Shape Benchmark) and write per-model
statistics to CSV, to choose which meshes to convert and use.

Its counts follow the same steps as mesh2ply --clean (README.md beside this file), and the CSV columns
share mesh2ply's report names where they mean the same, so the two can be cross-checked: run mesh2ply
with --clean --drop-invalid --report. Both weld on coordinates rounded to float32. One difference:
this scan welds on position only, while mesh2ply compares the whole vertex record (position, and
normal and colour where written), so counts can differ where vertices share a position but not a
colour or normal (COFF, NOFF and CNOFF files).

Usage:
  python -I psb_scan.py ROOT --out scan.csv [--cla classification.cla]
  python -I psb_scan.py ROOT --out scan.csv --cla classification.cla --select 20 --min-tris 5000 --seed 1

ROOT is searched recursively for *.off. A model's ID is taken from its filename (m123.off -> 123).
With --cla, each model is given its leaf class from the PSB classification file.

Per model (all counts after fan triangulation):
  - parse result, header keyword, vertex and face counts, largest polygon
  - faces with an out-of-range index (any such face excludes the model from selection)
  - degenerate and duplicate triangles (removed before the edge checks)
  - vertices merged by exact welding of the float32 coordinates, unreferenced vertices
  - boundary edges (1 face), non-manifold edges (>2 faces), misoriented edges (2 faces, same direction)
  - connected components, edge-manifold flag, per-vertex colour / normal flags

Selection rule (--select K): among models that parse, have no out-of-range faces, are edge-manifold after
welding and have at least --min-tris clean triangles, take the largest (most clean triangles; ties to the
lowest ID) in each leaf class; if more than K classes qualify, choose K classes at random with --seed from
the class names in sorted order.

Standard library only. Read-only on the input folder.
"""
import argparse, csv, os, random, re, statistics, struct, sys

sys.stdout.reconfigure(encoding="utf-8")
HEADER = re.compile(r"^(ST)?(C)?(N)?(4)?(n)?OFF(.*)$")
FLOAT32 = struct.Struct("<3f")


def lines_of(path):
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                yield line


def parse_off(path):
    it = lines_of(path)
    first = next(it)
    m = HEADER.match(first)
    if not m:
        raise ValueError(f"no OFF header: {first[:30]!r}")
    if m.group(4) or m.group(5):
        raise ValueError("4D / nD OFF not supported")
    keyword = first[: len(first) - len(m.group(6))]
    rest = m.group(6).split()
    counts = rest if len(rest) >= 2 else next(it).split()  # ModelNet-style "OFF1234 5678 0"
    nv, nf = int(counts[0]), int(counts[1])
    verts, extra = [], 0
    for _ in range(nv):
        t = next(it).split()
        # Rounded to float32, the precision mesh2ply writes and welds at (a value
        # too large for float32 raises OverflowError, a parse error here)
        verts.append(FLOAT32.unpack(FLOAT32.pack(float(t[0]), float(t[1]), float(t[2]))))
        extra = max(extra, len(t) - 3)
    faces = []
    for _ in range(nf):
        t = next(it).split()
        n = int(t[0])
        faces.append([int(x) for x in t[1:1 + n]])
    return dict(keyword=keyword, verts=verts, faces=faces, extra=extra,
                colour=bool(m.group(2)), normals=bool(m.group(3)))


def analyse(path):
    d = parse_off(path)
    verts, faces = d["verts"], d["faces"]
    nv = len(verts)
    weld, remap = {}, []
    for v in verts:
        remap.append(weld.setdefault(v, len(weld)))
    out_of_range = max_poly = degenerate = duplicate = 0
    seen, tris = set(), []
    for f in faces:
        max_poly = max(max_poly, len(f))
        if len(f) < 3 or any(i < 0 or i >= nv for i in f):
            out_of_range += 1
            continue
        g = [remap[i] for i in f]
        for k in range(1, len(g) - 1):
            t = (g[0], g[k], g[k + 1])
            if len(set(t)) < 3:
                degenerate += 1
                continue
            key = tuple(sorted(t))
            if key in seen:
                duplicate += 1
                continue
            seen.add(key)
            tris.append(t)
    undirected, directed = {}, {}
    parent = {}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for a, b, c in tris:
        for u, v in ((a, b), (b, c), (c, a)):
            k = (u, v) if u < v else (v, u)
            undirected[k] = undirected.get(k, 0) + 1
            directed[(u, v)] = directed.get((u, v), 0) + 1
        for x in (a, b, c):
            parent.setdefault(x, x)
        ra, rb, rc = find(a), find(b), find(c)
        parent[rb] = ra
        parent[find(rc)] = ra
    boundary = sum(1 for n in undirected.values() if n == 1)
    nonmanifold = sum(1 for n in undirected.values() if n > 2)
    misoriented = sum(1 for (u, v), n in undirected.items()
                      if n == 2 and (directed.get((u, v), 0) == 2 or directed.get((v, u), 0) == 2))
    return dict(header=d["keyword"], vertices=nv, faces=len(faces), max_poly=max_poly,
                out_of_range_faces=out_of_range, degenerate_tris=degenerate, duplicate_tris=duplicate,
                clean_tris=len(tris), welded_vertices_merged=nv - len(weld),
                unreferenced_vertices=len(weld) - len(parent), boundary_edges=boundary,
                nonmanifold_edges=nonmanifold, misoriented_edges=misoriented,
                components=len({find(x) for x in parent}), edge_manifold=int(nonmanifold == 0 and bool(tris)),
                vertex_colour=int(d["colour"]), vertex_normals=int(d["normals"]),
                extra_vertex_fields=d["extra"])


def read_cla(path):
    """PSB .cla: 'PSB 1', then 'nclasses nmodels', then blocks of 'name parent count' + count model IDs."""
    toks = [l.split() for l in lines_of(path)]
    if not toks or toks[0][0] != "PSB":
        raise ValueError("not a PSB classification file")
    i, cls = 2, {}
    while i < len(toks):
        name, par, n = toks[i][0], toks[i][1], int(toks[i][2])
        for j in range(n):
            cls[int(toks[i + 1 + j][0])] = (name, par)
        i += 1 + n
    return cls


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p / 100 * len(xs)))] if xs else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--out", required=True)
    ap.add_argument("--cla")
    ap.add_argument("--select", type=int)
    ap.add_argument("--min-tris", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    cls = read_cla(a.cla) if a.cla else {}
    rows = []
    for dp, _, fs in os.walk(a.root):
        for fn in sorted(fs):
            if not fn.lower().endswith(".off"):
                continue
            p = os.path.join(dp, fn)
            m = re.search(r"(\d+)", fn)
            mid = int(m.group(1)) if m else -1
            row = dict(model_id=mid, path=os.path.relpath(p, a.root),
                       leaf_class=cls.get(mid, ("", ""))[0], parent_class=cls.get(mid, ("", ""))[1])
            try:
                row.update(analyse(p), parse_error="")
            except Exception as e:  # report and carry on
                row["parse_error"] = f"{type(e).__name__}: {e}"[:200]
            rows.append(row)
    rows.sort(key=lambda r: r["model_id"])
    fields = list(dict.fromkeys(k for r in rows for k in r))
    with open(a.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)

    ok = [r for r in rows if not r["parse_error"]]
    good = [r for r in ok if r["out_of_range_faces"] == 0 and r["edge_manifold"]]
    tri = [r["clean_tris"] for r in ok]
    print(f"files {len(rows)}, parsed {len(ok)}, parse errors {len(rows) - len(ok)}")
    if tri:
        print("clean triangles: min {} p10 {} p25 {} median {} p75 {} p90 {} max {}".format(
            min(tri), pct(tri, 10), pct(tri, 25), int(statistics.median(tri)), pct(tri, 75), pct(tri, 90), max(tri)))
    print(f"no out-of-range faces and edge-manifold after welding: {len(good)}")
    print(f"{'min tris':>9} {'models':>7} {'classes':>8}")
    for n in (1000, 2000, 5000, 10000, 20000, 50000):
        sel = [r for r in good if r["clean_tris"] >= n]
        print(f"{n:>9} {len(sel):>7} {len({r['leaf_class'] for r in sel if r['leaf_class']}):>8}")

    if a.select:
        if not cls:
            sys.exit("--select needs --cla")
        best = {}
        for r in good:
            if r["clean_tris"] < a.min_tris or not r["leaf_class"]:
                continue
            b = best.get(r["leaf_class"])
            if b is None or (r["clean_tris"], -r["model_id"]) > (b["clean_tris"], -b["model_id"]):
                best[r["leaf_class"]] = r
        names = sorted(best)
        if len(names) > a.select:
            names = sorted(random.Random(a.seed).sample(names, a.select))
        print(f"\nselected {len(names)} of {len(best)} qualifying classes "
              f"(min tris {a.min_tris}, seed {a.seed}):")
        for n in names:
            r = best[n]
            print(f"  m{r['model_id']:<6} {n:30} {r['clean_tris']:>8} tris  {r['path']}")
        sp = os.path.splitext(a.out)[0] + f"_select{a.select}_min{a.min_tris}_seed{a.seed}.csv"
        with open(sp, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            w.writerows(best[n] for n in names)
        print(f"selection written to {sp}")


if __name__ == "__main__":
    main()
