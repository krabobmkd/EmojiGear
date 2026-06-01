# CMakeLists.txt  –  libutf8rastport + FreeType2 for AmigaOS/m68k
#
# Builds three targets:
#   freetype        – FreeType2 static lib (TTF/OTF + PNG emoji via libpng)
#   utf8rastport    – UTF-8 RastPort drawing static lib (links freetype)
#   test_urp        – validation executable (opens Intuition window, draws text)
#
# Build (from this directory):
#   mkdir build && cd build
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/krabob/Code/Amiga/amigacommonlibs/cmake/Modules/Platform/m68k-amigaos.cmake
#   cmake --build .
#
# Run on Amiga:
#   test_urp [path/to/font.ttf] [pointsize]

cmake_minimum_required(VERSION 3.12)
enable_language( VASM )
project(utf8rastport C)

set(CMAKE_C_STANDARD 11)

option(UTF8RASTPORT_SHARED "link utf8rastport as amiga shared lib"  ON)

# -------------------------------------------------------------------------
# Amiga CPU/FPU options: 68020, software floating-point (no FPU required)
# -------------------------------------------------------------------------

set(AMIGA_CPU_FLAGS -mcpu=68020 -msoft-float -O3)


# Assembler output for debug
list(APPEND AMIGA_CPU_FLAGS -save-temps)

# -------------------------------------------------------------------------
# zlib + libpng  –  required for FreeType to decode CBDT/PNG color emoji
#
# Both libraries live in amigacommonlibs and already have Amiga-aware
# CMakeLists.  libpng expects zlib at ../zlib relative to its own source
# directory; the binary dir layout below satisfies that expectation:
#   build/zlib/    ← zlib build (generates zconf.h here)
#   build/libpng/  ← libpng build (ZLIB_BUILD_DIRS = ../zlib = build/zlib)
# -------------------------------------------------------------------------
set(PNG_ZLIB_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../zlib)
set(PNG_LIB_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../libpng)

# Suppress libpng options we don't need
set(PNG_SHARED  OFF CACHE BOOL "" FORCE)
set(PNG_STATIC  ON  CACHE BOOL "" FORCE)
set(PNG_TESTS   OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS   OFF CACHE BOOL "" FORCE)
set(PNG_BUILD_ZLIB ON CACHE BOOL "" FORCE)   # use ../zlib, not system zlib
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
# Force libpng to use scripts/pnglibconf.h.prebuilt instead of AWK-generated
# config. The AWK probe compiles with the host compiler and may introduce
# floating-point or gamma code we don't want on a no-FPU Amiga target.
set(AWK "" CACHE FILEPATH "awk program" FORCE)

add_subdirectory(${PNG_ZLIB_DIR}  ${CMAKE_CURRENT_BINARY_DIR}/zlib)
add_subdirectory(${PNG_LIB_DIR}   ${CMAKE_CURRENT_BINARY_DIR}/libpng)

if(AMIGA)
    target_compile_options(z        PRIVATE ${AMIGA_CPU_FLAGS})
    target_compile_options(png_static PRIVATE ${AMIGA_CPU_FLAGS})
endif()


# -------------------------------------------------------------------------
# FreeType 2  –  minimal static library for TrueType + OpenType + PNG emoji
#
# We compile FreeType directly (bypassing its own CMakeLists.txt) to avoid
# platform-detection probes that do not work cleanly in cross-compilation.
# FT_CONFIG_OPTION_USE_PNG enables sfnt/pngshim.c which calls libpng to
# decode CBDT/CBLC color bitmap glyphs (e.g. NotoColorEmoji).
# -------------------------------------------------------------------------
set(FT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../freetype-2.14.3)

