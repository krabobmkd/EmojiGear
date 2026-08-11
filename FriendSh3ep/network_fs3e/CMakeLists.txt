cmake_minimum_required(VERSION 3.10)

project(fs3enet C)
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

set(FS3ENET_SOURCES
    fs3enet.c
    fs3enet_http.c
    fs3enet_mastodon.c
    fs3enet_cache.c
    cjson/cJSON.c
)

# Headers are listed too so IDEs (Qt Creator, etc.) show them in the project tree.
set(FS3ENET_HEADERS
    fs3enet.h
    fs3enet_http.h
    fs3enet_mastodon.h
    fs3enet_cache.h
    cjson/cJSON.h
)

add_library(fs3enet STATIC ${FS3ENET_SOURCES} ${FS3ENET_HEADERS})

# fs3enet.h/fs3enet_http.h/fs3enet_mastodon.h are the public API shared with the
# GUI; AmiSSL/SDI/cjson dirs are exposed alongside them.
target_include_directories(fs3enet PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${AMISSL_INCLUDE_DIR}
    ${SDI_INCLUDE_DIR}
)

# Parent dir so fs3enet.c can reach bdbprintf.h/compilers.h for the
# bdbprintf_now() request-queue tracing in FS3ENet_ProcEntry -- PRIVATE, not
# part of fs3enet's public API surface.
target_include_directories(fs3enet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Matches the GUI target's own USE_DEBUG_BDBPRINT (see the root
# CMakeLists.txt) so bdbprintf_now() calls here would compile to the real
# implementation instead of the no-op stub. BDBTRACEMULTIPART additionally
# turns on the chunked-download tracing in fs3enet.c
# (FS3ENet_StepActiveDownloads/FinishDownload) and the raw-request status/
# redirect tracing in fs3enet_http.c -- both were needed to diagnose
# archive (.zip/.lha) downloads failing on GitHub-style redirecting hosts
# (see FS3EHttp_DoRawRequest's redirect-following doc comment), now fixed.
# Left disabled again; uncomment both defines to re-enable that tracing if
# a similar issue needs debugging again.
#target_compile_definitions(fs3enet PRIVATE USE_DEBUG_BDBPRINT=1 BDBTRACEMULTIPART=1)

# Manual AmiSSL init path (see EmojiGear/testsocket/CMakeLists.txt): libnix'
# `errno` is `(*__errno)`, incompatible with libamisslauto.a's expectations,
# so we link the stub trampolines instead of using -lamisslauto.
target_link_libraries(fs3enet PUBLIC
    ${AMISSL_LIB_DIR}/libamisslstubs.a
)

# Inherit CPU/optimization flags chosen by the parent project.
if(AMIGA_COMPILER_OPTIONS)
    target_compile_options(fs3enet PRIVATE
        ${AMIGA_COMPILER_OPTIONS}
    )
endif()
