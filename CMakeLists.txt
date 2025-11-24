cmake_minimum_required(VERSION 3.26)
project(CodeMeter C)

add_executable(CodeMeter
        codemeter.c
)

target_link_libraries(CodeMeter PRIVATE Pathcch.lib)

if(MSVC)
    #
    # Common compile flags for all configurations
    #

    target_compile_options(CodeMeter PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:preprocessor
            /EHsc-
            /FC
            /diagnostics:column
    )

    #
    # Configuration-specific compile flags
    #

    target_compile_options(CodeMeter PRIVATE
            $<$<CONFIG:Debug>:
            /Od
            /Zi
            /RTC1
            /MDd
            /sdl
            /GS
            >

            $<$<CONFIG:RelWithDebInfo>:
            /O2
            /Zi
            /MD
            /GS
            /sdl
            /Gw
            /Oi
            >

            $<$<CONFIG:Release>:
            /O2
            /MD
            /GS
            /Gw
            /Oi
            /Ot
            >
    )

    #
    # Linker flags per configuration
    #
    target_link_options(CodeMeter PRIVATE
            $<$<CONFIG:Debug>:
            /DEBUG
            /INCREMENTAL
            >

            $<$<CONFIG:RelWithDebInfo>:
            /DEBUG
            /INCREMENTAL:NO
            /LTCG
            /OPT:REF
            /OPT:ICF
            >

            $<$<CONFIG:Release>:
            /INCREMENTAL:NO
            /LTCG
            /OPT:REF
            /OPT:ICF
            >
    )
endif()
