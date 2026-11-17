cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ply_colourise reads and writes PLY with mesh2ply's reader and writer and
# RPly, compiled in from their own folders, as mesh2ply compiles RPly in. It
# needs neither nanoPRC nor any other library.
set(PLY_COLOURISE_SOURCES
    src/ply_colourise.c
    src/plyc_layout.c
    src/plyc_field.c
    src/plyc_sha256.c
    src/plyc_text.c
    src/plyc_path.c)

set(MESH2PLY_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mesh2ply/src)
set(PLY_COLOURISE_MESH2PLY_SOURCES
    ${MESH2PLY_SRC_DIR}/m2p_util.c
    ${MESH2PLY_SRC_DIR}/m2p_read_ply.c
    ${MESH2PLY_SRC_DIR}/m2p_write_ply.c)

add_executable(ply_colourise ${PLY_COLOURISE_SOURCES} ${PLY_COLOURISE_MESH2PLY_SOURCES}
               ${RPLY_DIR}/rply.c)
target_include_directories(ply_colourise PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${MESH2PLY_SRC_DIR}
                           ${RPLY_DIR})

# The same warning levels as mesh2ply, for this tool's sources and mesh2ply's;
# RPly is vendored unchanged.
if(MSVC)
    set_source_files_properties(${PLY_COLOURISE_SOURCES} ${PLY_COLOURISE_MESH2PLY_SOURCES}
                                PROPERTIES COMPILE_OPTIONS "/W4")
    target_compile_definitions(ply_colourise PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    set_source_files_properties(${PLY_COLOURISE_SOURCES} ${PLY_COLOURISE_MESH2PLY_SOURCES}
                                PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-pedantic")
    target_link_libraries(ply_colourise PRIVATE m)
endif()
