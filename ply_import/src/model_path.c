/* Copyright (C) 2026 Ian Curington, Thames 3D Research

    This file is shared, unchanged, by prc_glb_rw, prc_obj_rw and prc_ply_rw.

    It is free software: you can redistribute it and/or modify it under the
    terms of the GNU Affero General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    It is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with it. If not, see <https://www.gnu.org/licenses/>.
*/

/* Directory listing and file tests are the only parts that differ between
   systems: FindFirstFile and GetFileAttributes on Windows, opendir and
   lstat elsewhere. Both skip links (reparse points on Windows). */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "model_path.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define KIND_NONE 0
#define KIND_FILE 1
#define KIND_DIR  2

/* What path is, without following a link: a link counts as neither. */
static int
kind_of(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path[0] ? path : ".");

    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT))
        return KIND_NONE;
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? KIND_DIR : KIND_FILE;
#else
    struct stat st;

    if (lstat(path[0] ? path : ".", &st) != 0)
        return KIND_NONE;
    if (S_ISREG(st.st_mode))
        return KIND_FILE;
    if (S_ISDIR(st.st_mode))
        return KIND_DIR;
    return KIND_NONE;
#endif
}

typedef struct
{
    char *name;
    int kind;
} entry;

static int
compare_entries(const void *a, const void *b)
{
    return strcmp(((const entry *)a)->name, ((const entry *)b)->name);
}

static char *
join(const char *a, const char *b, const char *c)
{
    size_t na = strlen(a), nb = strlen(b), nc = strlen(c);
    char *s = (char *)malloc(na + nb + nc + 1);

    if (s != NULL)
    {
        memcpy(s, a, na);
        memcpy(s + na, b, nb);
        memcpy(s + na + nb, c, nc + 1);
    }
    return s;
}

static void
free_entries(entry *e, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        free(e[i].name);
    free(e);
}

/* The files and folders in dir (a path ending in a separator, or "" for
   the current folder), links left out, sorted by name. *budget is the
   number of entries still allowed; listing stops when it reaches 0.
   Returns 0, or -1 when memory runs out. A folder that cannot be read
   lists as empty. */
static int
list_dir(const char *dir, entry **out, size_t *count, size_t *budget)
{
    entry *e = NULL;
    size_t n = 0, cap = 0;
    int rc = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char *pattern = join(dir[0] ? dir : ".\\", "*", "");

    if (pattern == NULL)
        return -1;
    h = FindFirstFileA(pattern, &fd);
    free(pattern);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            const char *name = fd.cFileName;
            int kind;

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;
            kind = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? KIND_DIR : KIND_FILE;
#else
    DIR *d = opendir(dir[0] ? dir : ".");
    struct dirent *de;

    if (d != NULL)
    {
        while ((de = readdir(d)) != NULL)
        {
            const char *name = de->d_name;
            char *full;
            int kind;

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            full = join(dir, name, "");
            if (full == NULL)
            {
                rc = -1;
                break;
            }
            kind = kind_of(full);
            free(full);
            if (kind == KIND_NONE)
                continue;
#endif
            if (*budget == 0)
                break;
            (*budget)--;
            if (n == cap)
            {
                size_t c = cap ? cap * 2 : 32;
                entry *bigger = (entry *)realloc(e, c * sizeof(entry));

                if (bigger == NULL)
                {
                    rc = -1;
                    break;
                }
                e = bigger;
                cap = c;
            }
            e[n].name = join(name, "", "");
            e[n].kind = kind;
            if (e[n].name == NULL)
            {
                rc = -1;
                break;
            }
            n++;
#ifdef _WIN32
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
        }
        closedir(d);
    }
#endif
    if (rc != 0)
    {
        free_entries(e, n);
        return rc;
    }
    if (n > 1)
        qsort(e, n, sizeof(entry), compare_entries);
    *out = e;
    *count = n;
    return 0;
}

