cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# prc_ply_rw builds in one of two ways:
#
#  - as a subdirectory of nanoPRC (a git submodule there): the nano_prc target
#    already exists and is used as is;
#  - on its own: NANOPRC_SOURCE_DIR names a nanoPRC checkout, which is added
#    here with its demos, tests and SDL switched off.
#
# mesh2ply needs no nanoPRC: on its own without NANOPRC_SOURCE_DIR, only
# mesh2ply is built, and ply_import and ply_export are left out.
#
# This repository must never contain nanoPRC as a submodule. nanoPRC contains
# this one, and each containing the other would nest copies without end under
# `git submodule update --recursive`.

set(PRC_PLY_RW_WITH_NANOPRC ON)
if(NOT TARGET nano_prc)
    project(prc_ply_rw C)

    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endif()

    set(NANOPRC_SOURCE_DIR "" CACHE PATH "Path to a nanoPRC checkout")
    if(EXISTS "${NANOPRC_SOURCE_DIR}/CMakeLists.txt")
        set(NANOPRC_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
        set(NANOPRC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(NANOPRC_BUILD_SDL   OFF CACHE BOOL "" FORCE)
        add_subdirectory("${NANOPRC_SOURCE_DIR}" nanoprc)

        # nanoPRC sets its own output directory, so its shared library would land
        # apart from the tools and they would not run from the build directory.
        set_target_properties(nano_prc PROPERTIES
                              RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    else()
        message(STATUS
            "No nanoPRC checkout (-DNANOPRC_SOURCE_DIR=<path>): building mesh2ply only")
        set(PRC_PLY_RW_WITH_NANOPRC OFF)
    endif()
endif()

# A function called before it is declared is an implicit declaration, which
# C99 removed: GCC and Clang refuse it, but MSVC only warns (C4013) and the
# build goes on. Make that warning an error here too, for this repository's
# own targets; nanoPRC's, added above, are not affected.
if(MSVC)
    add_compile_options(/we4013)
endif()

# RPly 1.1.4 is vendored unchanged from the author's release tarball; it has
# no git repository to take as a submodule. See THIRD_PARTY_NOTICES.md.
set(RPLY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rply" CACHE PATH
    "Path to the RPly sources")

if(PRC_PLY_RW_WITH_NANOPRC)
    add_subdirectory(ply_import)
    add_subdirectory(ply_export)
endif()
add_subdirectory(mesh2ply)
