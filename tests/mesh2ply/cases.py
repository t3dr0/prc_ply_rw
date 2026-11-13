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
"""mesh2ply's test cases (specification section 10).

Each case gives an input file, the options, the exit code expected, and for
a run that succeeds the mesh expected, written out by hand: vertices
(position, normal or None, colour or None) and faces. golden_bytes() turns
that mesh into the output's expected bytes with its own writer, which shares
no code with mesh2ply, so the goldens test the tool rather than repeat it.
"""
import struct

# ---------------------------------------------------------------- writer


def golden_bytes(verts, faces, normals=False, colour=False, alpha=False):
    """Binary little-endian PLY in the layout of section 7."""
    h = ["ply", "format binary_little_endian 1.0", "element vertex %d" % len(verts),
         "property float x", "property float y", "property float z"]
    if normals:
        h += ["property float nx", "property float ny", "property float nz"]
    if colour:
        h += ["property uchar red", "property uchar green", "property uchar blue"]
        if alpha:
            h += ["property uchar alpha"]
    h += ["element face %d" % len(faces), "property list uchar int vertex_indices", "end_header"]
    out = bytearray(("\n".join(h) + "\n").encode("ascii"))
    for p, n, c in verts:
        out += struct.pack("<3f", *p)
        if normals:
            out += struct.pack("<3f", *n)
        if colour:
            out += bytes(c[:4] if alpha else c[:3])
    for f in faces:
        out += struct.pack("<B", len(f)) + struct.pack("<%di" % len(f), *f)
    return bytes(out)


# ---------------------------------------------------------------- inputs

CUBE_P = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
CUBE_Q = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
CUBE_T = [t for q in CUBE_Q for t in ((q[0], q[1], q[2]), (q[0], q[2], q[3]))]


def off_text(header, verts, faces, counts_line=None):
    lines = [header]
    lines.append(counts_line if counts_line is not None else "%d %d 0" % (len(verts), len(faces)))
    lines += [" ".join(str(x) for x in v) for v in verts]
    lines += [" ".join(str(x) for x in f) for f in faces]
    return ("\n".join(lines) + "\n").encode("ascii")


def ply_bytes(fmt, vprops, vrows, fprops, frows, extra_header=()):
    """A PLY in any encoding. vprops/fprops: [(type, name)] or [("list", ctype, vtype, name)]."""
    h = ["ply", "format %s 1.0" % fmt] + list(extra_header)
    h.append("element vertex %d" % len(vrows))
    for p in vprops:
        h.append("property " + " ".join(p))
    h.append("element face %d" % len(frows))
    for p in fprops:
        h.append("property " + " ".join(p))
    h.append("end_header")
    out = bytearray(("\n".join(h) + "\n").encode("ascii"))
    codes = {"char": "b", "uchar": "B", "short": "h", "ushort": "H", "int": "i", "uint": "I",
             "float": "f", "double": "d", "int8": "b", "uint8": "B", "int32": "i", "float32": "f",
             "float64": "d"}
    end = "<" if fmt == "binary_little_endian" else ">"

    def emit(props, row):
        nonlocal out
        if fmt == "ascii":
            parts = []
            for p, v in zip(props, row):
                if p[0] == "list":
                    parts += [str(len(v))] + [str(x) for x in v]
                else:
                    parts.append(repr(v) if isinstance(v, float) else str(v))
            out += (" ".join(parts) + "\n").encode("ascii")
            return
        for p, v in zip(props, row):
            if p[0] == "list":
                out += struct.pack(end + codes[p[1]], len(v))
                out += struct.pack(end + codes[p[2]] * len(v), *v)
            else:
                out += struct.pack(end + codes[p[0]], v)

    for r in vrows:
        emit(vprops, r)
    for r in frows:
        emit(fprops, r)
    return bytes(out)


XYZ = [("float", "x"), ("float", "y"), ("float", "z")]
FACE = [("list", "uchar", "int", "vertex_indices")]


def cube_ply(fmt):
    return ply_bytes(fmt, XYZ, [list(map(float, p)) for p in CUBE_P], FACE, [[list(q)] for q in CUBE_Q])


