# mesh2ply

Converts one OFF or PLY mesh to binary little-endian PLY in one fixed layout, either losslessly or
with a stated, counted clean-up. It is written for preparing public meshes (Stanford 3D Scanning
Repository, Princeton Shape Benchmark and others) for measurement, so that every mesh reaches the
encoder in the same format and any change to it is exact and reproducible.

Version 0.1.0. C99; no dependency beyond the C standard library and RPly (vendored in this
repository). It does not need nanoPRC.

```
mesh2ply [options] INPUT OUTPUT.ply
```

## Input

- **Detection.** The first line with anything on it: `ply` is PLY; `[ST][C][N][4][n]OFF` is OFF.
  `--input-format off|ply` overrides this.
- **OFF.** `#` starts a comment anywhere; blank lines are ignored. One vertex or face per line.
  The counts `nv nf ne` may follow the keyword on its own line, even without a space
  (`OFF1234 5678 0`, as ModelNet writes). A vertex line is `x y z`, then `nx ny nz` with `N`,
  then a colour of 3 or 4 values with `C`, then `s t` with `ST` (dropped, and reported). Colours
  are floats in [0,1] if any colour value in the file is written with `.`, `e` or `E`, otherwise
  integers in [0,255]; alpha is kept only if every vertex has one. A face line is `n i1 ... in`,
  then optionally a face colour of 3 or 4 values (dropped, and counted). `4OFF` and `nOFF` are
  not supported.
- **PLY.** ASCII, binary little-endian and binary big-endian. The vertex element's `x y z`
  (required), `nx ny nz` and `red green blue [alpha]` (or `diffuse_red` ...), of any numeric
  type: float colours as for OFF, integer colours clamped to [0,255]. The face element's list
  `vertex_indices` or `vertex_index`, of any integer types. Every other element and property is
  dropped and named in the report; `comment` and `obj_info` lines are not copied.
- **Checks.** A NaN or infinite position or normal fails the run. A face with fewer than 3
  vertices or an index outside [0, vertices) fails the run, or with `--drop-invalid` is dropped
  and counted. A mesh with no vertices or no faces, before or after cleaning, fails. Counts are
  checked against the file's size before anything is allocated.

## Lossless conversion (default)

The same vertices and faces in the same order; polygons are kept. The only changes: positions and
normals stored as float32 (round to nearest), colours as uchar, properties outside the layout
dropped. A binary PLY already in the layout comes out byte-identical. `--triangulate` fan-triangulates
polygons and changes nothing else.

## Clean-up (`--clean`)

In this order, with the report columns that count each step:

1. **Invalid faces** (`out_of_range_faces`): as above.
2. **Output precision**: positions and normals to float32, colours to uchar. The weld compares
   these converted values, which is what the encoder will read.
3. **Exact weld** (`welded_vertices_merged`): vertices whose whole output record (position, and
   normal and colour where written) is identical are merged; `-0.0` equals `+0.0`. The first keeps
   its place. A vertex at the same position with a different normal or colour is kept.
4. **Fan triangulation**: polygon `v0 v1 ... vn-1` becomes `(v0, vk, vk+1)` for `k = 1 ... n-2`.
5. **Degenerate triangles** (`degenerate_tris`): a triangle with a repeated index is dropped.
6. **Duplicate triangles** (`duplicate_tris`): a triangle whose three indices, as a set, were seen
   before is dropped, whatever its orientation; the first is kept.
7. **Unreferenced vertices** (`unreferenced_vertices`): vertices no triangle uses are removed and
   the rest renumbered in order.

Nothing else: no re-orientation, no tolerance, no removal of non-manifold edges or small
components.

## Output

```
ply
format binary_little_endian 1.0
element vertex N
property float x
property float y
property float z
property float nx            (if every vertex has a normal)
property float ny
property float nz
property uchar red           (if every vertex has a colour)
property uchar green
property uchar blue
property uchar alpha         (if every vertex has an alpha)
element face M
property list uchar int vertex_indices
end_header
```

