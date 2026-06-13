cmake_minimum_required(VERSION 3.10)

project(fsnet C)
set(CMAKE_C_STANDARD 11)

# AmiSSL v5 SDK / SDI headers (bsdsocket.library comes from the standard NDK
# include dirs, already on the system include path via the toolchain file).
# Resolved to an absolute path so the AmiSSL static lib below is recognized
# as a file (not a -l flag) by target_link_libraries.
get_filename_component(COMMONLIBSDIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../amigacommonlibs" ABSOLUTE)

set(AMISSL_INCLUDE_DIR "${COMMONLIBSDIR}/extrasdk/AmiSSL/Developer/include"
    CACHE PATH "Path to AmiSSL v5 Developer include directory")
set(AMISSL_LIB_DIR "${COMMONLIBSDIR}/extrasdk/AmiSSL/Developer/lib/AmigaOS3"
    CACHE PATH "Path to AmiSSL v5 Developer lib directory (AmigaOS3)")
set(SDI_INCLUDE_DIR "${COMMONLIBSDIR}/extrasdk/SDI/include"
    CACHE PATH "Path to SDI headers (SDI_compiler.h etc.)")

# fsnet_http.c / fsnet_mastodon.c / fsnet_cache.c and the vendored cJSON
# sources land here in Phase 1 (see ../ARCHITECTURE.md). For now the library
# only contains the process/IPC skeleton.
set(FSNET_SOURCES
    fsnet.c
)

add_library(fsnet STATIC ${FSNET_SOURCES})

# fsnet.h is the public API shared with the GUI; AmiSSL/SDI dirs are exposed
# now so Phase 1 sources can be dropped in without touching this file.
target_include_directories(fsnet PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${AMISSL_INCLUDE_DIR}
    ${SDI_INCLUDE_DIR}
)

# Manual AmiSSL init path (see EmojiGear/testsocket/CMakeLists.txt): libnix'
# `errno` is `(*__errno)`, incompatible with libamisslauto.a's expectations,
# so we link the stub trampolines instead of using -lamisslauto.
target_link_libraries(fsnet PUBLIC
    ${AMISSL_LIB_DIR}/libamisslstubs.a
)

# Inherit CPU/optimization flags chosen by the parent project.
if(AMIGA_COMPILER_OPTIONS)
    target_compile_options(fsnet PRIVATE
        ${AMIGA_COMPILER_OPTIONS}
    )
endif()
