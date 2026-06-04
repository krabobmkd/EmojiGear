cmake_minimum_required(VERSION 3.10)

enable_language( VASM )
project(UniButtonGadget C)

set(CMAKE_C_STANDARD 11)

list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -Wall)
list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -O2 )


option(UTF8RASTPORT_SHARED "link utf8rastport as amiga shared lib"  ON)


# When ON the gadget is compiled into a static library linked directly by
# the host application.  When OFF (future) the gadget would be a relocatable
# .gadget binary with its own startup; that path is not implemented yet.
option(UNIBUTTON_STATIC_LINK
    "Build UniButton as a statically-linked gadget class" OFF)

set(UNIBUTTON_SOURCES
    class_unibutton_lib.c
    class_unibutton_dispatch.c
    class_unibutton_attribs.c
    class_unibutton_render.c
    class_unibutton_input.c
#    bdbprintf.c
)
if(UNIBUTTON_STATIC_LINK)
	set(LIBNAME "unibutton")
	add_library(${LIBNAME} STATIC ${UNIBUTTON_SOURCES})
	target_link_libraries(${LIBNAME} PUBLIC unitexteditor)
else()
	set(LIBNAME "unibutton.gadget")
	add_executable(${LIBNAME}
		libinit.s
		${UNIBUTTON_SOURCES}
		offscreenbm.c
		)
	target_link_options(${LIBNAME} PRIVATE -ffreestanding -nostartfiles -Wl,-e,__start)
	target_compile_options(${LIBNAME} PRIVATE -fomit-frame-pointer )
endif()

# Headers in this directory are part of the public API
target_include_directories(${LIBNAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
							${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include)


# unitexteditor target must already exist (built by parent before this dir).
# Linking it provides:
#   - offscreenbm.h include path (PUBLIC from unitexteditor)
#   - OffscreenBitMap_Init/Close compiled objects
#   - utf8rastport transitively


if(UNIBUTTON_STATIC_LINK)
    target_compile_definitions(${LIBNAME}
        # Gadget sources: know they are compiled statically (future #ifdef use)
        PRIVATE UNIBUTTON_STATIC=1
        # Host application: propagated automatically via link
        PUBLIC  USE_STATIC_UNIBUTTON=1
    )
endif()

# Inherit CPU/FPU flags from the parent project scope
if(AMIGA_COMPILER_OPTIONS)
    target_compile_options(${LIBNAME} PRIVATE
        ${AMIGA_COMPILER_OPTIONS}
    )
endif()

# target_compile_definitions(${LIBNAME} PRIVATE USE_DEBUG_BDBPRINT=1)
