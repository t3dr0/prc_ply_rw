cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# mesh2ply needs neither nanoPRC nor any library beyond RPly, whose one
# source file is compiled in, as the other tools do.
set(MESH2PLY_SOURCES
    src/mesh2ply.c
    src/m2p_util.c
    src/m2p_read_off.c
    src/m2p_read_ply.c
    src/m2p_clean.c
    src/m2p_write_ply.c)

add_executable(mesh2ply ${MESH2PLY_SOURCES} ${RPLY_DIR}/rply.c)
target_include_directories(mesh2ply PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${RPLY_DIR})

# The specification asks for no warnings at these levels. They apply to
# mesh2ply's own sources; RPly is vendored unchanged.
if(MSVC)
    set_source_files_properties(${MESH2PLY_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4")
    target_compile_definitions(mesh2ply PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    set_source_files_properties(${MESH2PLY_SOURCES} PROPERTIES
                                COMPILE_OPTIONS "-Wall;-Wextra;-pedantic")
    target_link_libraries(mesh2ply PRIVATE m)
endif()
