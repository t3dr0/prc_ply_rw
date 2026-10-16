#!/usr/bin/env python3
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

"""Compare the triangles of two ASCII PLY files, independent of vertex order.

    compare_ply.py <expected.ply> <actual.ply> [--tol T] [--no-colors] [--no-uv]

Every triangle of the actual file must lie in a polygon of the expected file:
each corner matching one of the polygon's corners -- position, and colour and
s,t where both files have them -- in the polygon's own cyclic order, so the
winding is checked but the choice of diagonals is not. tristrips are expanded.
Triangle totals must agree. Positions match within T (default 1e-4; the
compressed encoder quantises them).
Exit 0 on a match, 1 otherwise.
"""

import argparse
import sys


def read_ascii_ply(path):
    with open(path, 'r', encoding='latin-1') as f:
        lines = f.read().split('\n')
    if lines[0].strip() != 'ply' or not lines[1].startswith('format ascii'):
        raise SystemExit('%s: not an ASCII PLY' % path)
    elements, i = [], 2
    while lines[i].strip() != 'end_header':
        w = lines[i].split()
        if w and w[0] == 'element':
            elements.append((w[1], int(w[2]), []))
        elif w and w[0] == 'property':
            elements[-1][2].append((w[-1], w[1] == 'list'))
        i += 1
    tokens = ' '.join(lines[i + 1:]).split()
    pos, data = 0, {}
    for name, count, props in elements:
        rows = []
        for _ in range(count):
            row = {}
            for pname, is_list in props:
                if is_list:
                    n = int(float(tokens[pos])); pos += 1
                    row[pname] = [float(t) for t in tokens[pos:pos + n]]; pos += n
                else:
                    row[pname] = float(tokens[pos]); pos += 1
            rows.append(row)
        data[name] = rows
    return data


def polygons(data):
    """Faces as vertex-index polygons; tristrips expanded to triangles."""
    polys = []
    for face in data.get('face', []):
        idx = [int(v) for v in face.get('vertex_indices', face.get('vertex_index', []))]
        if len(idx) >= 3:
            polys.append(idx)
    for strip in data.get('tristrips', []):
        run = []
        for v in [int(x) for x in strip['vertex_indices']] + [-1]:
            if v < 0:
                for k in range(len(run) - 2):
                    t = [run[k], run[k + 1], run[k + 2]] if k % 2 == 0 else [run[k + 1], run[k], run[k + 2]]
                    if len(set(t)) == 3:
                        polys.append(t)
                run = []
            else:
                run.append(v)
    return data.get('vertex', []), polys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('expected')
    ap.add_argument('actual')
    ap.add_argument('--tol', type=float, default=1e-4)
    ap.add_argument('--no-colors', action='store_true')
    ap.add_argument('--no-uv', action='store_true')
    args = ap.parse_args()

    ev, ep = polygons(read_ascii_ply(args.expected))
    av, ap_ = polygons(read_ascii_ply(args.actual))
    keys = ['x', 'y', 'z']
    have = lambda vs, k: bool(vs) and k in vs[0]
    if not args.no_colors and have(ev, 'red') and have(av, 'red'):
        keys += ['red', 'green', 'blue']
    if not args.no_uv and have(ev, 's') and have(av, 's'):
        keys += ['s', 't']
    # Positions: --tol; colours: one step of a byte; texture coordinates:
    # float32 rounding.
    tol = [args.tol if k in 'xyz' else 1.0 if k in ('red', 'green', 'blue') else 1e-5
           for k in keys]

    def corner(v):
        return tuple(v.get(k, 0.0) for k in keys)

    def close(a, b):
        return all(abs(x - y) <= t for x, y, t in zip(a, b, tol))

    expected_tris = sum(len(p) - 2 for p in ep)
    actual_tris = [tuple(corner(av[i]) for i in p) for p in ap_ if len(p) == 3]
    print('compared %s: expected %d triangles, actual %d'
          % (','.join(keys), expected_tris, len(actual_tris)))
    if expected_tris != len(actual_tris) or any(len(p) != 3 for p in ap_):
        print('FAIL: triangle counts differ')
        return 1

    # Each actual triangle must lie in an expected polygon: every corner
    # matching one of its corners, in the polygon's own cyclic order.
    polys = [[corner(ev[i]) for i in p] for p in ep]
    bad = 0
    for t in actual_tris:
        ok = False
        for p in polys:
            n = len(p)
            pos = []
            for c in t:
                m = [i for i in range(n) if close(c, p[i])]
                if not m:
                    break
                pos.append(m[0])
            if len(pos) == 3 and len(set(pos)) == 3 and                     (pos[1] - pos[0]) % n + (pos[2] - pos[1]) % n + (pos[0] - pos[2]) % n == n:
                ok = True
                break
        if not ok:
            bad += 1
            if bad <= 5:
                print('  no matching polygon, or reversed winding:', t)
    if bad:
        print('FAIL: %d triangles unmatched' % bad)
        return 1
    print('OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
