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
"""Run mesh2ply's tests (specification section 10).

  python tests/mesh2ply/run_tests.py MESH2PLY [--regenerate] [--fuzz N] [--keep DIR]

--regenerate rewrites tests/mesh2ply/fixtures and expected from cases.py
(inputs, and golden outputs from the hand-written expected meshes). Without
it the committed files are used, so a change to cases.py shows as a diff.

For each case: the exit code, the output's bytes against the golden, and
the report fields the case names. Then: the cube in ASCII, binary LE and
binary BE gives one output; every output fed back in losslessly comes out
byte-identical; and --fuzz N mutated inputs (truncated, bytes flipped, counts
made huge) each exit with a code from 0 to 5, never a crash.
"""
import argparse, csv, os, random, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cases as C  # noqa: E402

FIX = os.path.join(HERE, "fixtures")
EXP = os.path.join(HERE, "expected")


def regenerate():
    for d in (FIX, EXP):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    for c in C.cases():
        with open(os.path.join(FIX, c["name"] + "." + c["ext"]), "wb") as f:
            f.write(c["data"])
        if c["mesh"] is not None:
            m = c["mesh"]
            with open(os.path.join(EXP, c["name"] + ".ply"), "wb") as f:
                f.write(C.golden_bytes(m["verts"], m["faces"], m.get("normals", False),
                                       m.get("colour", False), m.get("alpha", False)))


def run(exe, inp, out, opts, report=None):
    args = [exe] + opts + ["--quiet"] + (["--report", report] if report else []) + [inp, out]
    p = subprocess.run(args, capture_output=True, timeout=120)
    err = p.stderr.decode("utf-8", "replace")
    # Every run here has valid options, so exit 1 (usage) is never right. A
    # sanitizer's report also exits 1 and would otherwise pass for a code
    # from 0 to 5: make it -1.
    if "Sanitizer" in err or p.returncode == 1:
        return -1, err
    return p.returncode, err


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--regenerate", action="store_true")
    ap.add_argument("--fuzz", type=int, default=300)
    ap.add_argument("--keep")
    a = ap.parse_args()
    # Windows does not start a relative path written with forward slashes
    a.exe = os.path.abspath(a.exe)
    if a.regenerate:
        regenerate()
    work = a.keep or tempfile.mkdtemp(prefix="mesh2ply_")
    os.makedirs(work, exist_ok=True)
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)
            print("FAIL", what)

    allc = C.cases()
    for c in allc:
        inp = os.path.join(FIX, c["name"] + "." + c["ext"])
        out = os.path.join(work, c["name"] + ".out.ply")
        rep = os.path.join(work, c["name"] + ".csv")
        for p in (out, rep):
            if os.path.exists(p):
                os.remove(p)
        code, err = run(a.exe, inp, out, c["opts"], rep)
        check(code == c["code"], "%s: exit %d, expected %d (%s)" % (c["name"], code, c["code"], err.strip()))
        if c["code"] == 0:
            gold = open(os.path.join(EXP, c["name"] + ".ply"), "rb").read()
            got = open(out, "rb").read() if os.path.exists(out) else b""
            check(got == gold, "%s: output differs from the golden (%d vs %d bytes)" % (c["name"], len(got), len(gold)))
        else:
            check(not os.path.exists(out), "%s: an output was written on failure" % c["name"])
            check(not any(f.startswith(c["name"] + ".out.ply.") for f in os.listdir(work)),
                  "%s: a temporary file was left" % c["name"])
        rows = list(csv.DictReader(open(rep, newline="")))
        check(len(rows) == 1, "%s: %d report rows" % (c["name"], len(rows)))
        if rows:
            r = rows[0]
            check(r["exit_code"] == str(c["code"]), "%s: report exit_code %s" % (c["name"], r["exit_code"]))
            for k, v in c["report"].items():
                check(r.get(k) == v, "%s: report %s = %r, expected %r" % (c["name"], k, r.get(k), v))

    # The three encodings of the cube give one output
    outs = [open(os.path.join(work, "ply_cube_%s.out.ply" % f), "rb").read()
            for f in ("ascii", "binary_little_endian", "binary_big_endian")]
    check(outs[0] == outs[1] == outs[2], "cube: ASCII, LE and BE outputs differ")

    # Round trip: every output, fed back in losslessly, comes out the same
    for c in allc:
        out = os.path.join(work, c["name"] + ".out.ply")
        if c["code"] != 0:
            continue
        again = os.path.join(work, c["name"] + ".again.ply")
        code, err = run(a.exe, out, again, [])
        check(code == 0 and open(again, "rb").read() == open(out, "rb").read(),
              "%s: round trip not byte-identical (exit %d %s)" % (c["name"], code, err.strip()))

    # Mutations: no crash, an exit code from 0 to 5
    rnd = random.Random(1)
    inputs = [os.path.join(FIX, c["name"] + "." + c["ext"]) for c in allc]
    for n in range(a.fuzz):
        src = open(rnd.choice(inputs), "rb").read()
        b = bytearray(src)
        kind = n % 3
        if kind == 0 and len(b) > 1:
            b = b[: rnd.randrange(1, len(b))]
        elif kind == 1:
            for _ in range(rnd.randint(1, 8)):
                b[rnd.randrange(len(b))] = rnd.randrange(256)
        else:
            s = bytes(b).replace(b"element vertex ", b"element vertex 99999999").replace(b"\n8 6 0", b"\n99999999 6 0")
            s = s.replace(b"element face ", b"element face 4000000000")
            b = bytearray(s)
        mf = os.path.join(work, "fuzz.in")
        open(mf, "wb").write(bytes(b))
        code, err = run(a.exe, mf, os.path.join(work, "fuzz.out.ply"), rnd.choice([[], ["--clean"], ["--clean", "--drop-invalid"]]))
        check(0 <= code <= 5, "fuzz %d (kind %d): exit %d %s" % (n, kind, code, err.strip()[:200]))

    print("%d cases, %d mutations: %s" % (len(allc), a.fuzz, "all passed" if not fails else "%d FAILED" % len(fails)))
    if not a.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
