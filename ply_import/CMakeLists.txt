cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)

# RPly's one source file is compiled into each tool rather than built as a
# library of its own.
add_executable(nano_prc_ply_import
               src/ply_import.c
               ${RPLY_DIR}/rply.c)

target_link_libraries(nano_prc_ply_import PRIVATE nano_prc)
target_include_directories(nano_prc_ply_import PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${RPLY_DIR})

# RPly uses fopen and sscanf, which MSVC deprecates in favour of its own
# _s variants; the warnings are about RPly's unchanged source, not ours.
if(MSVC)
    target_compile_definitions(nano_prc_ply_import PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Only link the math library if we aren't targeting Windows
if(NOT WIN32)
    target_link_libraries(nano_prc_ply_import PRIVATE m)
endif()
