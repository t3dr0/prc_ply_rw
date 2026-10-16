# Third-party notices

prc_ply_rw includes or depends on the following third-party components.

## RPly

- Purpose: PLY reading and writing (ASCII, binary little-endian, binary big-endian), used by both
  tools.
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

## stb_image_write

- Purpose: PNG writing for texture export, used by `ply_export`.
- Location: `ply_export/src/stb_image_write.h` (v1.16, vendored).
- Source: https://github.com/nothings/stb
- Licence: public domain (Unlicense) or MIT, at the user's choice. The full text is at the end of the
  header file.

## nanoPRC

- Purpose: PRC reading and writing; both tools link it.
- Not included in this repository. Supplied as a separate checkout (see README).
- Source: https://github.com/mvrhel/nanoPRC
- Licence: GNU AGPL v3, with a commercial licence available from its owner.
