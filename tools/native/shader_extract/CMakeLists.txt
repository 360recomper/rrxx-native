# rrxx_shader_cache: builds the RRXX native renderer's shader cache on the player's machine (RR-NR-3,
# port of UFC-native's ufc3_shader_cache). No Python, no rexglue SDK: Windows crypto (bcrypt) for the
# xex and the file names, xxHash for the cache hash. RR's pacs are BPE (own code), no zlib needed.
#
# On its own (MSVC env + LLVM + Ninja on PATH):
#   cmake -S native/tools/native/shader_extract -B <build> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
#         -DCMAKE_CXX_COMPILER=clang++ [-DRRXX_SC_THIRD_PARTY=<folder with XenosRecomp/>]
#   cmake --build <build>
# RRXX_SC_THIRD_PARTY defaults to native/third_party (patched XenosRecomp: UFC's stack + xenosrecomp-0e.patch).

cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(rrxx_shader_cache LANGUAGES CXX)
endif()

get_filename_component(RRXX_SC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(RRXX_SC_THIRD_PARTY "${RRXX_SC_ROOT}/third_party" CACHE PATH "folder holding the patched XenosRecomp")
set(RRXX_SC_XENOS "${RRXX_SC_THIRD_PARTY}/XenosRecomp")
set(RRXX_SC_COMMON "${RRXX_SC_XENOS}/XenosRecomp/shader_common.h")
if(NOT EXISTS "${RRXX_SC_COMMON}")
    message(FATAL_ERROR "rrxx_shader_cache: ${RRXX_SC_XENOS} (patched XenosRecomp) missing")
endif()

# XenosRecomp reads shader_common.h from a file at run time. Embed it (as bytes, so the tool
# writes back exactly this file) instead of shipping it loose; a change re-runs the configure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RRXX_SC_COMMON}")
file(READ "${RRXX_SC_COMMON}" RRXX_SC_COMMON_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," RRXX_SC_COMMON_BYTES "${RRXX_SC_COMMON_HEX}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)"
       "\\1\n" RRXX_SC_COMMON_BYTES "${RRXX_SC_COMMON_BYTES}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shader_common_embed.inc"
     CONTENT "${RRXX_SC_COMMON_BYTES}\n" @ONLY)

add_executable(rrxx_shader_cache
    main.cpp
    shader_set.cpp
    shader_set.h
    win_crypto.cpp
    win_crypto.h
    xex_image.cpp
    xex_image.h)
target_compile_features(rrxx_shader_cache PRIVATE cxx_std_20)
target_include_directories(rrxx_shader_cache PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"           # shader_common_embed.inc
    "${RRXX_SC_XENOS}/thirdparty/xxHash")
target_compile_definitions(rrxx_shader_cache PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(rrxx_shader_cache PRIVATE bcrypt)
# Static C runtime: the tool runs before anything else of the native renderer does.
set_target_properties(rrxx_shader_cache PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
