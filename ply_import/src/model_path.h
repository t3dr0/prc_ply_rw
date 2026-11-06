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

/* Find an external file a model refers to (a texture image, an MTL
   library, a glTF buffer) without leaving the model's folder.

   Model files name their companions with whatever path their author's
   machine had: "textures/wood.png", "C:\Users\me\Desktop\wood.png",
   "../../shared/wood.png". Joining such a name to the model's folder can
   reach any file on the machine, whose bytes would then go into the
   output; and an absolute path from another machine is simply not found.
   So a reference is resolved in this order:

     1. A plain relative path that only goes down from the model's folder
        ("textures/wood.png") is used as written when that file exists.
     2. Otherwise the name is reduced to its last part ("wood.png") and
        looked for in the model's folder, then in its subfolders, nearest
        first: breadth-first, at most MODEL_PATH_MAX_DEPTH levels down,
        alphabetically within a folder, stopping after
        MODEL_PATH_MAX_ENTRIES directory entries.
     3. A name that matches exactly anywhere wins over one that differs
        only in letter case; the latter is used only when there is no exact
        match.

   Symbolic links and junctions are never followed, so the search cannot
   leave the model's folder through one. A reference that is a URL
   ("http://...", "data:...") is not a file and is not resolved here. */

#ifndef MODEL_PATH_H
#define MODEL_PATH_H

#include <stddef.h>

#define MODEL_PATH_MAX_DEPTH   4
#define MODEL_PATH_MAX_ENTRIES 10000

/* model_path_resolve results. */
#define MODEL_PATH_AS_WRITTEN   0   /* step 1: the reference as written */
#define MODEL_PATH_SUBSTITUTED  1   /* steps 2-3: a file found by name */
#define MODEL_PATH_NOT_FOUND   -1
#define MODEL_PATH_NO_MEMORY   -2

/* Resolve ref, as written in the model file at model_file, to a path that
   can be opened. On success *out is that path (free it with free()), and
   when the file was found by searching, note holds a one-line explanation
   for the user, e.g.
     'C:\textures\wood.png' is outside the model's folder; using 'tex/wood.png'
   Otherwise *out is NULL and note says why. note may be NULL. */
int model_path_resolve(const char *model_file, const char *ref, char **out,
                       char *note, size_t note_size);

/* The folder part of path including its last separator, or "" for none;
   free it with free(). NULL when memory runs out. */
char *model_path_folder(const char *path);

#endif