set(FREETYPE_SRCS
    # Core
    ${FT_ROOT}/src/base/ftbase.c
    ${FT_ROOT}/src/base/ftinit.c
    ${FT_ROOT}/builds/amiga/src/base/ftsystem.c  # Amiga: AllocPool+Open/Read, no malloc/fopen
    ${FT_ROOT}/src/base/ftdebug.c

    # Useful base extensions
    ${FT_ROOT}/src/base/ftbbox.c
    ${FT_ROOT}/src/base/ftbitmap.c
    ${FT_ROOT}/src/base/ftglyph.c
    ${FT_ROOT}/src/base/ftmm.c        # FT_Set_Named_Instance (variable fonts)
    ${FT_ROOT}/src/base/ftstroke.c
    ${FT_ROOT}/src/base/ftsynth.c

    # Font format drivers (TrueType + OpenType/CFF only)
    ${FT_ROOT}/src/truetype/truetype.c  # TrueType (.ttf, .ttc)
    ${FT_ROOT}/src/cff/cff.c            # CFF/OpenType (.otf, .otc)
    ${FT_ROOT}/src/sfnt/sfnt.c          # Shared SFNT tables (used by both)
    ${FT_ROOT}/src/psaux/psaux.c        # PostScript auxiliary (CFF deps)
    ${FT_ROOT}/src/psnames/psnames.c    # PostScript glyph names

    # Hinting
    ${FT_ROOT}/src/autofit/autofit.c    # Auto-hinter (TTF + CFF)
    ${FT_ROOT}/src/pshinter/pshinter.c  # PostScript hinter (CFF)

    # Renderers
    ${FT_ROOT}/src/raster/raster.c      # Monochrome (1-bit) renderer
    ${FT_ROOT}/src/smooth/smooth.c      # Anti-aliased (8-bit grey) renderer

    # Compressed font support (uses FreeType's built-in mini-zlib/mini-lzw)
    ${FT_ROOT}/src/gzip/ftgzip.c
    ${FT_ROOT}/src/lzw/ftlzw.c

    # Glyph cache (optional but useful for performance)
    ${FT_ROOT}/src/cache/ftcache.c
)

add_library(freetype STATIC ${FREETYPE_SRCS})

target_compile_definitions(freetype PRIVATE
    FT2_BUILD_LIBRARY           # required: tells FreeType it is being built
    FT_CONFIG_OPTION_USE_PNG    # enables sfnt/pngshim.c → CBDT color emoji
)

# ft_amiga_config/ is listed BEFORE the FreeType include/ so that our
# custom ftmodule.h (with only TTF+OTF modules) shadows the stock one.
# libpng source + binary dirs provide png.h and the generated pnglibconf.h.
# zlib source + binary dirs provide zlib.h / zconf.h required by png.h
# (pngshim.c is compiled inside FreeType's sfnt.c and uses angle-bracket
#  includes, so the external zlib must be reachable from FreeType's include
#  path; FreeType's own bundled mini-zlib uses quoted "zlib.h" so no clash).
target_include_directories(freetype
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ft_amiga_config
    PRIVATE ${PNG_LIB_DIR}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/libpng
    PRIVATE ${PNG_ZLIB_DIR}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/zlib
    PUBLIC  ${FT_ROOT}/include
)

# FreeType's pngshim.c calls libpng; libpng calls zlib.
# png_static does not link z itself (commented out in its CMakeLists),
# so both must be listed here.
target_link_libraries(freetype PRIVATE png_static z)

if(AMIGA)
    target_compile_options(freetype PRIVATE ${AMIGA_CPU_FLAGS})
endif()
if(UTF8RASTPORT_SHARED)
	target_link_options(freetype PRIVATE -ffreestanding -nostartfiles -Wl,-e,__start)
	target_compile_options(freetype PRIVATE -fomit-frame-pointer )
	target_link_options(png_static PRIVATE -ffreestanding -nostartfiles -Wl,-e,__start)
	target_compile_options(png_static PRIVATE -fomit-frame-pointer )
	target_link_options(z PRIVATE -ffreestanding -nostartfiles -Wl,-e,__start)
	target_compile_options(z PRIVATE -fomit-frame-pointer )
endif()
# -------------------------------------------------------------------------
# FriBidi 1.0.16  –  Unicode Bidirectional Algorithm static library
#
# Compiled directly (no autoconf) with a hand-crafted fribidi-config.h
# placed in fribidi-amiga-config/ (same pattern as ft_amiga_config/).
# Only the core algorithm sources are included; charset converters and
# deprecated API are excluded.
# -------------------------------------------------------------------------
if(USEFRIBIDI)
set(FRIBIDI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../fribidi/lib)

