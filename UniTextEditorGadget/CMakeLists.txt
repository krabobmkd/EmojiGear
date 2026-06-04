cmake_minimum_required(VERSION 3.10)

enable_language( VASM )
project(UniTextEditorGadget C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -Wall)
list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -O2 )

option(UTF8RASTPORT_SHARED "link utf8rastport as amiga shared lib"  ON)
option(UNITEXTEDTORDEBUG "woot"  OFF)
# When ON the gadget is compiled into a static library linked directly by
# the host application.  When OFF (future) the gadget would be a relocatable
# .gadget binary with its own startup; that path is not implemented yet.
option(UNITEXTEDITOR_STATIC_LINK
    "Build UniTextEditor as a statically-linked gadget class" OFF)

set(UNITEXTEDITOR_SOURCES
    class_unitexteditor_lib.c
    class_unitexteditor_dispatch.c
    class_unitexteditor_attribs.c
    class_unitexteditor_clipboard.c
    class_unitexteditor_text.c
    class_unitexteditor_lines.c
    class_unitexteditor_render.c
    class_unitexteditor_handleinput.c
    class_unitexteditor_undo.c
)
if(UNITEXTEDTORDEBUG)
	list(APPEND UNITEXTEDITOR_SOURCES bdbprintf.c)
endif()
if(UNITEXTEDITOR_STATIC_LINK)
	set(UTENAME unitexteditor)
	add_library(${UTENAME} STATIC ${UNITEXTEDITOR_SOURCES})
else()
	set(UTENAME unitexteditor.gadget)
	add_executable(${UTENAME}
		libinit.s
		${UNITEXTEDITOR_SOURCES})
	target_link_options(${UTENAME} PRIVATE -ffreestanding -nostartfiles -Wl,-e,__start)
	target_compile_options(${UTENAME} PRIVATE -fomit-frame-pointer )
endif()

# Headers in this directory are part of the public API
target_include_directories(${UTENAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
					${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include
						)

if(UNITEXTEDITOR_STATIC_LINK)
    target_compile_definitions(${UTENAME}
        # Gadget sources: know they are compiled statically (future #ifdef use)
        PRIVATE UNITEXTEDITOR_STATIC=1
        # Host application: propagated automatically via link, triggers
        # UniTextEditor_Init/Exit calls and direct class pointer access
        PUBLIC  USE_STATIC_UNITEXTEDITOR=1
    )
else()
endif()

# Inherit CPU/FPU flags chosen by the parent project (EmojiGear sets
# AMIGA_COMPILER_OPTIONS before calling add_subdirectory; CMake child
# scopes inherit parent variables).
if(AMIGA_COMPILER_OPTIONS)
    target_compile_options(${UTENAME} PRIVATE
        ${AMIGA_COMPILER_OPTIONS}
    )
endif()

if(UNITEXTEDTORDEBUG)
 target_compile_definitions(${UTENAME} PRIVATE USE_DEBUG_BDBPRINT=1)
endif()