static int
same_ignoring_case(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b))
    {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

char *
model_path_folder(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    size_t n;
    char *out;

    if (bslash > slash)
        slash = bslash;
    n = slash != NULL ? (size_t)(slash - path + 1) : 0;
    out = (char *)malloc(n + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

/* ref with '\' as '/', when it is a plain relative path that only goes
   down: no leading separator, no drive or scheme, no ".." part. Empty and
   "." parts are dropped. NULL when it is not, or memory runs out. */
static char *
downward_path(const char *ref)
{
    size_t n = strlen(ref), i, o = 0;
    char *s;

    if (n == 0 || ref[0] == '/' || ref[0] == '\\' || strchr(ref, ':') != NULL)
        return NULL;
    s = (char *)malloc(n + 1);
    if (s == NULL)
        return NULL;
    for (i = 0; i <= n;)
    {
        size_t j = i;

        while (j < n && ref[j] != '/' && ref[j] != '\\')
            j++;
        if (j - i == 2 && ref[i] == '.' && ref[i + 1] == '.')
        {
            free(s);
            return NULL;
        }
        if (j > i && !(j - i == 1 && ref[i] == '.'))
        {
            if (o > 0)
                s[o++] = '/';
            memcpy(s + o, ref + i, j - i);
            o += j - i;
        }
        i = j + 1;
    }
    s[o] = '\0';
    if (o == 0)
    {
        free(s);
        return NULL;
    }
    return s;
}

/* Every folder on the way to folder + rel, and the file itself, must be
   real, not a link. */
static int
plain_file_below(const char *folder, const char *rel)
{
    char *path = join(folder, rel, "");
    size_t k = strlen(folder);
    int ok = path != NULL;

    while (ok && path[k] != '\0')
    {
        if (path[k] == '/')
        {
            path[k] = '\0';
            ok = kind_of(path) == KIND_DIR;
            path[k] = '/';
        }
        k++;
    }
    if (ok)
        ok = kind_of(path) == KIND_FILE;
    free(path);
    return ok;
}

/* The last part of ref: after any '/', '\' or ':'. */
static const char *
file_name_of(const char *ref)
{
    const char *p, *name = ref;

    for (p = ref; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':')
            name = p + 1;
    return name;
}

static void
say(char *note, size_t size, const char *fmt, const char *a, const char *b, unsigned extra)
{
    int k;

    if (note == NULL || size == 0)
        return;
    k = snprintf(note, size, fmt, a, b);
    if (extra > 0 && k > 0 && (size_t)k < size)
        snprintf(note + k, size - (size_t)k, " (%u other file%s of that name ignored)",
                 extra, extra == 1 ? "" : "s");
}

/* A queue of folders, relative to the model's folder, each ending in '/'
   (or "" for the model's folder itself). */
typedef struct
{
    char **dir;
    int *depth;
    size_t head, tail, cap;
} queue;

static int
push_dir(queue *q, char *dir, int depth)
{
    if (q->tail == q->cap)
    {
        size_t c = q->cap ? q->cap * 2 : 16;
        char **d = (char **)realloc(q->dir, c * sizeof(char *));
        int *p;

        if (d == NULL)
            return -1;
        q->dir = d;
        p = (int *)realloc(q->depth, c * sizeof(int));
        if (p == NULL)
            return -1;
        q->depth = p;
        q->cap = c;
    }
    q->dir[q->tail] = dir;
    q->depth[q->tail++] = depth;
    return 0;
}

int
model_path_resolve(const char *model_file, const char *ref, char **out,
                   char *note, size_t note_size)
{
    char *folder, *rel;
    const char *name;
    queue q = { NULL, NULL, 0, 0, 0 };
    char *exact = NULL, *folded = NULL;
    unsigned exact_others = 0, folded_others = 0;
    size_t budget = MODEL_PATH_MAX_ENTRIES;
    int rc = MODEL_PATH_NOT_FOUND;
    int memory = 0;

    *out = NULL;
    if (note != NULL && note_size > 0)
        note[0] = '\0';
    if (model_file == NULL || ref == NULL)
        return MODEL_PATH_NOT_FOUND;
    if (strstr(ref, "://") != NULL || strncmp(ref, "data:", 5) == 0)
    {
        say(note, note_size, "'%s' is a URL, not a file%s", ref, "", 0);
        return MODEL_PATH_NOT_FOUND;
    }
    folder = model_path_folder(model_file);
    if (folder == NULL)
        return MODEL_PATH_NO_MEMORY;

    /* 1. As written, when it only goes down. */
    rel = downward_path(ref);
    if (rel != NULL && plain_file_below(folder, rel))
    {
        *out = join(folder, rel, "");
        free(rel);
        free(folder);
        return *out != NULL ? MODEL_PATH_AS_WRITTEN : MODEL_PATH_NO_MEMORY;
    }
    free(rel);

    /* 2. By name, nearest first. */
    name = file_name_of(ref);
    if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        free(folder);
        say(note, note_size, "'%s' does not name a file%s", ref, "", 0);
        return MODEL_PATH_NOT_FOUND;
    }
    {
        char *start = join("", "", "");

        if (start == NULL || push_dir(&q, start, 0) != 0)
        {
            free(start);
            memory = 1;
        }
    }
    while (!memory && q.head < q.tail)
    {
        char *dir = q.dir[q.head];
        int depth = q.depth[q.head++];
        char *full = join(folder, dir, "");
        entry *e = NULL;
        size_t n = 0, i;

        if (full == NULL || list_dir(full, &e, &n, &budget) != 0)
        {
            free(full);
            memory = 1;
            break;
        }
        free(full);
        for (i = 0; i < n && !memory; i++)
        {
            if (e[i].kind == KIND_FILE)
            {
                if (strcmp(e[i].name, name) == 0)
                {
                    if (exact == NULL)
                        memory = (exact = join(dir, e[i].name, "")) == NULL;
                    else
                        exact_others++;
                }
                else if (same_ignoring_case(e[i].name, name))
                {
                    if (folded == NULL)
                        memory = (folded = join(dir, e[i].name, "")) == NULL;
                    else
                        folded_others++;
                }
            }
            else if (e[i].kind == KIND_DIR && depth < MODEL_PATH_MAX_DEPTH)
            {
                char *sub = join(dir, e[i].name, "/");

                if (sub == NULL || push_dir(&q, sub, depth + 1) != 0)
                {
                    free(sub);
                    memory = 1;
                }
            }
        }
        free_entries(e, n);
    }
    /* Folders listed and folders not reached alike are still allocated. */
    for (q.head = 0; q.head < q.tail; q.head++)
        free(q.dir[q.head]);
    free(q.dir);
    free(q.depth);

    if (memory)
        rc = MODEL_PATH_NO_MEMORY;
    else if (exact != NULL || folded != NULL)
    {
        const char *found = exact != NULL ? exact : folded;

        *out = join(folder, found, "");
        if (*out == NULL)
            rc = MODEL_PATH_NO_MEMORY;
        else
        {
            rc = MODEL_PATH_SUBSTITUTED;
            if (exact != NULL)
                say(note, note_size, "'%s' is not in the model's folder as written; using '%s'",
                    ref, found, exact_others);
            else
                say(note, note_size, "'%s' is not in the model's folder as written; using '%s', "
                    "whose name differs in letter case", ref, found, folded_others);
        }
    }
    else if (budget == 0)
        say(note, note_size, "'%s' not found in the model's folder or its subfolders "
            "(the search stopped after %s entries)", ref, "10000", 0);
    else
        say(note, note_size, "'%s' not found in the model's folder or its subfolders%s", ref, "", 0);
    free(exact);
    free(folded);
    free(folder);
    return rc;
}
