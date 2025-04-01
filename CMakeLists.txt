cmake_minimum_required(VERSION 3.26)
project(CodeMeter C)

#
# Build the MAIN executable
#
add_executable(CodeMeter
        codemeter.c
        tests.c
        codemeter.h
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

#
# Build the TEST executable
#
add_executable(CodeMeterTest
        tests.c
        codemeter.c
)
target_link_libraries(CodeMeterTest PRIVATE Pathcch.lib)

if(MSVC)
    target_compile_options(CodeMeterTest PRIVATE /W4 /WX)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(CodeMeterTest PRIVATE /DEBUG /Z7 /INCREMENTAL)
    else()
        target_compile_options(CodeMeterTest PRIVATE /O2)
    endif()
endif()
