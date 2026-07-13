cmake_minimum_required(VERSION 3.10)

project(FriendSh3ep C)
set(CMAKE_C_STANDARD 11)

# Assembler output for debug; picked up by the network subdirectory via scope.
list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -Wall)
list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -O2)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include)

# ---- Network process static library --------------------------------------
# Standalone, no GUI includes; runs in its own CreateNewProc() task.
add_subdirectory(network_fs3e)

# ---- GUI sources -----------------------------------------------------------
# BOOPSI window.class + layout.gadget GUI, structured after
# EmojiGear/emojigear.c (see ARCHITECTURE.md). More gadgets/rendering land in
# later phases.

set(FRIENDSH3EP_SOURCES
	bdbprintf.c
    avatarimages.c
    bmimage.c
    fs3etextwrap.c
    fs3ethumb.c
    rgbscale.c
    rgbimage.c
    scalepixelarraybilinear.c
    clipboard.c
    patch9.c
    stylefile.c
    friendsh3ep.c
    fs3estyle.c
    fs3eboopsimainwindow.c
    fs3eboopsimessage.c
    fs3eloginview.c
    fs3etootview.c
    fs3eemojibox.c
    fs3ethemeview.c
    fs3esettingsview.c
    fs3emediaview.c
    fs3elocale.c
    fs3emenu.c
    fs3eaction.c
    fs3esettings.c
    fs3emachineid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../EmojiGear/tooltypepref.c
    UniButtonP9/unibuttonp9.c
    UniButtonP9/unibuttonp9_dispatch.c
    UniButtonP9/unibuttonp9_attribs.c
    UniButtonP9/unibuttonp9_render.c
    UniButtonP9/unibuttonp9_input.c
    UniButtonBGBM/unibuttonbgbm.c
    UniButtonBGBM/unibuttonbgbm_dispatch.c
    UniButtonBGBM/unibuttonbgbm_attribs.c
    UniButtonBGBM/unibuttonbgbm_render.c
    UniButtonBGBM/unibuttonbgbm_input.c
    UniButtonBGBM/offscreenbm.c
    TitleBarLayout/fs3etitlebar.c
    NavBarLayout/fs3enavbar.c
    TootTimeline/fs3etoottimeline.c
    TootTimeline/fs3etoottimeline_attribs.c
    TootTimeline/fs3etoottimeline_posts.c
    TootTimeline/fs3etoottimeline_tiles.c
    TootTimeline/fs3etoottimeline_render.c
    TootTimeline/fs3etoottimeline_input.c
    TootTimeline/fs3etoottimeline_profile.c
    TootTimeline/fs3etoottimeline_notif.c
)

# Listed so IDEs (Qt Creator, etc.) show them in the project tree.
set(FRIENDSH3EP_HEADERS
    bmimage.h
    fs3etextwrap.h
    clipboard.h
    patch9.h
    stylefile.h
    friendsh3ep.h
    fs3estyle.h
    fs3eboopsimainwindow.h
    fs3eboopsimessage.h
    fs3eloginview.h
    fs3etootview.h
    fs3eemojibox.h
    fs3ethemeview.h
    fs3esettingsview.h
    fs3emediaview.h
    fs3elocale.h
    fs3emenu.h
    fs3eaction.h
    fs3esettings.h
    fs3emachineid.h
    fs3egadgetid.h
    compilers.h
    bdbprintf.h
    UniButtonP9/unibuttonp9.h
    UniButtonBGBM/unibuttonbgbm.h
    TitleBarLayout/fs3etitlebar.h
    NavBarLayout/fs3enavbar.h
    TootTimeline/fs3etoottimeline.h
    TootTimeline/fs3etoottimeline_private.h
)

add_executable(FriendSh3ep ${FRIENDSH3EP_SOURCES} ${FRIENDSH3EP_HEADERS})

target_link_libraries(FriendSh3ep PRIVATE fs3enet)

target_compile_options(FriendSh3ep PRIVATE
    ${AMIGA_COMPILER_OPTIONS}
)

install(TARGETS FriendSh3ep ARCHIVE DESTINATION bin)


target_compile_definitions(FriendSh3ep PRIVATE USE_DEBUG_BDBPRINT=1)