set(FRIBIDI_SRCS
    ${FRIBIDI_ROOT}/fribidi.c
    ${FRIBIDI_ROOT}/fribidi-bidi.c
    ${FRIBIDI_ROOT}/fribidi-bidi-types.c
    ${FRIBIDI_ROOT}/fribidi-brackets.c
    ${FRIBIDI_ROOT}/fribidi-joining.c
    ${FRIBIDI_ROOT}/fribidi-joining-types.c
    ${FRIBIDI_ROOT}/fribidi-mirroring.c
    ${FRIBIDI_ROOT}/fribidi-arabic.c
    ${FRIBIDI_ROOT}/fribidi-shape.c
    ${FRIBIDI_ROOT}/fribidi-run.c
    ${FRIBIDI_ROOT}/fribidi-char-sets.c
    ${FRIBIDI_ROOT}/fribidi-char-sets-utf8.c
)

add_library(fribidi STATIC ${FRIBIDI_SRCS})

target_compile_definitions(fribidi PRIVATE
    HAVE_STDLIB_H=1
    HAVE_STRING_H=1
    HAVE_STRINGIZE=1
    STDC_HEADERS=1
    FRIBIDI_LIB_STATIC
    FRIBIDI_NO_DEPRECATED
)

# Our fribidi-amiga-config/ provides fribidi-config.h and must be searched
# before the library source dir so it shadows the .in template.
target_include_directories(fribidi
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fribidi-amiga-config
    PUBLIC  ${FRIBIDI_ROOT}
)

if(AMIGA)
    target_compile_options(fribidi PRIVATE ${AMIGA_CPU_FLAGS})
endif()

endif()

# -------------------------------------------------------------------------
# libutf8rastport  –  UTF-8 RastPort static library
# -------------------------------------------------------------------------

if(UTF8RASTPORT_SHARED)
	set(UTF8RPNAME "utf8rastport.library")
	add_executable( ${UTF8RPNAME}
		libinit.s
		libcinit.c
		lib_amiga_crt.c
		utf8rastport.c
		urp_cgx_blend.c
		bdbprintf.c
	)

target_link_options(${UTF8RPNAME} PRIVATE
    -nostdlib -ffreestanding -nostartfiles -Wl,-e,__start)
target_compile_options(${UTF8RPNAME} PRIVATE -fomit-frame-pointer )

# lib_amiga_crt.c provides memset/memcpy/strlen/etc. With -O3, GCC recognises
# our simple loops as built-in patterns and replaces them with calls to the
# very function being compiled → infinite recursion / freeze.
# -fno-builtin suppresses that only for this translation unit.
set_source_files_properties(lib_amiga_crt.c PROPERTIES
    COMPILE_FLAGS "-fno-builtin")
#target_compile_definitions(${UTF8RPNAME} PRIVATE PRIVATE VERSION_STRING="${VERSTRING}")


else()
	set(UTF8RPNAME "utf8rastport")
	add_library(${UTF8RPNAME} STATIC
		utf8rastport.c
		urp_cgx_blend.c
	)
endif()


target_include_directories(${UTF8RPNAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# utf8rastport consumers automatically get FreeType headers via PUBLIC linkage

	target_link_libraries(${UTF8RPNAME} PUBLIC freetype )

# libgcc m020 variant: __udivdi3 / __divdi3 without soft-32 fallbacks.
# Must come after freetype/png/zlib so the linker resolves refs first.
# The library targets 68020+ (compiled -mcpu=68020) so the m020 ABI is correct.
if(UTF8RASTPORT_SHARED)
    target_link_libraries(${UTF8RPNAME} PRIVATE
        /opt/amiga/lib/gcc/m68k-amigaos/6.5.0b/libm020/libgcc.a)
endif()

if(USEFRIBIDI)
	target_link_libraries(${UTF8RPNAME} PUBLIC fribidi )
endif()

if(AMIGA)
    target_compile_options(${UTF8RPNAME} PRIVATE ${AMIGA_CPU_FLAGS})
endif()
target_compile_definitions(${UTF8RPNAME} PRIVATE USE_DEBUG_BDBPRINT=1)


