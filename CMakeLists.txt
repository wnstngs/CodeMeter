cmake_minimum_required(VERSION 3.26)
project(CodeMeter C)

option(BUILD_TESTS "Build the regression tests" ON)

#
# Build the MAIN executable
#
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

if(BUILD_TESTS)
    #
    # Build the TEST executable
    #
    add_executable(CodeMeterTest
            tests.c
            codemeter.c
            codemeter.h
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
endif()