No comments, so the size depends only on the mesh. Every value is written byte by byte, so the
same input and options give the same bytes on every platform. A polygon of more than 255 vertices
cannot be written losslessly (exit 5; use `--triangulate`). `--no-normals` and `--no-colour` leave
those out. Nothing is written unless the run succeeds: the output goes to a temporary file beside
it and is renamed at the end.

## Report

`--report FILE.csv` appends one row per run, with a header if the file is new or empty:

`input, input_format, header, bytes_in, vertices, faces, max_poly, out_of_range_faces,
degenerate_tris, duplicate_tris, welded_vertices_merged, unreferenced_vertices, vertices_out,
faces_out, triangles_out, has_normals, has_colour, has_alpha, dropped_face_colours,
dropped_properties, clean, bytes_out, exit_code`

`header` is the OFF keyword or the PLY format. `out_of_range_faces` counts faces with an index out
of range or fewer than 3 vertices. The clean-up counts are empty without `--clean`, and a field the
run did not reach is empty. `dropped_properties` lists names separated by `;`. The names that also
appear in the output of `psb_scan.py` (below) mean the same there.

## psb_scan.py

`psb_scan.py`, beside this file, scans a folder of OFF meshes (such as a split of the Princeton
Shape Benchmark) and writes per-model statistics to CSV, with an optional selection rule; see its
docstring. Its counts follow the clean-up above and can be cross-checked with mesh2ply's report.
Both weld on coordinates rounded to float32; the one difference is that it welds on position only,
so a vertex that shares a position but not a colour or normal is merged there and kept here.

## mesh_topology.py and vertex_distance.py

Two scripts beside this file measure meshes in mesh2ply's layout. They need numpy and scipy, which
the other tools here do not; see each docstring for the full definitions.

`mesh_topology.py` reports, per mesh, the counts of vertices, edges, triangles, components and
boundary loops, the genus, non-manifold edges and vertices, the boundary vertex fraction, the mean
and standard deviation of valence, and the smallest interior angle of each triangle as a median and
the fraction below 10 degrees. Lucy (28 million triangles) takes under two minutes.

`vertex_distance.py` compares an original mesh with its copy decoded from PRC (ply_import, then
ply_export). It gives the distance from each decoded vertex to the nearest original vertex and back,
and with `--quant-tolerance` counts the vertices further than sqrt(3)/2 of the tolerance from any
original vertex, the most that rounding on three axes can move one. `--metro` adds the symmetric
Metro surface distance (Hausdorff and RMS) from PyMeshLab, which is then needed too. Both scripts
read ply_export's output directly, and write one CSV row per mesh or pair with `--csv`.

## Exit codes

0 done; 1 usage; 2 I/O error; 3 parse error; 4 invalid mesh; 5 not supported (4D or nD OFF, a
polygon of more than 255 vertices without `--triangulate`, more than 2^31-1 vertices).

## Tests

```
python tests/mesh2ply/run_tests.py build/bin/Release/mesh2ply.exe
```

runs the fixtures in `tests/mesh2ply/fixtures` against golden outputs in `tests/mesh2ply/expected`,
the round trip (every output, converted again, is byte-identical), the three PLY encodings of one
cube (one output), and a loop of mutated inputs that must each exit with a code from 0 to 5.
`tests/mesh2ply/cases.py` holds the cases and their expected meshes, written out by hand;
`--regenerate` rewrites the fixtures and goldens from it.

```
python tests/mesh_stats/run_tests.py
```

tests the two measuring scripts on small meshes with known answers (a tetrahedron, two tetrahedra
meeting at one vertex, a square, a bow-tie, a sliver with a zero-area triangle, and a tetrahedron
moved a known distance), with both the old and the sized PLY type names. Its `--metro` cases run only
when PyMeshLab is installed.
