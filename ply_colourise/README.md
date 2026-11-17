# ply_colourise

Adds one synthetic colour to every vertex of a binary PLY mesh in [mesh2ply](../mesh2ply/README.md)'s
layout. A vertex's colour is set by its coordinate along one axis, looked up in Kenneth Moreland's
*Smooth Cool Warm* colour map. The result is a smooth, low-frequency colouring, much like a stress or
temperature plot, which is how CAD and simulation results are often coloured in PRC files. Positions,
normals and faces are copied unchanged, so a mesh with colours differs from one without them only in
its colour data.

Version: colourise specification 0.4. C99; no dependency beyond the C standard library, RPly and
mesh2ply's PLY reader and writer, all compiled in from this repository. It does not need nanoPRC.

```
ply_colourise [options] INPUT.ply OUTPUT.ply
```

| Option | Effect |
|---|---|
| `--axis x\|y\|z` | The coordinate that sets the colour. Default `y`. `--axis=z` also works. |
| `--replace` | If the input already has vertex colours, replace them and drop any alpha. Without it, a coloured input is refused (exit 4). |
| `--report FILE.csv` | Append one row for this run (below). The header is written if the file is new or empty. |
| `--quiet` | No summary line on stderr. |
| `--help`, `--version` | `--version` prints the specification version the program implements. |

INPUT and OUTPUT must be different files (exit 2). The output is written to a temporary file beside
it and renamed at the end, so a failed run never leaves a partial file.

## Field

Coordinates are the float32 values in the file, converted exactly to double; all arithmetic is in
double precision. With `min` and `max` the smallest and largest coordinate on the chosen axis over all
vertices, each vertex gets

`t = (v - min) / (max - min)`, clamped to [0, 1], or `t = 0` for every vertex if `max = min`.

Two vertices at the same position always get the same colour, so no colour seam is created.

## Colour map

Row `i = floor(t * 255 + 0.5)` (halves round up), clamped to [0, 255], of Moreland's 256-entry byte
table *Smooth Cool Warm*, `smooth-cool-warm-table-byte-0256.csv` from
<https://www.kennethmoreland.com/color-advice/> (accessed 16 November 2026; SHA-256
`68a67431a6b1eb42930e4f1f33ff851e006b956655680adc178cc80a0c83ead4`). The table is in
`src/plyc_field.c`, generated from that file and unchanged; no entry is interpolated. So a mesh uses
at most 256 distinct colours. Rows for reference (index: R, G, B): 0: 59, 76, 192. 64: 141, 176, 254.
128: 221, 220, 220. 191: 244, 154, 123. 255: 180, 4, 38.

The map follows K. Moreland, "Diverging Color Maps for Scientific Visualization", in *Advances in
Visual Computing*, Proc. 5th International Symposium on Visual Computing (ISVC 2009), Lecture Notes
in Computer Science, Springer, 2009, pp. 92-103,
[doi:10.1007/978-3-642-10520-3_9](https://doi.org/10.1007/978-3-642-10520-3_9). The source page
states that Smooth Cool Warm is covered by ParaView's BSD licence; see
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Input

Exactly mesh2ply's layout, which `mesh2ply` writes from any OFF or PLY; anything else is refused
(exit 4) with a message naming the first line that differs:

- `format binary_little_endian 1.0`; no `comment` or `obj_info` lines; each header line ends in a
  line feed.
- Elements `vertex` then `face`, nothing else.
- Vertex properties, in order: `float x y z`; optionally `float nx ny nz`; optionally
  `uchar red green blue` and then optionally `uchar alpha`.
- Face property `list uchar int vertex_indices` only.
- The file is exactly as long as the header, the vertex records and the faces the counts imply:
  exit 3 if shorter, exit 4 if longer.
- A NaN or infinite position or normal is exit 5. A face index that is negative or not below the
  vertex count is exit 4. mesh2ply never writes either.

The layout is checked first, by this program; the data is then read with mesh2ply's PLY reader
(RPly underneath).

## Output

The input's header with `property uchar red`, `green` and `blue` after the last normal property (or
after `z`), and without any input colour or alpha properties. Each vertex record is its position, its
normal if the input has one, and the three colour bytes. The faces are written as they were read.
Written by mesh2ply's writer, byte by byte, least significant byte first, so the same input gives the
same bytes on every platform.

A summary line goes to stderr unless `--quiet`:

```
INPUT: N vertices, y MIN to MAX, D distinct colours -> OUTPUT
```

## Report

`--report FILE.csv` appends one row per run, lines ending in CR LF:

`input, output, spec_version, axis, replaced, vertices, faces, min, max, distinct_colours, bytes_in,
bytes_out, sha256_out, exit_code`

`min` and `max` are the extremes on the chosen axis, written as Python's `repr()` writes them, so they
read back as exactly the same values. `sha256_out` is the SHA-256 of the output file, for checking a
rerun against a published result. A field the run did not reach is empty.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Done. |
| 1 | Cannot write the output or the report. |
| 2 | Usage: a bad option, or INPUT and OUTPUT the same file. |
| 3 | Cannot read the input, or it is truncated. |
| 4 | Not mesh2ply's layout, a face index out of range, or already coloured without `--replace`. |
| 5 | A NaN or infinite position or normal. |

## Reference implementation

The colourise specification also describes `colourise.py`, a reference implementation in Python. On
every input in mesh2ply's layout, ply_colourise with the default axis writes the same bytes, the same
summary line and the same report values. The differences:

- The report has an `axis` column, and `min`/`max` in place of `ymin`/`ymax`; `spec_version` is 0.4.
- A NaN or infinite normal, and a face index out of range, are refused here (exit 5 and 4); the
  Python copies them. mesh2ply writes neither, and mesh2ply's reader, used here, does not pass them
  through unchanged.
- A failure to write the output or the report is exit 1 with a message; the Python stops with an
  error.

## Tests

```
bash tests/ply_colourise/run_tests.sh build/bin/Release/ply_colourise.exe ply_colourise_test
```

runs from the given scratch folder (created if needed) with mesh2ply's unit cube,
`tests/mesh2ply/expected/off_cube.ply`. It checks along each axis the header, every vertex's colour
and position and the faces, against values written out in the script; the hand-checked case of the
specification, which tests the rounding of `t * 255 + 0.5` (y = 0.5 gives row 128, 0.25 row 64, 0.75
row 191); re-colouring with `--replace`; the report; and the refusals (a coloured input without
`--replace`, ASCII, truncated, the same input and output, a bad axis), which must leave no file.