def plain(ps):
    return [(p, None, None) for p in ps]


# ---------------------------------------------------------------- cases
# name, file extension, input bytes, options, exit code, expected mesh or
# None, expected report fields (a subset, compared as text)

def cases():
    c = []

    def add(name, ext, data, opts, code, mesh=None, report=None):
        c.append(dict(name=name, ext=ext, data=data, opts=opts, code=code, mesh=mesh, report=report or {}))

    cube_mesh = dict(verts=plain(CUBE_P), faces=CUBE_Q)
    cube_tris = dict(verts=plain(CUBE_P), faces=CUBE_T)

    # --- OFF
    add("off_cube", "off", off_text("OFF", CUBE_P, [(4,) + q for q in CUBE_Q]), [], 0, cube_mesh,
        dict(input_format="off", header="OFF", vertices="8", faces="6", max_poly="4", out_of_range_faces="0",
             degenerate_tris="", vertices_out="8", faces_out="6", triangles_out="12", clean="0"))
    add("off_cube_clean", "off", off_text("OFF", CUBE_P, [(4,) + q for q in CUBE_Q]), ["--clean"], 0,
        cube_tris, dict(degenerate_tris="0", duplicate_tris="0", welded_vertices_merged="0",
                        unreferenced_vertices="0", faces_out="12", triangles_out="12", clean="1"))
    add("off_comments", "off",
        b"# a comment first\n\nOFF # keyword\n\n8 6 0   # counts\n" +
        b"".join(b"%d %d %d\n\n" % p for p in CUBE_P) + b"# between\n" +
        b"".join(b"4 %d %d %d %d # face\n" % q for q in CUBE_Q), [], 0, cube_mesh)
    add("off_modelnet", "off", off_text("OFF8 6 0", CUBE_P, [(4,) + q for q in CUBE_Q], counts_line="")
        .replace(b"\n\n", b"\n"), [], 0, cube_mesh, dict(header="OFF"))

    tri_p = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    add("coff_int", "off", off_text("COFF", [p + c for p, c in zip(tri_p, [(255, 0, 0), (0, 255, 0), (300, -4, 9)])],
                                    [(3, 0, 1, 2)]), [], 0,
        dict(verts=[(p, None, c) for p, c in zip(tri_p, [(255, 0, 0, 255), (0, 255, 0, 255), (255, 0, 9, 255)])],
             faces=[(0, 1, 2)], colour=True), dict(has_colour="1", has_alpha="0"))
    add("coff_float", "off", off_text("COFF", [p + c for p, c in zip(tri_p, [(1.0, 0, 0), (0.5, 0.2, 2), (0, 0, 1)])],
                                      [(3, 0, 1, 2)]), [], 0,
        dict(verts=[(p, None, c) for p, c in zip(tri_p, [(255, 0, 0, 255), (128, 51, 255, 255), (0, 0, 255, 255)])],
             faces=[(0, 1, 2)], colour=True))
    add("coff_rgba", "off", off_text("COFF", [p + c for p, c in zip(tri_p, [(1, 2, 3, 4), (5, 6, 7, 8), (9, 10, 11, 12)])],
                                     [(3, 0, 1, 2)]), [], 0,
        dict(verts=[(p, None, c) for p, c in zip(tri_p, [(1, 2, 3, 4), (5, 6, 7, 8), (9, 10, 11, 12)])],
             faces=[(0, 1, 2)], colour=True, alpha=True), dict(has_alpha="1"))
    add("coff_some_alpha", "off", off_text("COFF", [p + c for p, c in zip(tri_p, [(1, 2, 3, 4), (5, 6, 7), (9, 10, 11)])],
                                           [(3, 0, 1, 2)]), [], 0,
        dict(verts=[(p, None, c) for p, c in zip(tri_p, [(1, 2, 3, 255), (5, 6, 7, 255), (9, 10, 11, 255)])],
             faces=[(0, 1, 2)], colour=True), dict(has_alpha="0", dropped_properties="alpha"))
    nrm = [(0, 0, 1), (0, 0, 1), (0.0, 0.6, 0.8)]
    add("noff", "off", off_text("NOFF", [p + n for p, n in zip(tri_p, nrm)], [(3, 0, 1, 2)]), [], 0,
        dict(verts=[(p, n, None) for p, n in zip(tri_p, nrm)], faces=[(0, 1, 2)], normals=True),
        dict(has_normals="1"))
    add("cnoff", "off", off_text("CNOFF", [p + n + (10, 20, 30) for p, n in zip(tri_p, nrm)], [(3, 0, 1, 2)]), [], 0,
        dict(verts=[(p, n, (10, 20, 30, 255)) for p, n in zip(tri_p, nrm)], faces=[(0, 1, 2)], normals=True,
             colour=True), dict(has_normals="1", has_colour="1", header="CNOFF"))
    add("stoff", "off", off_text("STOFF", [p + (0.5, 0.25) for p in tri_p], [(3, 0, 1, 2)]), [], 0,
        dict(verts=plain(tri_p), faces=[(0, 1, 2)]), dict(dropped_properties="st", header="STOFF"))
    add("off_face_colours", "off", off_text("OFF", tri_p, [(3, 0, 1, 2, 255, 0, 0)]), [], 0,
        dict(verts=plain(tri_p), faces=[(0, 1, 2)]), dict(dropped_face_colours="1"))
    add("off_truncated", "off", off_text("OFF", CUBE_P, [(4,) + q for q in CUBE_Q])[:-20], [], 3)
    add("off_nan", "off", off_text("OFF", [(0, 0, 0), ("nan", 0, 0), (0, 1, 0)], [(3, 0, 1, 2)]), [], 4)
    add("off_out_of_range", "off", off_text("OFF", tri_p, [(3, 0, 1, 2), (3, 0, 1, 7)]), [], 4,
        None, dict(out_of_range_faces="1", exit_code="4"))
    add("off_out_of_range_dropped", "off", off_text("OFF", tri_p, [(3, 0, 1, 2), (3, 0, 1, 7)]),
        ["--drop-invalid"], 0, dict(verts=plain(tri_p), faces=[(0, 1, 2)]), dict(out_of_range_faces="1"))
    add("off_two_vertex_face", "off", off_text("OFF", tri_p, [(3, 0, 1, 2), (2, 0, 1)]), [], 4)
    add("off_4d", "off", off_text("4OFF", [p + (1,) for p in tri_p], [(3, 0, 1, 2)]), [], 5)

    # --- PLY: the same cube in all three encodings gives the same output
    for fmt in ("ascii", "binary_little_endian", "binary_big_endian"):
        add("ply_cube_" + fmt, "ply", cube_ply(fmt), [], 0, cube_mesh, dict(input_format="ply", header=fmt))
    add("ply_vertex_index", "ply",
        ply_bytes("ascii", XYZ, [list(map(float, p)) for p in CUBE_P], [("list", "uchar", "int", "vertex_index")],
                  [[list(q)] for q in CUBE_Q]), [], 0, cube_mesh)
    dpos = [(0.1, 0.2, 0.3), (1e-8, 2.5, -3.75), (123456.789, 0.0, 1.0)]
    add("ply_double", "ply",
        ply_bytes("binary_big_endian", [("double", "x"), ("double", "y"), ("double", "z")], [list(p) for p in dpos],
                  [("list", "uchar", "uint", "vertex_indices")], [[[0, 1, 2]]]), [], 0,
        dict(verts=plain(dpos), faces=[(0, 1, 2)]))
    add("ply_float_colour", "ply",
        ply_bytes("ascii", XYZ + [("float", "red"), ("float", "green"), ("float", "blue")],
                  [list(map(float, p)) + c for p, c in zip(tri_p, [[1.0, 0.0, 0.5], [0.2, 0.4, 0.6], [2.0, -1.0, 0.999]])],
                  FACE, [[[0, 1, 2]]]), [], 0,
        dict(verts=[(p, None, c) for p, c in zip(tri_p, [(255, 0, 128, 255), (51, 102, 153, 255), (255, 0, 255, 255)])],
             faces=[(0, 1, 2)], colour=True))
    add("ply_extra_props", "ply",
        ply_bytes("binary_little_endian", XYZ + [("float", "confidence"), ("float", "intensity")],
                  [list(map(float, p)) + [0.5, 0.25] for p in tri_p], FACE + [("uchar", "flags")], [[[0, 1, 2], 7]],
                  extra_header=["comment made by hand", "obj_info test"]), [], 0,
        dict(verts=plain(tri_p), faces=[(0, 1, 2)]), dict(dropped_properties="confidence;intensity;flags"))
    import math
    ring = [(math.cos(2 * math.pi * k / 300), math.sin(2 * math.pi * k / 300), 0.0) for k in range(300)]
    big = ply_bytes("binary_little_endian", XYZ, [list(p) for p in ring], [("list", "ushort", "int", "vertex_indices")],
                    [[list(range(300))]])
    add("ply_300gon_lossless", "ply", big, [], 5)
    add("ply_300gon_triangulate", "ply", big, ["--triangulate"], 0,
        dict(verts=plain(ring), faces=[(0, k, k + 1) for k in range(1, 299)]),
        dict(max_poly="300", faces_out="298", triangles_out="298", degenerate_tris=""))

    # --- clean-up
    add("clean_degenerate", "off", off_text("OFF", tri_p + [(1, 1, 0)], [(3, 0, 1, 2), (3, 1, 1, 3)]), ["--clean"], 0,
        dict(verts=plain(tri_p), faces=[(0, 1, 2)]),
        dict(degenerate_tris="1", unreferenced_vertices="1", vertices_out="3", faces_out="1"))
    add("clean_duplicate_reversed", "off", off_text("OFF", tri_p, [(3, 0, 1, 2), (3, 2, 1, 0)]), ["--clean"], 0,
        dict(verts=plain(tri_p), faces=[(0, 1, 2)]), dict(duplicate_tris="1", faces_out="1"))
    add("clean_weld", "off",
        off_text("OFF", [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 0, 0), (1, 1, 0)], [(3, 0, 1, 2), (3, 3, 4, 2)]),
        ["--clean"], 0, dict(verts=plain([(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)]), faces=[(0, 1, 2), (1, 3, 2)]),
        dict(welded_vertices_merged="1", vertices_out="4"))
    add("clean_colour_seam", "off",
        off_text("COFF", [(0, 0, 0, 255, 0, 0), (1, 0, 0, 255, 0, 0), (0, 1, 0, 255, 0, 0), (1, 0, 0, 0, 0, 255),
                          (1, 1, 0, 0, 0, 255)], [(3, 0, 1, 2), (3, 3, 4, 2)]), ["--clean"], 0,
        dict(verts=[((0, 0, 0), None, (255, 0, 0, 255)), ((1, 0, 0), None, (255, 0, 0, 255)),
                    ((0, 1, 0), None, (255, 0, 0, 255)), ((1, 0, 0), None, (0, 0, 255, 255)),
                    ((1, 1, 0), None, (0, 0, 255, 255))], faces=[(0, 1, 2), (3, 4, 2)], colour=True),
        dict(welded_vertices_merged="0", vertices_out="5"))
    add("clean_unreferenced", "off", off_text("OFF", [(5, 5, 5)] + tri_p, [(3, 1, 2, 3)]), ["--clean"], 0,
        dict(verts=plain(tri_p), faces=[(0, 1, 2)]), dict(unreferenced_vertices="1", vertices_out="3"))
    add("clean_negative_zero", "off",
        off_text("OFF", [(0.0, 0, 0), (1, 0, 0), (0, 1, 0), ("-0.0", "-0.0", 0)], [(3, 0, 1, 2), (3, 3, 1, 2)]),
        ["--clean"], 0, dict(verts=plain(tri_p), faces=[(0, 1, 2)]),
        dict(welded_vertices_merged="1", duplicate_tris="1"))
    return c
