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

"""Write the small PLY files and texture in tests/data. Standard library only.

  cube.ply            ASCII, 8 vertices, 6 quads (12 triangles once split)
  cube_colors.ply     ASCII, the cube with byte vertex colours, one per corner
  quad_textured.ply   ASCII, 2 triangles with s,t and a TextureFile comment
  checker.png         32x32 RGB checkerboard for quad_textured.ply
  strip.ply           ASCII, a tristrips element of 6 corners (4 triangles)
"""

import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')

CUBE_V = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
          (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
# Outward-facing quads, counter-clockwise seen from outside.
CUBE_F = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
          (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]


def header(nv, vprops, nf, fprops, comments=()):
    lines = ['ply', 'format ascii 1.0']
    lines += ['comment ' + c for c in comments]
    lines.append('element vertex %d' % nv)
    lines += ['property ' + p for p in vprops]
    if nf is not None:
        lines.append('element face %d' % nf)
        lines += ['property ' + p for p in fprops]
    lines.append('end_header')
    return '\n'.join(lines) + '\n'


def write(name, text):
    with open(os.path.join(DATA, name), 'w', newline='\n') as f:
        f.write(text)


def cube():
    out = header(8, ['float x', 'float y', 'float z'],
                 6, ['list uchar int vertex_indices'], ['quad-faced unit cube'])
    out += ''.join('%g %g %g\n' % v for v in CUBE_V)
    out += ''.join('4 %d %d %d %d\n' % f for f in CUBE_F)
    write('cube.ply', out)


def cube_colors():
    out = header(8, ['float x', 'float y', 'float z',
                     'uchar red', 'uchar green', 'uchar blue'],
                 6, ['list uchar int vertex_indices'], ['unit cube, colour = corner position'])
    out += ''.join('%g %g %g %d %d %d\n' % (v + tuple(255 * c for c in v)) for v in CUBE_V)
    out += ''.join('4 %d %d %d %d\n' % f for f in CUBE_F)
    write('cube_colors.ply', out)


def quad_textured():
    verts = [(0, 0, 0, 0, 0), (1, 0, 0, 1, 0), (1, 1, 0, 1, 1), (0, 1, 0, 0, 1)]
    out = header(4, ['float x', 'float y', 'float z', 'float s', 'float t'],
                 2, ['list uchar int vertex_indices'], ['TextureFile checker.png'])
    out += ''.join('%g %g %g %g %g\n' % v for v in verts)
    out += '3 0 1 2\n3 0 2 3\n'
    write('quad_textured.ply', out)


def strip():
    # A 2x3 grid of points, one strip zig-zagging along it.
    verts = [(0, 0, 0), (0, 1, 0), (1, 0, 0), (1, 1, 0), (2, 0, 0), (2, 1, 0)]
    lines = ['ply', 'format ascii 1.0', 'comment one triangle strip',
             'element vertex 6', 'property float x', 'property float y', 'property float z',
             'element tristrips 1', 'property list int int vertex_indices', 'end_header']
    out = '\n'.join(lines) + '\n'
    out += ''.join('%g %g %g\n' % v for v in verts)
    out += '6 0 1 2 3 4 5\n'
    write('strip.ply', out)


def checker_png():
    size, cell = 32, 8
    rows = []
    for y in range(size):
        row = bytearray([0])  # filter type None
        for x in range(size):
            on = ((x // cell) + (y // cell)) % 2
            row += bytes((230, 60, 40)) if on else bytes((240, 240, 240))
        rows.append(bytes(row))
    raw = b''.join(rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    with open(os.path.join(DATA, 'checker.png'), 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    os.makedirs(DATA, exist_ok=True)
    cube()
    cube_colors()
    quad_textured()
    strip()
    checker_png()
    print('wrote', ', '.join(sorted(os.listdir(DATA))))
