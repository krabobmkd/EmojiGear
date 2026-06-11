cmake_minimum_required(VERSION 3.10)
project(MUImojiGear C)
set(CMAKE_C_STANDARD 11)

# MUI 5.0 SDK include path – adjust if installed elsewhere
set(MUI_INCLUDE_DIR "/home/krabob/Code/Amiga/MUI-5.0-20210831-os3/SDK/MUI/C/include"
    CACHE PATH "Path to MUI 5.0 C include directory")

# Assembler output for debug; picked up by gadget sub-projects via scope
list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -Wall)
list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -Os )


# Source files.
# eglocale.c and tooltypepref.c are reused directly from EmojiGear.
set(MUIMOJIGEARSOURCES
    muimojiGear.c       muimojiGear.h
    mmgboopsimessage.c  mmgboopsimessage.h
    mmgsettings.c       mmgsettings.h
    mmgaction.c         mmgaction.h
    mmgemojibox.c       mmgemojibox.h
    mmgfontsview.c      mmgfontsview.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../EmojiGear/eglocale.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../EmojiGear/tooltypepref.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../EmojiGear/egpipeinput.c
)

add_executable(MUImojiGear ${MUIMOJIGEARSOURCES})

target_include_directories(MUImojiGear PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # mmgboopsimessage.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../EmojiGear  # eglocale.h, tooltypepref.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include # unitexteditor.h, unibutton.h …
    ${MUI_INCLUDE_DIR}                    # libraries/mui.h
)

# Inherit AMIGA_COMPILER_OPTIONS (-mcpu=68020 -O1 …) from the parent scope
# when built as a subdirectory of EmojiGear/CMakeLists.txt.
target_compile_options(MUImojiGear PRIVATE ${AMIGA_COMPILER_OPTIONS})

# Link against the static BOOPSI class libraries (built by the parent project).
# unitexteditor brings utf8rastport and libc stubs transitively.
if(NOT UNITEXTEDITOR_SHARED)
    target_link_libraries(MUImojiGear PRIVATE unitexteditor)
endif()
