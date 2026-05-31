cmake_minimum_required(VERSION 3.10)

project(UniTests C)

set(CMAKE_C_STANDARD 11)

list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -mcpu=68020 -O2 )

add_executable(TestUtf8DrawText testutf8drawtext.c )
target_include_directories(TestUtf8DrawText PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_options(TestUtf8DrawText PRIVATE ${AMIGA_COMPILER_OPTIONS} )


add_executable(testgadget testgadget.c )
target_include_directories(testgadget PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_options(testgadget PRIVATE ${AMIGA_COMPILER_OPTIONS} )

# add_executable(testoffscreenbm
#     testoffscreenbm.c
#     ${CMAKE_CURRENT_SOURCE_DIR}/../../UniButtonGadget/offscreenbm.c
# )
# target_include_directories(testoffscreenbm PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${CMAKE_CURRENT_SOURCE_DIR}/../include
# )
# target_compile_options(testoffscreenbm PRIVATE ${AMIGA_COMPILER_OPTIONS} )

add_executable(testunibutton testunibutton.c)
target_include_directories(testunibutton PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_options(testunibutton PRIVATE ${AMIGA_COMPILER_OPTIONS} )


# MUI SDK path – adjust if your MUI headers are elsewhere
set(MUI_INCLUDE_DIR "/home/krabob/Code/Amiga/MUI-5.0-20210831-os3/SDK/MUI/C/include"
    CACHE PATH "Path to MUI 5.0 C include directory")

add_executable(testmuiuted testmuiuted.c)
target_include_directories(testmuiuted PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${MUI_INCLUDE_DIR}
)
target_compile_options(testmuiuted PRIVATE ${AMIGA_COMPILER_OPTIONS} )
