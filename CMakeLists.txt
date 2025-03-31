cmake_minimum_required(VERSION 3.26)
project(CodeMeter C)

add_executable(CodeMeter
        codemeter.c
)

target_link_libraries(CodeMeter PRIVATE Pathcch.lib)

if(MSVC)
    target_compile_options(CodeMeter PRIVATE /W4 /WX)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(CodeMeter PRIVATE /DEBUG /Z7 /INCREMENTAL)
    else()
        target_compile_options(CodeMeter PRIVATE /O2)
    endif()
endif()
