/* Copyright (C) 2026 Ian Curington, Thames 3D Research

    prc_ply_rw is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    prc_ply_rw is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
    General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with prc_ply_rw. If not, see <https://www.gnu.org/licenses/>.
*/

/* ply_colourise: the field and the colour map (specification sections 5
   and 6).

   The colour map is Kenneth Moreland's "Smooth Cool Warm", as its 256-entry
   byte table smooth-cool-warm-table-byte-0256.csv from
   https://www.kennethmoreland.com/color-advice/ (accessed 16 November
   2026; SHA-256 of that file
   68a67431a6b1eb42930e4f1f33ff851e006b956655680adc178cc80a0c83ead4). Row i
   is the colour at scalar i / 255; the file's scalar column is not needed.
   The rows below were generated from that file, not typed, and are copied
   unchanged: no entry is interpolated or altered.

   The map follows K. Moreland, "Diverging Color Maps for Scientific
   Visualization", in Advances in Visual Computing, Proc. 5th International
   Symposium on Visual Computing (ISVC 2009), Lecture Notes in Computer
   Science, Springer, 2009, pp. 92-103, doi:10.1007/978-3-642-10520-3_9.

   The source page states that Smooth Cool Warm was first featured in
   ParaView and is covered by ParaView's BSD licence
   (https://www.paraview.org/paraview-license/, checked 17 November 2026):

     Copyright (c) 2000 Kitware Inc.
     Redistribution and use in source and binary forms, with or without
     modification, are permitted provided that the following conditions are
     met:
     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
     3. Neither the name of Kitware nor the names of any contributors may be
        used to endorse or promote products derived from this software
        without specific prior written permission.
     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
     IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
     PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR
     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
     EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
     PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
     PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
     LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
     NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
     SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "plyc.h"

#include <math.h>

const unsigned char plyc_smooth_cool_warm[256][3] = {
    {  59,  76, 192 }, {  60,  78, 194 }, {  61,  80, 195 }, {  62,  81, 197 },  /*   0 */
    {  63,  83, 199 }, {  64,  85, 200 }, {  66,  87, 202 }, {  67,  88, 203 },  /*   4 */
    {  68,  90, 204 }, {  69,  92, 206 }, {  70,  94, 207 }, {  72,  95, 209 },  /*   8 */
    {  73,  97, 210 }, {  74,  99, 212 }, {  75, 100, 213 }, {  76, 102, 214 },  /*  12 */
    {  78, 104, 216 }, {  79, 106, 217 }, {  80, 107, 218 }, {  81, 109, 219 },  /*  16 */
    {  83, 111, 221 }, {  84, 112, 222 }, {  85, 114, 223 }, {  86, 116, 224 },  /*  20 */
    {  88, 117, 226 }, {  89, 119, 227 }, {  90, 120, 228 }, {  91, 122, 229 },  /*  24 */
    {  93, 124, 230 }, {  94, 125, 231 }, {  95, 127, 232 }, {  97, 129, 233 },  /*  28 */
    {  98, 130, 234 }, {  99, 132, 235 }, { 101, 133, 236 }, { 102, 135, 237 },  /*  32 */
    { 103, 136, 238 }, { 105, 138, 239 }, { 106, 140, 240 }, { 107, 141, 240 },  /*  36 */
    { 109, 143, 241 }, { 110, 144, 242 }, { 111, 146, 243 }, { 113, 147, 244 },  /*  40 */
    { 114, 149, 244 }, { 115, 150, 245 }, { 117, 152, 246 }, { 118, 153, 246 },  /*  44 */
    { 119, 154, 247 }, { 121, 156, 248 }, { 122, 157, 248 }, { 123, 159, 249 },  /*  48 */
    { 125, 160, 249 }, { 126, 162, 250 }, { 128, 163, 250 }, { 129, 164, 251 },  /*  52 */
    { 130, 166, 251 }, { 132, 167, 252 }, { 133, 168, 252 }, { 134, 170, 252 },  /*  56 */
    { 136, 171, 253 }, { 137, 172, 253 }, { 139, 174, 253 }, { 140, 175, 254 },  /*  60 */
    { 141, 176, 254 }, { 143, 177, 254 }, { 144, 178, 254 }, { 146, 180, 254 },  /*  64 */
    { 147, 181, 255 }, { 148, 182, 255 }, { 150, 183, 255 }, { 151, 184, 255 },  /*  68 */
    { 153, 186, 255 }, { 154, 187, 255 }, { 155, 188, 255 }, { 157, 189, 255 },  /*  72 */
    { 158, 190, 255 }, { 159, 191, 255 }, { 161, 192, 255 }, { 162, 193, 255 },  /*  76 */
    { 163, 194, 254 }, { 165, 195, 254 }, { 166, 196, 254 }, { 168, 197, 254 },  /*  80 */
    { 169, 198, 254 }, { 170, 199, 253 }, { 172, 200, 253 }, { 173, 201, 253 },  /*  84 */
    { 174, 201, 252 }, { 176, 202, 252 }, { 177, 203, 252 }, { 178, 204, 251 },  /*  88 */
    { 180, 205, 251 }, { 181, 206, 250 }, { 182, 206, 250 }, { 183, 207, 249 },  /*  92 */
    { 185, 208, 249 }, { 186, 209, 248 }, { 187, 209, 248 }, { 189, 210, 247 },  /*  96 */
    { 190, 211, 246 }, { 191, 211, 246 }, { 192, 212, 245 }, { 193, 212, 244 },  /* 100 */
    { 195, 213, 244 }, { 196, 214, 243 }, { 197, 214, 242 }, { 198, 215, 241 },  /* 104 */
    { 200, 215, 241 }, { 201, 216, 240 }, { 202, 216, 239 }, { 203, 216, 238 },  /* 108 */
    { 204, 217, 237 }, { 205, 217, 236 }, { 206, 218, 235 }, { 208, 218, 234 },  /* 112 */
    { 209, 218, 233 }, { 210, 219, 232 }, { 211, 219, 231 }, { 212, 219, 230 },  /* 116 */
    { 213, 219, 229 }, { 214, 220, 228 }, { 215, 220, 227 }, { 216, 220, 226 },  /* 120 */
    { 217, 220, 225 }, { 218, 220, 224 }, { 219, 221, 222 }, { 220, 221, 221 },  /* 124 */
    { 221, 220, 220 }, { 222, 220, 219 }, { 223, 220, 217 }, { 225, 219, 216 },  /* 128 */
    { 226, 218, 214 }, { 227, 218, 213 }, { 228, 217, 211 }, { 229, 217, 210 },  /* 132 */
    { 229, 216, 209 }, { 230, 216, 207 }, { 231, 215, 206 }, { 232, 214, 204 },  /* 136 */
    { 233, 214, 203 }, { 234, 213, 201 }, { 235, 212, 200 }, { 235, 211, 198 },  /* 140 */
    { 236, 211, 197 }, { 237, 210, 195 }, { 238, 209, 194 }, { 238, 208, 192 },  /* 144 */
    { 239, 207, 191 }, { 239, 206, 189 }, { 240, 206, 187 }, { 241, 205, 186 },  /* 148 */
    { 241, 204, 184 }, { 242, 203, 183 }, { 242, 202, 181 }, { 243, 201, 180 },  /* 152 */
    { 243, 200, 178 }, { 244, 199, 177 }, { 244, 198, 175 }, { 244, 197, 173 },  /* 156 */
    { 245, 196, 172 }, { 245, 195, 170 }, { 245, 193, 169 }, { 246, 192, 167 },  /* 160 */
    { 246, 191, 166 }, { 246, 190, 164 }, { 246, 189, 162 }, { 247, 188, 161 },  /* 164 */
    { 247, 186, 159 }, { 247, 185, 158 }, { 247, 184, 156 }, { 247, 183, 155 },  /* 168 */
    { 247, 181, 153 }, { 247, 180, 151 }, { 247, 179, 150 }, { 247, 177, 148 },  /* 172 */
    { 247, 176, 147 }, { 247, 175, 145 }, { 247, 173, 144 }, { 247, 172, 142 },  /* 176 */
    { 247, 171, 140 }, { 247, 169, 139 }, { 247, 168, 137 }, { 247, 166, 136 },  /* 180 */
    { 246, 165, 134 }, { 246, 163, 133 }, { 246, 162, 131 }, { 246, 160, 129 },  /* 184 */
    { 245, 159, 128 }, { 245, 157, 126 }, { 245, 156, 125 }, { 244, 154, 123 },  /* 188 */
    { 244, 153, 122 }, { 244, 151, 120 }, { 243, 149, 119 }, { 243, 148, 117 },  /* 192 */
    { 242, 146, 116 }, { 242, 144, 114 }, { 241, 143, 113 }, { 241, 141, 111 },  /* 196 */
    { 240, 139, 110 }, { 240, 138, 108 }, { 239, 136, 107 }, { 238, 134, 105 },  /* 200 */
    { 238, 133, 104 }, { 237, 131, 102 }, { 237, 129, 101 }, { 236, 127,  99 },  /* 204 */
    { 235, 125,  98 }, { 234, 124,  96 }, { 234, 122,  95 }, { 233, 120,  93 },  /* 208 */
    { 232, 118,  92 }, { 231, 116,  91 }, { 230, 114,  89 }, { 229, 112,  88 },  /* 212 */
    { 229, 111,  86 }, { 228, 109,  85 }, { 227, 107,  84 }, { 226, 105,  82 },  /* 216 */
    { 225, 103,  81 }, { 224, 101,  79 }, { 223,  99,  78 }, { 222,  97,  77 },  /* 220 */
    { 221,  95,  75 }, { 220,  93,  74 }, { 219,  91,  73 }, { 218,  89,  71 },  /* 224 */
    { 216,  86,  70 }, { 215,  84,  69 }, { 214,  82,  68 }, { 213,  80,  66 },  /* 228 */
    { 212,  78,  65 }, { 211,  76,  64 }, { 209,  73,  62 }, { 208,  71,  61 },  /* 232 */
    { 207,  69,  60 }, { 206,  67,  59 }, { 204,  64,  57 }, { 203,  62,  56 },  /* 236 */
    { 202,  59,  55 }, { 200,  57,  54 }, { 199,  54,  53 }, { 198,  52,  52 },  /* 240 */
    { 196,  49,  50 }, { 195,  46,  49 }, { 193,  43,  48 }, { 192,  40,  47 },  /* 244 */
    { 191,  37,  46 }, { 189,  34,  45 }, { 188,  30,  44 }, { 186,  26,  43 },  /* 248 */
    { 185,  22,  41 }, { 183,  17,  40 }, { 182,  11,  39 }, { 180,   4,  38 }   /* 252 */
};

/* The table row for coordinate v, where vmin is the smallest coordinate on
   the chosen axis and span the largest less the smallest:
   t = (v - vmin) / span clamped to [0, 1], or 0 when span is 0, and the row
   is floor(t * 255 + 0.5) clamped to [0, 255]. All in double precision, as
   the specification asks.

   t * 255 is stored in a volatile before 0.5 is added, so that it is
   rounded to a double on its own. A compiler may otherwise fuse the
   multiply and the add into one instruction with a single rounding, which
   on some machines would move a value that lies close to a half to the
   other row. */
int
plyc_table_index(double v, double vmin, double span)
{
    double t;
    volatile double scaled;
    double row;

    if (span <= 0.0)
        return 0;
    t = (v - vmin) / span;
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    scaled = t * 255.0;
    row = floor(scaled + 0.5);
    return row > 255.0 ? 255 : (int)row;
}
