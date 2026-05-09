# completely redone by krb for Amiga port.
# still the zconf.h is processed.
cmake_minimum_required (VERSION 3.1)
set(CXX_STANDARD 11)
set(C_STANDARD 11)
project(zlib LANGUAGES C CXX VERSION 1.17.6)


	add_definitions(-std=c11)
	
	set(BUILD_SHARED_LIBS OFF)
	set(SKIP_INSTALL_LIBRARIES ON)

	set(HAVE_SYS_TYPES_H ON)
	set(HAVE_STDINT_H ON)
	set(HAVE_STDDEF_H ON)

# ../zlib/adler32.c
#add_executable(woot test.c ../zlib/adler32.c)



set(ZLIB_PUBLIC_HDRS
    ${CMAKE_CURRENT_BINARY_DIR}/zconf.h
    zlib.h
)
set(ZLIB_PRIVATE_HDRS
    crc32.h
    deflate.h
    gzguts.h
    inffast.h
    inffixed.h
    inflate.h
    inftrees.h
    trees.h
    zutil.h
)
set(ZLIB_SRCS
    adler32.c
    compress.c
    crc32.c
    deflate.c
    gzclose.c
    gzlib.c
    gzread.c
    gzwrite.c
    inflate.c
    infback.c
    inftrees.c
    inffast.c
    trees.c
    uncompr.c
    zutil.c
)

message(WARNING "zlib compiled with options:${AMIGA_COMPILER_OPTIONS}")
add_library(z STATIC  ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS} ${ZLIB_PRIVATE_HDRS} )
set_property(TARGET z PROPERTY C_STANDARD 11)
if(AMIGA)
	target_compile_options(z PRIVATE -O2 ${AMIGA_COMPILER_OPTIONS} )
endif()

# generate zconf.h
configure_file(	${CMAKE_CURRENT_SOURCE_DIR}/../zlib/zconf.h.cmakein
		${CMAKE_CURRENT_BINARY_DIR}/zconf.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR})

# add_library(minizip STATIC
# 			${CMAKE_CURRENT_SOURCE_DIR}/contrib/minizip/unzip.c
# 			${CMAKE_CURRENT_SOURCE_DIR}/contrib/minizip/unzip.h )
# include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/../zlib/contribs/minizip)
# if(AMIGA)
# 	target_compile_options(minizip PRIVATE -O2 ${AMIGA_COMPILER_OPTIONS} )
# endif()
