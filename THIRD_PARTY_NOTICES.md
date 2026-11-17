# Third-party notices

prc_ply_rw includes or depends on the following third-party components.

## RPly

- Purpose: PLY reading and writing (ASCII, binary little-endian, binary big-endian), used by every
  tool: `ply_import`, `ply_export`, `mesh2ply`, and `ply_colourise` through mesh2ply's reader.
- Location: `thirdparty/rply` — `rply.c`, `rply.h`, `rplyfile.h` and `LICENSE`, vendored unchanged.
- Version: 1.1.4.
- Source: http://w3.impa.br/~diego/software/rply/ — the author publishes release tarballs only, with
  no git repository, so it is vendored rather than taken as a git submodule. The files were taken
  from `rply-1.1.4.tar.gz` (SHA-256
  `daf0b060fe701adf72aab0d525323d2e2e1bde9aa6aa9713ff1a5ef1e768d703`), downloaded 16 October 2026.
- Licence: MIT

```
RPly 1.1.4 license
Copyright (C) 2003-2015 Diego Nehab.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

The vendored `LICENSE` file is byte-for-byte the author's, which encodes the copyright sign in
Latin-1; it is shown above as `(C)`.

Known in the unchanged source: `ply_open_from_file` and `ply_create_to_file` pass an uninitialised
handle to the error callback if `ply_type_check()` fails, which happens only on a platform whose C
types do not have the sizes PLY needs. Neither tool's callback uses that argument.

Also known, and unfixed upstream: CVE-2026-115722. `ply_read_header` accepts a list whose length or
value type is itself `list`, and `ply_read` then calls through its handler table one past the end.
`ply_import` refuses such a header after `ply_read_header` and before `ply_read`
(`check_list_types`); RPly itself is left unchanged.

## stb_image_write

- Purpose: PNG writing for texture export, used by `ply_export`.
- Location: `ply_export/src/stb_image_write.h` (v1.16, vendored).
- Author: Sean Barrett.
- Source: https://github.com/nothings/stb
- Licence: public domain (Unlicense) or MIT, at the user's choice. The full text is at the end of the
  header file.
- Known, and unfixed upstream: CVE-2026-102805, unchecked size arithmetic in `stbi_write_png_to_mem`
  and the JPEG and TGA writers. `ply_export` writes a texture only when its width, height and
  channels are sensible and the image is at most 2^28 bytes.

## Smooth Cool Warm colour map

- Purpose: the colour table `ply_colourise` looks vertex colours up in.
- Location: `ply_colourise/src/plyc_field.c`, the array `plyc_smooth_cool_warm`: 256 rows of red,
  green and blue bytes, generated from the source file below and unchanged.
- Author: Kenneth Moreland.
- Source: `smooth-cool-warm-table-byte-0256.csv` from https://www.kennethmoreland.com/color-advice/,
  accessed 16 November 2026 (SHA-256
  `68a67431a6b1eb42930e4f1f33ff851e006b956655680adc178cc80a0c83ead4`).
- Reference: K. Moreland, "Diverging Color Maps for Scientific Visualization", in *Advances in
  Visual Computing*, Proc. 5th International Symposium on Visual Computing (ISVC 2009), Lecture
  Notes in Computer Science, Springer, 2009, pp. 92-103, doi:10.1007/978-3-642-10520-3_9.
- Licence: the source page states that this colour map was first featured in ParaView and is
  covered by ParaView's BSD licence (BSD 3-Clause, https://www.paraview.org/paraview-license/,
  checked 17 November 2026). The page releases the colour maps original to it as public domain
  (CC0) and names other licences for borrowed ones, as here. The ParaView licence:

```
Copyright (c) 2000 Kitware Inc.
Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions
   and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of
   conditions and the following disclaimer in the documentation and/or other materials provided
   with the distribution.
3. Neither the name of Kitware nor the names of any contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

The page shows the list items without numbers and the quotation marks around "AS IS" as curly
quotes; they are shown here as numbered items and plain quotes.

## SHA-256

`ply_colourise` computes the SHA-256 of its output for the report (`ply_colourise/src/plyc_sha256.c`).
It is this repository's own code, written from the published standard: NIST FIPS 180-4, *Secure
Hash Standard (SHS)*, August 2015, doi:10.6028/NIST.FIPS.180-4. No third-party code is used.

## nanoPRC

- Purpose: PRC reading and writing; `ply_import` and `ply_export` link it. `mesh2ply` and
  `ply_colourise` do not need it.
- Not included in this repository. Supplied as a separate checkout (see README).
- Source: https://github.com/CascadiaVoxel/nanoPRC
- Licence: GNU AGPL v3, with a commercial licence available from its owner.
