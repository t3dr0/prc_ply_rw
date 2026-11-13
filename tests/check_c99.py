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
"""Check that every first-party C source in prc_ply_rw parses as strict C99.

Both tools pin C_STANDARD 99, but MSVC's C mode accepts constructs that C99
removed, so a clean Windows build proves nothing. The usual case is a static
function called before its definition: an implicit declaration, legal in C89,
which GCC and Clang reject and MSVC passes without a warning.

The check parses each source with a Clang or GCC front end at -std=c99. It
does not link and does not replace CI.

Before checking anything it parses a small file that is known to be wrong. If
the compiler does not reject that file, the check stops with exit 2 instead of
reporting a pass it cannot vouch for.

Compiler search order: clang, gcc, cc on PATH; then clang-tidy on PATH; then,
on Windows, the clang or clang-tidy that Visual Studio installs with its
"C++ Clang tools" component (found with vswhere, or under the standard
install folders).

usage: python tests/check_c99.py [--nanoprc DIR] [--rply DIR] [-v]
       exit 0 = clean, 1 = violations found, 2 = no usable compiler or setup error
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# First-party sources. thirdparty/ is excluded; the bundled stb_image_write.h
# is still parsed, because ply_export.c includes its implementation.
SOURCE_GLOBS = ['ply_import/src/*.c', 'ply_export/src/*.c', 'mesh2ply/src/*.c', 'tests/*.c']

DEFINES = ['-D_CRT_SECURE_NO_WARNINGS']

WARN_AS_ERROR = ['-Werror=implicit-function-declaration', '-Werror=implicit-int']

# A diagnostic line starts with <file>:<line>:<col>: error: ...  Matching the
# position prefix keeps echoed source lines that happen to contain "error:"
# from counting.
DIAG_RE = re.compile(r'^.+?:\d+:\d+:\s+(fatal error|error):\s')
# clang-tidy also runs analyzer checks; their findings are not conformance.
IGNORE_RE = re.compile(r'\[clang-analyzer-')

KNOWN_BAD = '''static int caller(void) { return callee(); }
static int callee(void) { return 1; }
int main(void) { return caller(); }
'''

VSWHERE = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
VS_ROOT_GLOBS = ['C:/Program Files/Microsoft Visual Studio/*/*',
                 'C:/Program Files (x86)/Microsoft Visual Studio/*/*']


def visual_studio_roots():
    roots = []
    if os.path.exists(VSWHERE):
        try:
            out = subprocess.run([VSWHERE, '-all', '-products', '*',
                                  '-property', 'installationPath'],
                                 capture_output=True, text=True).stdout
            roots += [r.strip() for r in out.splitlines() if r.strip()]
        except OSError:
            pass
    for g in VS_ROOT_GLOBS:
        roots += sorted(glob.glob(g), reverse=True)
    return roots


def find_compiler():
    """Return (kind, path): kind is 'cc' for clang/gcc, or 'tidy'."""
    for name in ('clang', 'gcc', 'cc'):
        p = shutil.which(name)
        if p:
            return 'cc', p
    p = shutil.which('clang-tidy')
    if p:
        return 'tidy', p
    if os.name == 'nt':
        for root in visual_studio_roots():
            llvm = os.path.join(root, 'VC', 'Tools', 'Llvm')
            for sub, exe, kind in (('x64/bin', 'clang.exe', 'cc'),
                                   ('bin', 'clang.exe', 'cc'),
                                   ('x64/bin', 'clang-tidy.exe', 'tidy'),
                                   ('bin', 'clang-tidy.exe', 'tidy')):
                cand = os.path.join(llvm, sub, exe)
                if os.path.exists(cand):
                    return kind, os.path.normpath(cand)
    return None, None


def find_nanoprc(arg):
    cands = []
    if arg:
        cands.append(arg)
    if os.environ.get('NANOPRC_SOURCE_DIR'):
        cands.append(os.environ['NANOPRC_SOURCE_DIR'])
    # When prc_ply_rw is the nanoPRC submodule at demos/prc_ply_rw.
    cands.append(os.path.join(REPO, '..', '..'))
    for c in cands:
        if os.path.exists(os.path.join(c, 'include', 'prc_api.h')):
            return os.path.abspath(c)
    return None


def parse(kind, tool, src, incs):
    flags = ['-std=c99'] + WARN_AS_ERROR + DEFINES + ['-I' + d for d in incs]
    if kind == 'tidy':
        # Enable clang-diagnostic-* alone. Adding '-*' in front, to turn the
        # other checks off, leaves clang-tidy with "no checks enabled": it then
        # stops without parsing the file.
        cmd = [tool, '--checks=clang-diagnostic-*', '--quiet', src, '--'] + flags
    else:
        cmd = [tool, '-fsyntax-only'] + flags + [src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    text = (r.stdout or '') + (r.stderr or '')
    return [ln.strip() for ln in text.splitlines()
            if DIAG_RE.match(ln) and not IGNORE_RE.search(ln)]


def compiler_detects_known_bad(kind, tool):
    d = tempfile.mkdtemp(prefix='c99check_')
    path = os.path.join(d, 'known_bad.c')
    try:
        with open(path, 'w') as f:
            f.write(KNOWN_BAD)
        return bool(parse(kind, tool, path, []))
    finally:
        shutil.rmtree(d, ignore_errors=True)


def sources():
    out = []
    for g in SOURCE_GLOBS:
        out += glob.glob(os.path.join(REPO, g))
    return sorted(out)


def main():
    ap = argparse.ArgumentParser(description='strict C99 check for prc_ply_rw')
    ap.add_argument('--nanoprc', help='nanoPRC checkout (default: $NANOPRC_SOURCE_DIR, '
                                      'or the enclosing nanoPRC when this is its submodule)')
    ap.add_argument('--rply', default=os.path.join(REPO, 'thirdparty', 'rply'),
                    help='RPly sources (default: thirdparty/rply)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()

    nanoprc = find_nanoprc(args.nanoprc)
    if not nanoprc:
        print('error: nanoPRC not found; pass --nanoprc or set NANOPRC_SOURCE_DIR',
              file=sys.stderr)
        return 2
    if not os.path.exists(os.path.join(args.rply, 'rply.h')):
        print('error: rply.h not found in %s' % args.rply, file=sys.stderr)
        return 2

    kind, tool = find_compiler()
    if not tool:
        print('error: no clang, gcc or clang-tidy found, so C99 conformance cannot be '
              'checked. On Windows, install the Visual Studio "C++ Clang tools" component.',
              file=sys.stderr)
        return 2
    if not compiler_detects_known_bad(kind, tool):
        print('error: %s did not reject a known C99 violation; its result would mean '
              'nothing' % tool, file=sys.stderr)
        return 2

    incs = [os.path.join(nanoprc, 'include'), args.rply]
    srcs = sources()
    print('C99 check: %d files via %s (nanoPRC at %s)' % (len(srcs), tool, nanoprc))

    failures = 0
    for s in srcs:
        own = os.path.dirname(s)
        bad = parse(kind, tool, s, [own] + incs)
        rel = os.path.relpath(s, REPO).replace('\\', '/')
        if bad:
            failures += 1
            print('\nFAIL %s' % rel)
            for ln in bad[:10]:
                print('   ' + ln)
        elif args.verbose:
            print('  ok   %s' % rel)

    print()
    if failures:
        print('%d of %d file(s) are not strict C99. Fix them before opening a pull request.'
              % (failures, len(srcs)))
        return 1
    print('all %d files parse clean at -std=c99' % len(srcs))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
