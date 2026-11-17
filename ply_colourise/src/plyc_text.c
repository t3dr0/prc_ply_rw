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

/* ply_colourise: a double written as Python's repr() writes it, so that the
   report's min and max columns read back as exactly the same value and match
   the specification's reference implementation character for character.

   The digits are the fewest that read back as the same double: printf's %e
   with 1, 2, ... 17 significant digits, the first that strtod turns back
   into the value. At a power of two the doubles below are closer together
   than those above, so the nearest number of that many digits can fail to
   read back while the next one away from it, on the wider side, does: that
   one is tried as well, as Python's shortest form would find it. The digits
   are then laid out as Python does: plain notation when the decimal
   exponent is from -4 to 15, with ".0" after a whole number, otherwise
   d.ddde+XX with at least two exponent digits. */

#include "plyc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* s is printf's %e form, [-]d[.ddd]e(+|-)xx. Writes to alt the number with
   the same count of digits one unit away in the last digit: up in magnitude
   if up, else down. Returns 0 if there is no such number of that form. */
static int
neighbour(const char *s, int up, char *alt)
{
    char d[24];
    int nd = 0, neg = 0, exp10, i;
    char *o = alt;

    if (*s == '-')
    {
        neg = 1;
        s++;
    }
    for (; *s != 'e'; s++)
        if (*s != '.')
            d[nd++] = *s;
    exp10 = atoi(s + 1);
    for (i = nd - 1; i >= 0; i--)
    {
        if (up && d[i] == '9')
            d[i] = '0';
        else if (!up && d[i] == '0')
            d[i] = '9';
        else
        {
            d[i] = (char)(d[i] + (up ? 1 : -1));
            break;
        }
    }
    if (i < 0)
        return 0;           /* 99..9 up or 00..0 down: not needed here */
    if (d[0] == '0')
    {
        /* 1000 down became 0999: one digit fewer, exponent one less */
        if (nd == 1)
            return 0;
        memmove(d, d + 1, (size_t)(nd - 1));
        nd--;
        exp10--;
    }
    if (neg)
        *o++ = '-';
    *o++ = d[0];
    if (nd > 1)
    {
        *o++ = '.';
        for (i = 1; i < nd; i++)
            *o++ = d[i];
    }
    sprintf(o, "e%d", exp10);
    return 1;
}

void
plyc_repr(double v, char out[40])
{
    char buf[40], digits[24] = "0";
    const char *p;
    char *o = out;
    int exp10, nd = 0, i, prec;

    if (isnan(v))
    {
        strcpy(out, "nan");
        return;
    }
    if (v == 0.0)
    {
        strcpy(out, signbit(v) ? "-0.0" : "0.0");
        return;
    }
    if (isinf(v) && v > 0.0)
    {
        strcpy(out, "inf");
        return;
    }
    if (isinf(v))
    {
        strcpy(out, "-inf");
        return;
    }
    for (prec = 1; prec <= 17; prec++)
    {
        char alt[40];
        double back;

        sprintf(buf, "%.*e", prec - 1, v);
        back = strtod(buf, NULL);
        if (back == v)
            break;
        if (neighbour(buf, fabs(back) < fabs(v), alt) && strtod(alt, NULL) == v)
        {
            strcpy(buf, alt);
            break;
        }
    }

    /* buf is [-]d[.ddd]e(+|-)xx */
    p = buf;
    if (*p == '-')
        *o++ = *p++;
    for (; *p != 'e'; p++)
        if (*p != '.')
            digits[nd++] = *p;
    exp10 = atoi(p + 1);
    while (nd > 1 && digits[nd - 1] == '0')
        nd--;

    if (exp10 >= -4 && exp10 < 16)
    {
        if (exp10 < 0)
        {
            *o++ = '0';
            *o++ = '.';
            for (i = -1; i > exp10; i--)
                *o++ = '0';
            for (i = 0; i < nd; i++)
                *o++ = digits[i];
        }
        else
        {
            for (i = 0; i <= exp10; i++)
                *o++ = i < nd ? digits[i] : '0';
            *o++ = '.';
            if (nd > exp10 + 1)
                for (i = exp10 + 1; i < nd; i++)
                    *o++ = digits[i];
            else
                *o++ = '0';
        }
        *o = '\0';
        return;
    }
    *o++ = digits[0];
    if (nd > 1)
    {
        *o++ = '.';
        for (i = 1; i < nd; i++)
            *o++ = digits[i];
    }
    sprintf(o, "e%c%02d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
}
