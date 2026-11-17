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

/* ply_colourise: SHA-256 of the output file, for the report's sha256_out
   column. Written from the Secure Hash Standard, NIST FIPS 180-4 (August
   2015), sections 4.1.2, 4.2.2, 5.1.1, 5.3.3 and 6.2. */

#include "plyc.h"

#include <stdio.h>
#include <string.h>

/* FIPS 180-4 section 4.2.2 */
static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t
rotr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

/* One 64-byte block (section 6.2.2) */
static void
compress(uint32_t h[8], const unsigned char *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, k, t1, t2;
    unsigned i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (i = 16; i < 64; i++)
    {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; k = h[7];
    for (i = 0; i < 64; i++)
    {
        t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

void
plyc_sha256_init(plyc_sha256 *s)
{
    /* Section 5.3.3 */
    static const uint32_t H0[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    memcpy(s->h, H0, sizeof(H0));
    s->fill = 0;
    s->bytes = 0;
}

void
plyc_sha256_update(plyc_sha256 *s, const unsigned char *data, size_t n)
{
    s->bytes += n;
    while (n > 0)
    {
        size_t take = 64 - s->fill;

        if (take > n)
            take = n;
        memcpy(s->block + s->fill, data, take);
        s->fill += take;
        data += take;
        n -= take;
        if (s->fill == 64)
        {
            compress(s->h, s->block);
            s->fill = 0;
        }
    }
}

/* Padding (section 5.1.1): a 1 bit, zeros, and the length in bits as a
   64-bit big-endian number, to a multiple of 64 bytes */
void
plyc_sha256_final(plyc_sha256 *s, unsigned char digest[32])
{
    uint64_t bits = s->bytes * 8;
    unsigned i;

    s->block[s->fill++] = 0x80;
    if (s->fill > 56)
    {
        memset(s->block + s->fill, 0, 64 - s->fill);
        compress(s->h, s->block);
        s->fill = 0;
    }
    memset(s->block + s->fill, 0, 56 - s->fill);
    for (i = 0; i < 8; i++)
        s->block[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
    compress(s->h, s->block);
    for (i = 0; i < 32; i++)
        digest[i] = (unsigned char)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}

/* The SHA-256 of a file's bytes as 64 lower-case hex digits. Returns 0 if
   the file cannot be read. */
int
plyc_sha256_file(const char *path, char hex[65])
{
    static unsigned char buf[1 << 16];
    static const char DIGITS[] = "0123456789abcdef";
    plyc_sha256 s;
    unsigned char d[32];
    FILE *f = fopen(path, "rb");
    size_t got;
    int i;

    hex[0] = '\0';
    if (f == NULL)
        return 0;
    plyc_sha256_init(&s);
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
        plyc_sha256_update(&s, buf, got);
    if (ferror(f))
    {
        fclose(f);
        return 0;
    }
    fclose(f);
    plyc_sha256_final(&s, d);
    for (i = 0; i < 32; i++)
    {
        hex[i * 2] = DIGITS[d[i] >> 4];
        hex[i * 2 + 1] = DIGITS[d[i] & 15];
    }
    hex[64] = '\0';
    return 1;
}
