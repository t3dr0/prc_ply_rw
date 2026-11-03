# prc_ply_rw

Convert PLY (Stanford polygon format) to PRC / 3D PDF and back, using
[nanoPRC](https://github.com/CascadiaVoxel/nanoPRC).

PRC (ISO 14739) is the 3D format embedded in 3D PDF files. This repository holds two command-line
tools built on nanoPRC's public C API. Both read and write all three PLY encodings — ASCII, binary
little-endian and binary big-endian — through [RPly](http://w3.impa.br/~diego/software/rply/).

| Tool | Direction | What it does |
|---|---|---|
| `nano_prc_ply_import` | PLY → PRC and PDF | Reads a PLY mesh with its normals, colours and texture coordinates, and writes a `.prc` file and a minimal 3D PDF |
| `nano_prc_ply_export` | PRC or PDF → PLY | Extracts the tessellated surfaces as one PLY mesh, with textures as PNG |

## ply_import

```
nano_prc_ply_import <input.ply> [output.prc] [output.pdf]
                    [--triangles] [--no-normals] [--no-colors] [--no-texture]
                    [--quant-tolerance F]
```

What it reads:

| PLY | Becomes |
|---|---|
| `vertex`: `x y z` | positions |
| `vertex`: `nx ny nz` | normals; without them the reader computes normals |
| `vertex`: `red green blue [alpha]`, bytes or floats 0..1 | vertex colours |
| `face`: `red green blue [alpha]` | face colours (used when vertices have none) |
| `vertex`: `s t`, `u v`, `texture_u texture_v` or `texture_s texture_t` | texture coordinates |
| `face`: `texcoord` list (`2n` values) | per-corner texture coordinates, as MeshLab writes them |
| `face`: `texnumber` | which texture a face uses |
| `face`: `vertex_indices` (or `vertex_index`) | polygons, triangulated by ear clipping |
| `tristrips`: `vertex_indices`, `-1` restarting a strip | triangles |
| `comment TextureFile <image>` | the texture image, PNG or JPEG, relative to the PLY file |

- A PLY file has no parts, so the model is one part named after the file. It holds one tessellation,
  or one per texture when a file names several.
- Geometry is written as PRC compressed tessellation, textured, coloured or neither. `--triangles`
  writes plain triangles instead. Coloured meshes need a nanoPRC that writes compressed colours.
- `--quant-tolerance` sets the compressed encoding's tolerance as a fraction of the bounding-box
  diagonal (default `1e-6`).
- A coloured mesh is given a white material, so its colours show as they are.
- Build against a nanoPRC that includes its pull requests #153, #157 and #168. Older versions can
  write files that Adobe Acrobat shows with an empty model tree, depending on where a section of the
  file happens to end; that is why earlier versions of this tool warned about PNG textures of 256
  pixels or more and suggested trying another tolerance.
- Faces of fewer than three corners, faces that index a missing vertex and triangles with a repeated
  vertex are skipped and counted.

## ply_export

```
nano_prc_ply_export <input.pdf|input.prc> <output.ply>
                    [--ascii | --big-endian] [--no-normals] [--no-colors] [--no-texture]
```

Writes binary little-endian PLY by default. Every surface tessellation goes into one indexed mesh,
with vertices shared as the reader gives them.

- Each vertex carries `x y z`, `nx ny nz`, `red green blue` (and `alpha` when any vertex is not
  opaque), and `s t` when any face is textured. A vertex the file gives no normal gets the
  area-weighted average of its triangles' normals.
- Colours follow nanoPRC's viewer demo: a vertex's own material colour when the tessellation styles
  each vertex, otherwise the face's material colour times the vertex colour. A mesh coloured only by
  nanoPRC's default material, the terracotta it writes for items that have none, is written without
  colours, so a PLY without colours comes back without them.
- Textures are written beside the PLY as `<name>_tex_<n>.png`, each distinct image once, named by
  `comment TextureFile` lines. With more than one texture each face also carries `texnumber`.
- Positions are written in each tessellation's own coordinates, as `obj_export` writes them: part
  placement transforms are not applied. Wire and markup tessellations are not exported.

## Building

nanoPRC is required. Build either way:

**Inside a nanoPRC checkout**, where this repository is a submodule, nanoPRC's own CMake build
includes it.

**On its own**, pointing at a nanoPRC checkout:

```
git clone https://github.com/t3dr0/prc_ply_rw
cmake -S prc_ply_rw -B build -DNANOPRC_SOURCE_DIR=/path/to/nanoPRC
cmake --build build --config Release
```

RPly is vendored in `thirdparty/rply` (it has no git repository to take as a submodule), so no
submodule step is needed.

Both tools are C99. Before opening a pull request, run the C99 check and the round trip:

```
python tests/check_c99.py --nanoprc /path/to/nanoPRC
bash tests/roundtrip.sh build/bin/Release roundtrip_out
```

`tests/roundtrip.sh` takes each file in `tests/data` through PRC and PDF and all three PLY encodings,
and compares the result with the original triangle by triangle: positions, winding, colours and
texture coordinates. `tests/make_test_data.py` regenerates `tests/data`.

## Licence

Copyright (C) 2026 Ian Curington, Thames 3D Research.

prc_ply_rw is licensed under the GNU Affero General Public License v3.0; see [LICENSE](LICENSE).
Third-party components are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

### Commercial licensing

A commercial licence is available on request for uses where the AGPLv3 is not suitable. Contact
info@thames3.org.

prc_ply_rw links nanoPRC, which is licensed separately by its owner. A closed-source product built
with prc_ply_rw also needs a commercial nanoPRC licence; see nanoPRC's README.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Contributions require agreement to [CLA.md](CLA.md).
