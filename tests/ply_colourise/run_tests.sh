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

# ply_colourise's tests.
#
#   tests/ply_colourise/run_tests.sh <ply_colourise executable> [scratch dir]
#
# The input is mesh2ply's unit cube, tests/mesh2ply/expected/off_cube.ply:
# 8 vertices with coordinates 0 or 1, 6 quads, no normals. Every expected
# value is written out here, not taken from the program: the header with the
# colour properties added, each vertex's colour from its coordinate on the
# chosen axis (0 is table row 0, 59 76 192; 1 is row 255, 180 4 38), the
# positions and faces copied unchanged. Then the hand-checked case of the
# specification (section 10.3), which tests the rounding of t * 255 + 0.5,
# re-colouring with --replace, the report, and the inputs that must be
# refused, which must leave no output file behind.
#
# Needs bash, od, cmp, head, tail, and sha256sum or shasum.

set -u

exe="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
here="$(cd "$(dirname "$0")" && pwd)"
work="${2:-$(mktemp -d)}"
mkdir -p "$work"
cd "$work" || exit 2
rm -f ./*.ply ./*.csv ./*.ply_colourise-tmp

# Copied in, so that every path the program is given is a plain relative one
cp "$here/../mesh2ply/expected/off_cube.ply" cube.ply
cp "$here/../data/cube.ply" ascii_cube.ply
cube=cube.ply
ascii_cube=ascii_cube.ply
fail=0

ok() { echo "ok    $1"; }
bad() { echo "FAIL  $1"; fail=1; }

# Bytes [offset, offset + length) of a file as decimal numbers, one space apart
bytes() { od -An -v -tu1 -j "$2" -N "$3" "$1" | tr -s ' \n' '  ' | sed 's/^ //; s/ $//'; }

sha256() {
    if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -c1-64
    else shasum -a 256 "$1" | cut -c1-64; fi
}

# Runs the program; $code is its exit code
run() { "$exe" --quiet "$@" 2> stderr.txt; code=$?; }

expect_code() {
    if [ "$code" = "$1" ]; then ok "$2: exit $1"
    else bad "$2: exit $code, expected $1 ($(cat stderr.txt))"; fi
}

# The fixture: header 169 bytes, 8 vertices of 12 bytes, 6 faces of 17
in_header=169
in_faces=$((in_header + 8 * 12))
[ "$(wc -c < "$cube" | tr -d ' ')" = 367 ] || { bad "fixture $cube is not the 367-byte cube"; exit 1; }

printf 'ply\nformat binary_little_endian 1.0\nelement vertex 8\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nelement face 6\nproperty list uchar int vertex_indices\nend_header\n' > expected_header.txt
out_header=$(wc -c < expected_header.txt | tr -d ' ')
out_faces=$((out_header + 8 * 15))

low="59 76 192"
high="180 4 38"
# The cube's coordinates, vertex by vertex
xs="0 1 1 0 0 1 1 0"
ys="0 0 1 1 0 0 1 1"
zs="0 0 0 0 1 1 1 1"

# Checks a colourised cube against the coordinates on one axis
check_cube() {
    local file="$1" what="$2" i want got was="$fail"
    local coords=($3)
    fail=0
    if [ ! -f "$file" ] || [ "$(wc -c < "$file" | tr -d ' ')" != $((out_faces + 6 * 17)) ]; then
        bad "$what: not written, or not $((out_faces + 6 * 17)) bytes"; fail=1; return
    fi
    head -c "$out_header" "$file" > got_header.txt
    cmp -s got_header.txt expected_header.txt || bad "$what: header differs"
    for i in 0 1 2 3 4 5 6 7; do
        got=$(bytes "$file" $((out_header + i * 15)) 12)
        [ -n "$got" ] && [ "$got" = "$(bytes "$cube" $((in_header + i * 12)) 12)" ] ||
            bad "$what: vertex $i position changed"
        if [ "${coords[$i]}" = 0 ]; then want="$low"; else want="$high"; fi
        got=$(bytes "$file" $((out_header + i * 15 + 12)) 3)
        [ "$got" = "$want" ] || bad "$what: vertex $i colour $got, expected $want"
    done
    got=$(bytes "$file" "$out_faces" 102)
    [ -n "$got" ] && [ "$got" = "$(bytes "$cube" "$in_faces" 102)" ] || bad "$what: faces changed"
    [ "$fail" = 0 ] && ok "$what: header, positions, colours and faces"
    [ "$was" = 0 ] || fail=1
}

# --- each axis, the default, and the two ways of giving an option's value
run "$cube" y.ply;              expect_code 0 "default axis"; check_cube y.ply "default axis (y)" "$ys"
run --axis x "$cube" x.ply;     expect_code 0 "--axis x";     check_cube x.ply "--axis x" "$xs"
run --axis z "$cube" z.ply;     expect_code 0 "--axis z";     check_cube z.ply "--axis z" "$zs"
run --axis y "$cube" y2.ply;    expect_code 0 "--axis y"
cmp -s y.ply y2.ply && ok "--axis y equals the default" || bad "--axis y differs from the default"
run --axis=z "$cube" z2.ply;    expect_code 0 "--axis=z"
cmp -s z.ply z2.ply && ok "--axis=z equals --axis z" || bad "--axis=z differs from --axis z"

# --- the hand-checked case (specification 10.3): y = 0 0 0 1 0.5 0.25 0.75
# 0.5 gives t * 255 = 127.5, row 128; 0.25 gives 63.75, row 64; 0.75 gives
# 191.25, row 191
{
    printf 'ply\nformat binary_little_endian 1.0\nelement vertex 7\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n'
    z='\x00\x00\x00\x00'; one='\x00\x00\x80\x3f'
    half='\x00\x00\x00\x3f'; quarter='\x00\x00\x80\x3e'; three_quarters='\x00\x00\x40\x3f'
    printf "$z$z$z$one$z$z$z$z$one$z$one$z"
    printf "$z$half$z$z$quarter$z$z$three_quarters$z"
    printf '\x03\x00\x00\x00\x00\x01\x00\x00\x00\x03\x00\x00\x00'
} > hand.ply
run hand.ply hand_out.ply; expect_code 0 "hand-checked case"
hh=$(( $(wc -c < hand.ply | tr -d ' ') - 7 * 12 - 13 + 60 ))
want=("$low" "$low" "$low" "$high" "221 220 220" "141 176 254" "244 154 123")
hand_ok=1
for i in 0 1 2 3 4 5 6; do
    got=$(bytes hand_out.ply $((hh + i * 15 + 12)) 3)
    [ "$got" = "${want[$i]}" ] || { bad "hand-checked case: vertex $i colour $got, expected ${want[$i]}"; hand_ok=0; }
done
[ $hand_ok = 1 ] && ok "hand-checked case: rows 0, 255, 128, 64 and 191"

# --- a coloured input: refused without --replace, re-coloured with it
run y.ply again.ply;           expect_code 4 "coloured input without --replace"
[ ! -e again.ply ] && ok "no output after a refusal" || bad "an output was written after a refusal"
run --replace y.ply again.ply; expect_code 0 "coloured input with --replace"
cmp -s again.ply y.ply && ok "re-colouring gives the same file" || bad "re-colouring changed the file"
run --replace --axis x y.ply again_x.ply; expect_code 0 "--replace --axis x"
cmp -s again_x.ply x.ply && ok "re-colouring along x gives the x file" || bad "re-colouring along x differs"

# --- the report
rm -f report.csv
run --report report.csv "$cube" r.ply; expect_code 0 "--report"
run --report report.csv y.ply r2.ply;  expect_code 4 "--report on a refused input"
header=$(head -n 1 report.csv | tr -d '\r')
[ "$header" = "input,output,spec_version,axis,replaced,vertices,faces,min,max,distinct_colours,bytes_in,bytes_out,sha256_out,exit_code" ] &&
    ok "report header" || bad "report header: $header"
row=$(sed -n 2p report.csv | tr -d '\r')
[ "$row" = "$cube,r.ply,0.4,y,0,8,6,0.0,1.0,2,367,$((out_faces + 102)),$(sha256 r.ply),0" ] &&
    ok "report row" || bad "report row: $row"
row=$(sed -n 3p report.csv | tr -d '\r')
[ "$row" = "y.ply,r2.ply,0.4,y,,,,,,,$((out_faces + 102)),,,4" ] && ok "report row of a refusal" || bad "report row of a refusal: $row"

# --- inputs that must be refused, leaving no file behind
head -c 300 "$cube" > truncated.ply
cp "$cube" same.ply
run "$ascii_cube" o1.ply;     expect_code 4 "ASCII PLY"
run truncated.ply o2.ply;     expect_code 3 "truncated file"
run same.ply same.ply;        expect_code 2 "same input and output"
cmp -s same.ply "$cube" && ok "the input is untouched" || bad "the input was changed"
run --axis w "$cube" o3.ply;  expect_code 2 "--axis w"
run "$cube";                  expect_code 2 "no output named"
for f in o1.ply o2.ply o3.ply; do [ ! -e "$f" ] || bad "$f was written"; done
ls ./*.ply_colourise-tmp > /dev/null 2>&1 && bad "a temporary file was left" || ok "no temporary file left"

if [ $fail = 0 ]; then echo "ply_colourise: all tests passed"; else echo "ply_colourise: TESTS FAILED"; fi
exit $fail
