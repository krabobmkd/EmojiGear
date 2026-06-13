cmake_minimum_required(VERSION 3.10)

project(FriendSheeep C)
set(CMAKE_C_STANDARD 11)

# Assembler output for debug; picked up by the network subdirectory via scope.
list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -Wall)
list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -Os)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include)

# ---- Network process static library --------------------------------------
# Standalone, no GUI includes; runs in its own CreateNewProc() task.
add_subdirectory(network)

# ---- GUI sources -----------------------------------------------------------
# BOOPSI window / gadgets / rendering land here in later phases (see
# ARCHITECTURE.md). For now this is just main() plus the network IPC link.

set(FRIENDSHEEEP_SOURCES
    friendsheep.c
)

add_executable(FriendSheeep ${FRIENDSHEEEP_SOURCES})

target_link_libraries(FriendSheeep PRIVATE fsnet)

target_compile_options(FriendSheeep PRIVATE
    ${AMIGA_COMPILER_OPTIONS}
)

install(TARGETS FriendSheeep ARCHIVE DESTINATION bin)
