cmake_minimum_required(VERSION 3.10)

project(FriendSh3ep C)
set(CMAKE_C_STANDARD 11)

# Assembler output for debug; picked up by the network subdirectory via scope.
list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -Wall)
list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -Os)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include)

# ---- Network process static library --------------------------------------
# Standalone, no GUI includes; runs in its own CreateNewProc() task.
add_subdirectory(network_fs3e)

# ---- GUI sources -----------------------------------------------------------
# BOOPSI window.class + layout.gadget GUI, structured after
# EmojiGear/emojigear.c (see ARCHITECTURE.md). More gadgets/rendering land in
# later phases.

set(FRIENDSH3EP_SOURCES
    friendsh3ep.c
    fs3eboopsimainwindow.c
    fs3eboopsimessage.c
    fs3eloginview.c
    fs3etootview.c
    fs3elocale.c
)

# Listed so IDEs (Qt Creator, etc.) show them in the project tree.
set(FRIENDSH3EP_HEADERS
    friendsh3ep.h
    fs3eboopsimainwindow.h
    fs3eboopsimessage.h
    fs3eloginview.h
    fs3etootview.h
    fs3elocale.h
    fs3egadgetid.h
    compilers.h
    bdbprintf.h
)

add_executable(FriendSh3ep ${FRIENDSH3EP_SOURCES} ${FRIENDSH3EP_HEADERS})

target_link_libraries(FriendSh3ep PRIVATE fs3enet)

target_compile_options(FriendSh3ep PRIVATE
    ${AMIGA_COMPILER_OPTIONS}
)

install(TARGETS FriendSh3ep ARCHIVE DESTINATION bin)
