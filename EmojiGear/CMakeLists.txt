cmake_minimum_required(VERSION 3.10)

project(EmojiGear C)
set(CMAKE_C_STANDARD 11)



option(UTF8RASTPORT_SHARED "link utf8rastport as amiga shared lib"  ON)
option(UNITEXTEDITOR_SHARED "link utf8rastport as amiga shared lib"  ON)


# Assembler output for debug; picked up by gadget sub-projects via scope
list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -Wall)
list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -O1 )


set(EMOJIGEAR_SOURCES
    bdbprintf.c
    boopsimessage.c
    emojigear.c
    boopsimainwindow.c
    emojigearsettings.c
    borderscrollers.c
    egmenu.c
    egaction.c
    eglocale.c
    egsettingsview.c
    egfontsview.c
    tooltypepref.c
    emojibox.c
    egsearchbox.c
    egtabs.c
)

# ---- Sub-libraries -------------------------------------------------------
# Order matters: utf8rastport first, then unitexteditor (uses utf8rastport),
# then unibutton (uses unitexteditor).  AMIGA_COMPILER_OPTIONS is inherited
# by each subdirectory's scope so gadgets pick up the correct CPU flags.
#
# utf8rastport.c includes "bdbprintf.h" which lives in UniTextEditorGadget/.
# That subdirectory is not a cmake target yet when libutf8rastport is
# configured, so we add it as a global directory-scope include.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include)


add_subdirectory(../libutf8rastport    ${CMAKE_CURRENT_BINARY_DIR}/libutf8rastport)

add_subdirectory(../UniTextEditorGadget ${CMAKE_CURRENT_BINARY_DIR}/UniTextEditorGadget)
add_subdirectory(../UniButtonGadget    ${CMAKE_CURRENT_BINARY_DIR}/UniButtonGadget)

# ---- EmojiGear executable ------------------------------------------------

add_executable(EmojiGear ${EMOJIGEAR_SOURCES})

install(TARGETS EmojiGear ARCHIVE DESTINATION bin)

# USE_STATIC_UNITEXTEDITOR and USE_STATIC_UNIBUTTON propagate automatically
# from the gadget targets' PUBLIC compile definitions via link, so they do
# not need to be set here manually.

target_compile_options(EmojiGear PRIVATE
    ${AMIGA_COMPILER_OPTIONS}
)

# unitexteditor and unibutton bring utf8rastport transitively, or not is shared
if(NOT UNITEXTEDITOR_SHARED)
 target_link_libraries(EmojiGear PRIVATE unitexteditor unibutton)
endif()

# target_compile_definitions(EmojiGear PRIVATE USE_DEBUG_BDBPRINT=1)
