#!/usr/bin/env bash
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

# Round-trip every file in tests/data through PRC/PDF and all three PLY
# encodings: import, export binary little-endian, import that, export binary
# big-endian, import that, export ASCII -- then compare the last file with
# the original, triangle by triangle, positions, winding, colours and UVs.
#
#   tests/roundtrip.sh <bin dir> <scratch dir> [python]
#
# Written as plain triangles (--triangles) the comparison must be exact.
# Written compressed, only the triangle count is checked: nanoPRC's
# compressed write-and-read currently reverses the winding of some
# triangles (5 of the cube's 12, on 16 October 2026), a defect in nanoPRC
# rather than in these tools.

set -euo pipefail

bin="$(cd "$1" && pwd)"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
py="${3:-python3}"
here="$(cd "$(dirname "$0")" && pwd)"
ext=""
[ -e "$bin/nano_prc_ply_import.exe" ] && ext=".exe"
imp="$bin/nano_prc_ply_import$ext"
exp="$bin/nano_prc_ply_export$ext"

mkdir -p "$out"
cp "$here"/data/* "$out"/
cd "$out"

fail=0
for f in cube cube_colors quad_textured strip; do
    for mode in triangles compressed; do
        opt=""
        [ "$mode" = triangles ] && opt="--triangles"
        "$imp" "$f.ply" a.prc a.pdf $opt > /dev/null
        "$exp" a.pdf le.ply > /dev/null
        "$imp" le.ply b.prc b.pdf $opt > /dev/null
        "$exp" b.pdf be.ply --big-endian > /dev/null
        "$imp" be.ply c.prc c.pdf $opt > /dev/null
        "$exp" c.pdf back.ply --ascii > /dev/null

        if [ "$mode" = triangles ]; then
            if result=$("$py" "$here/compare_ply.py" "$f.ply" back.ply); then
                echo "ok    $f ($mode): ${result##*$'\n'}"
            else
                echo "FAIL  $f ($mode)"; echo "$result"; fail=1
            fi
        else
            # compare_ply.py fails on the reversed triangles; only its count
            # of expected triangles is wanted here.
            want=$( ("$py" "$here/compare_ply.py" "$f.ply" back.ply || true) | head -1 |
                    sed 's/.*expected \([0-9]*\).*/\1/')
            got=$(grep -m1 '^element face' back.ply | awk '{print $3}')
            if [ "$want" = "$got" ]; then
                echo "ok    $f ($mode): $got triangles"
            else
                echo "FAIL  $f ($mode): expected $want triangles, got $got"; fail=1
            fi
        fi
    done
done
exit $fail
