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

add_executable(testoffscreenbm
    testoffscreenbm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../UniButtonGadget/offscreenbm.c
)
target_include_directories(testoffscreenbm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_options(testoffscreenbm PRIVATE ${AMIGA_COMPILER_OPTIONS} )


