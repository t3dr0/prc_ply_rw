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

/* ply_colourise: whether INPUT and OUTPUT name the same file (specification
   section 4). Standard C cannot tell, so this is the one place that uses the
   operating system: on Windows the two full paths are compared without
   regard to letter case or the direction of slashes, and elsewhere, when
   both files exist, their device and file numbers. Otherwise the paths are
   compared as written. */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif

#include "plyc.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

static void
normalise(char *s)
{
    for (; *s != '\0'; s++)
        *s = *s == '/' ? '\\' : (char)tolower((unsigned char)*s);
}

int
plyc_same_file(const char *a, const char *b)
{
    char fa[1024], fb[1024];

    if (_fullpath(fa, a, sizeof(fa)) == NULL || _fullpath(fb, b, sizeof(fb)) == NULL)
        return strcmp(a, b) == 0;
    normalise(fa);
    normalise(fb);
    return strcmp(fa, fb) == 0;
}

#else

#include <sys/stat.h>

int
plyc_same_file(const char *a, const char *b)
{
    struct stat sa, sb;

    if (stat(a, &sa) == 0 && stat(b, &sb) == 0)
        return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
    return strcmp(a, b) == 0;
}

#endif
