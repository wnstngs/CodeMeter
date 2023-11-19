/*++

Copyright (c) 2023  Glebs. All rights reserved.

Module Name:

    codemeter.c
    
Abstract:

    This module implements CodeMeter, a program for counting lines of code.

    Further, the term "revision" is mentioned repeatedly. It is understood to mean
    the whole process: from scanning files and counting them to counting lines of code.
                          ┌─────────────────┐
                e.g. path │                 │ returns
    Init params ─────────►│    Revision     ├─────────► Statistics
                          │                 │
                          └─────────────────┘
    
Author:

    Glebs Oct-2023

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

//
// ------------------------------------------------------ Data Type Definitions
//

/**
 * @brief This structure stores the initialization parameters of the revision
 * provided by the user at launch.
 */
typedef struct REVISION_INIT_PARAMS {
    /**
     * @brief Path to the revision root directory.
     */
    _Field_z_ PWCHAR RootDirectory;

    /**
     * @brief Indicates whether verbose revision mode is active.
     */
    BOOL IsVerboseMode;
} REVISION_INIT_PARAMS, *PREVISION_INIT_PARAMS;

/**
 * @brief This enumeration defines the types of file extensions (to be used in
 * REVISION_RECORD_EXTENSION_MAPPING)
 */
typedef enum _REVISION_RECORD_EXTENSION_TYPE {
    /**
     * @brief File extension with a single dot, e.g. ".txt".
     */
    SingleDot,

    /**
     * @brief File extension with multiple dots, e.g. ".CMakeLists.txt".
     */
    MultiDot
} REVISION_RECORD_EXTENSION_TYPE;

/**
 * @brief This structure stores the mapping of file extensions to programming languages.
 */
typedef struct REVISION_RECORD_EXTENSION_MAPPING {
    /**
     * @brief File extension.
     */
    _Field_z_ PWCHAR Extension;

    /**
     * @brief Programming language or file type.
     */
    _Field_z_ PWCHAR LanguageOrFileType;

    /**
     * @brief Extension type which indicates whether the extension is a single-dot
     * or multi-dot extension.
     */
    REVISION_RECORD_EXTENSION_TYPE ExtensionType;
} REVISION_RECORD_EXTENSION_MAPPING, *PREVISION_RECORD_EXTENSION_MAPPING;

/**
 * @brief This structure stores statistics for some specific file extension.
 */
typedef struct REVISION_RECORD {
    /**
     * @brief Linked list entry.
     */
    LIST_ENTRY ListEntry;

    /**
     * @brief Extension of the revision record file and recognized programming language/file
     * type based on extension.
     */
    REVISION_RECORD_EXTENSION_MAPPING ExtensionMapping;

    /**
     * @brief Number of lines in the revision record.
     */
    ULONGLONG CountOfLinesTotal;

    /**
     * @brief Number of blank lines in the revision record.
     */
    ULONGLONG CountOfLinesBlank;

    /**
     * @brief Number of files in the revision record.
     */
    ULONG CountOfFiles;
} REVISION_RECORD, *PREVISION_RECORD;

/**
 * @brief This structure stores the statistics of the entire revision.
 */
typedef struct REVISION {
    /**
     * @brief Revision initialization parameters provided by the user.
     */
    REVISION_INIT_PARAMS InitParams;

    /**
     * @brief List of revision records.
     */
    LIST_ENTRY RevisionRecordListHead;

    /**
     * @brief Number of lines in the whole project.
     */
    ULONGLONG CountOfLinesTotal;

    /**
     * @brief Number of blank lines in the whole project.
     */
    ULONGLONG CountOfLinesBlank;

    /**
     * @brief Number of files in the whole project.
     */
    ULONG CountOfFiles;

    /**
     * @brief Number of ignored files during the revision.
     */
    ULONG CountOfIgnoredFiles;
} REVISION, *PREVISION;

/**
 * @brief This enumeration represents different console text colors.
 */
typedef enum _CONSOLE_FOREGROUND_COLOR {
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan
} CONSOLE_FOREGROUND_COLOR;

//
// ------------------------------------------------------ Constants and Globals
//

/**
 * @brief The string to be prepended to a path to avoid the MAX_PATH limitation.
 */
#define MAX_PATH_FIX    L"\\\\?\\"

/**
 * @brief The string to be appended to a path to indicate all of its contents.
 */
#define ASTERISK        L"\\*"

const WCHAR WelcomeString[] =
    L"CodeMeter v0.0.1                 Copyright(c) 2023 Glebs\n"
    "--------------------------------------------------------\n\n";

const WCHAR UsageString[] =
    L"DESCRIPTION:\n\n"
    "\tIn order to count the number of lines of CodeMeter code, you need\n"
    "\tthe path to the root directory of the project you want to revise.\n"
    "\tThe path should be passed as the first argument of the command line:\n\n\t"
    "CodeMeter.exe \"C:\\\\MyProject\"\n\n"
    "OPTIONS:\n\n"
    "\t-help, -h, -?\n"
    "\tPrint a help message and exit.\n\n"
    "\t-v\n"
    "\tEnable verbose logging mode.\n\n";

/**
 * @brief This array holds ANSI escape sequences for changing text color
 * in the console for each corresponding CONSOLE_FOREGROUND_COLOR.
 * @note The order must be the same as in the enumeration CONSOLE_FOREGROUND_COLOR.
 */
const PWCHAR ConsoleForegroundColors[] = {
    /* Red */
    L"\x1b[31m",

    /* Green */
    L"\x1b[32m",

    /* Yellow */
    L"\x1b[33m",

    /* Blue */
    L"\x1b[34m",

    /* Magenta */
    L"\x1b[35m",

    /* Cyan */
    L"\x1b[36m"
};

/**
 * @brief Mapping of file extensions that can be recognized to human-readable descriptions of file types.
 *
 * TODO: Multi-dot extensions are commented out, we need to add support for them.
         The current algorithm counts everything after the last dot as an extension.
 */
REVISION_RECORD_EXTENSION_MAPPING ExtensionMappingTable[] = {
    {L".abap",               L"ABAP"},
    {L".ac",                 L"m4"},
    {L".ada",                L"Ada"},
    {L".adb",                L"Ada"},
    {L".ads",                L"Ada"},
    {L".adso",               L"ADSO/IDSM"},
    {L".ahkl",               L"AutoHotkey"},
    {L".ahk",                L"AutoHotkey"},
    {L".agda",               L"Agda"},
    {L".lagda",              L"Agda"},
    {L".aj",                 L"AspectJ"},
    {L".am",                 L"make"},
    {L".ample",              L"AMPLE"},
    {L".apl",                L"APL"},
    {L".apla",               L"APL"},
    {L".aplf",               L"APL"},
    {L".aplo",               L"APL"},
    {L".apln",               L"APL"},
    {L".aplc",               L"APL"},
    {L".apli",               L"APL"},
    {L".dyalog",             L"APL"},
    {L".dyapp",              L"APL"},
    {L".mipage",             L"APL"},
    {L".as",                 L"ActionScript"},
    {L".adoc",               L"AsciiDoc"},
    {L".asciidoc",           L"AsciiDoc"},
    {L".dofile",             L"AMPLE"},
    {L".startup",            L"AMPLE"},
    {L".axd",                L"ASP"},
    {L".ashx",               L"ASP"},
    {L".asa",                L"ASP"},
    {L".asax",               L"ASP.NET"},
    {L".ascx",               L"ASP.NET"},
    {L".asd",                L"Lisp"},
    {L".asmx",               L"ASP.NET"},
    {L".asp",                L"ASP"},
    {L".aspx",               L"ASP.NET"},
    {L".master",             L"ASP.NET"},
    {L".sitemap",            L"ASP.NET"},
    {L".nasm",               L"Assembly"},
    {L".a51",                L"Assembly"},
    {L".asm",                L"Assembly"},
    {L".astro",              L"Astro"},
    {L".asy",                L"Asymptote"},
    {L".cshtml",             L"Razor"},
    {L".razor",              L"Razor"},
    {L".nawk",               L"awk"},
    {L".mawk",               L"awk"},
    {L".gawk",               L"awk"},
    {L".auk",                L"awk"},
    {L".awk",                L"awk"},
    {L".bash",               L"Bourne Again Shell"},
    {L".bazel",              L"Starlark"},
    {L".BUILD",              L"Bazel"},
    {L".dxl",                L"DOORS Extension Language"},
    {L".bat",                L"DOS Batch"},
    {L".BAT",                L"DOS Batch"},
    {L".cmd",                L"DOS Batch"},
    {L".CMD",                L"DOS Batch"},
    {L".btm",                L"DOS Batch"},
    {L".BTM",                L"DOS Batch"},
    {L".blade",              L"Blade"},
//    {L".blade.php",          L"Blade"},
//    {L".build.xml",          L"Ant"},
    {L".b",                  L"Brainfuck"},
    {L".bf",                 L"Brainfuck"},
    {L".brs",                L"BrightScript"},
    {L".bzl",                L"Starlark"},
    {L".btp",                L"BizTalk Pipeline"},
    {L".odx",                L"BizTalk Orchestration"},
    {L".carbon",             L"Carbon"},
    {L".cpy",                L"COBOL"},
    {L".cobol",              L"COBOL"},
    {L".ccp",                L"COBOL"},
    {L".cbl",                L"COBOL"},
    {L".CBL",                L"COBOL"},
    {L".idc",                L"C"},
    {L".cats",               L"C"},
    {L".c",                  L"C"},
    {L".c++",                L"C++"},
    {L".C",                  L"C++"},
    {L".cc",                 L"C++"},
    {L".ccm",                L"C++"},
    {L".c++m",               L"C++"},
    {L".cppm",               L"C++"},
    {L".cxxm",               L"C++"},
    {L".h++",                L"C++"},
    {L".inl",                L"C++"},
    {L".ipp",                L"C++"},
    {L".ixx",                L"C++"},
    {L".tcc",                L"C++"},
    {L".tpp",                L"C++"},
    {L".ccs",                L"CCS"},
    {L".cfc",                L"ColdFusion CFScript"},
    {L".cfml",               L"ColdFusion"},
    {L".cfm",                L"ColdFusion"},
    {L".chpl",               L"Chapel"},
    {L".cl",                 L"Lisp/OpenCL"},
//    {L".riemann.config",     L"Clojure"},
    {L".hic",                L"Clojure"},
    {L".cljx",               L"Clojure"},
    {L".cljscm",             L"Clojure"},
//    {L".cljs.hl",            L"Clojure"},
    {L".cl2",                L"Clojure"},
    {L".boot",               L"Clojure"},
    {L".clj",                L"Clojure"},
    {L".cljs",               L"ClojureScript"},
    {L".cljc",               L"ClojureC"},
    {L".cls",                L"Visual Basic/TeX/Apex Class"},
//    {L".cmake.in",           L"CMake"},
//    {L".CMakeLists.txt",     L"CMake"},
    {L".cmake",              L"CMake"},
    {L".cob",                L"COBOL"},
    {L".COB",                L"COBOL"},
    {L".cocoa5",             L"CoCoA 5"},
    {L".c5",                 L"CoCoA 5"},
    {L".cpkg5",              L"CoCoA 5"},
    {L".cocoa5server",       L"CoCoA 5"},
    {L".iced",               L"CoffeeScript"},
    {L".cjsx",               L"CoffeeScript"},
    {L".cakefile",           L"CoffeeScript"},
    {L"._coffee",            L"CoffeeScript"},
    {L".coffee",             L"CoffeeScript"},
    {L".component",          L"Visualforce Component"},
    {L".cg3",                L"Constraint Grammar"},
    {L".rlx",                L"Constraint Grammar"},
    {L".Containerfile",      L"Containerfile"},
    {L".cpp",                L"C++"},
    {L".CPP",                L"C++"},
    {L".cr",                 L"Crystal"},
    {L".cs",                 L"C#/Smalltalk"},
//    {L".designer.cs",        L"C# Designer"},
    {L".cake",               L"Cake Build Script"},
    {L".csh",                L"C Shell"},
    {L".cson",               L"CSON"},
    {L".css",                L"CSS"},
    {L".csv",                L"CSV"},
    {L".cu",                 L"CUDA"},
    {L".cuh",                L"CUDA"},
    {L".cxx",                L"C++"},
    {L".d",                  L"D/dtrace"},
    {L".da",                 L"DAL"},
    {L".dart",               L"Dart"},
    {L".dsc",                L"DenizenScript"},
    {L".derw",               L"Derw"},
    {L".def",                L"Windows Module Definition"},
    {L".dhall",              L"dhall"},
    {L".dt",                 L"DIET"},
    {L".patch",              L"diff"},
    {L".diff",               L"diff"},
    {L".dmap",               L"NASTRAN DMAP"},
    {L".sthlp",              L"Stata"},
    {L".matah",              L"Stata"},
    {L".mata",               L"Stata"},
    {L".ihlp",               L"Stata"},
    {L".doh",                L"Stata"},
    {L".ado",                L"Stata"},
    {L".do",                 L"Stata"},
    {L".DO",                 L"Stata"},
    {L".Dockerfile",         L"Dockerfile"},
    {L".dockerfile",         L"Dockerfile"},
    {L".pascal",             L"Pascal"},
    {L".lpr",                L"Pascal"},
    {L".dfm",                L"Delphi Form"},
    {L".dpr",                L"Pascal"},
    {L".dita",               L"DITA"},
    {L".drl",                L"Drools"},
    {L".dtd",                L"DTD"},
    {L".ec",                 L"C"},
    {L".ecpp",               L"ECPP"},
    {L".eex",                L"EEx"},
    {L".el",                 L"Lisp"},
    {L".elm",                L"Elm"},
    {L".exs",                L"Elixir"},
    {L".ex",                 L"Elixir"},
    {L".ecr",                L"Embedded Crystal"},
    {L".ejs",                L"EJS"},
    {L".erb",                L"ERB"},
    {L".ERB",                L"ERB"},
    {L".yrl",                L"Erlang"},
    {L".xrl",                L"Erlang"},
//    {L".rebar.lock",         L"Erlang"},
//    {L".rebar.config.lock",  L"Erlang"},
//    {L".rebar.config",       L"Erlang"},
    {L".emakefile",          L"Erlang"},
//    {L".app.src",            L"Erlang"},
    {L".erl",                L"Erlang"},
    {L".exp",                L"Expect"},
    {L".4th",                L"Forth"},
    {L".fish",               L"Fish Shell"},
    {L".fsl",                L"Finite State Language"},
    {L".jssm",               L"Finite State Language"},
    {L".fnl",                L"Fennel"},
    {L".forth",              L"Forth"},
    {L".fr",                 L"Forth"},
    {L".frt",                L"Forth"},
    {L".fth",                L"Forth"},
    {L".f83",                L"Forth"},
    {L".fb",                 L"Forth"},
    {L".fpm",                L"Forth"},
    {L".e4",                 L"Forth"},
    {L".rx",                 L"Forth"},
    {L".ft",                 L"Forth"},
    {L".f77",                L"Fortran 77"},
    {L".F77",                L"Fortran 77"},
    {L".f90",                L"Fortran 90"},
    {L".F90",                L"Fortran 90"},
    {L".f95",                L"Fortran 95"},
    {L".F95",                L"Fortran 95"},
    {L".f",                  L"Fortran 77/Forth"},
    {L".F",                  L"Fortran 77"},
    {L".for",                L"Fortran 77/Forth"},
    {L".FOR",                L"Fortran 77"},
    {L".ftl",                L"Freemarker Template"},
    {L".ftn",                L"Fortran 77"},
    {L".FTN",                L"Fortran 77"},
    {L".fmt",                L"Oracle Forms"},
    {L".focexec",            L"Focus"},
    {L".fs",                 L"F#/Forth"},
    {L".fsi",                L"F#"},
    {L".fsx",                L"F# Script"},
    {L".fut",                L"Futhark"},
    {L".fxml",               L"FXML"},
    {L".gnumakefile",        L"make"},
    {L".Gnumakefile",        L"make"},
    {L".gd",                 L"GDScript"},
    {L".gdshader",           L"Godot Shaders"},
    {L".vshader",            L"GLSL"},
    {L".vsh",                L"GLSL"},
    {L".vrx",                L"GLSL"},
    {L".gshader",            L"GLSL"},
    {L".glslv",              L"GLSL"},
    {L".geo",                L"GLSL"},
    {L".fshader",            L"GLSL"},
    {L".fsh",                L"GLSL"},
    {L".frg",                L"GLSL"},
    {L".fp",                 L"GLSL"},
    {L".fbs",                L"Flatbuffers"},
    {L".glsl",               L"GLSL"},
    {L".graphqls",           L"GraphQL"},
    {L".gql",                L"GraphQL"},
    {L".graphql",            L"GraphQL"},
    {L".vert",               L"GLSL"},
    {L".tesc",               L"GLSL"},
    {L".tese",               L"GLSL"},
    {L".geom",               L"GLSL"},
    {L".feature",            L"Cucumber"},
    {L".frag",               L"GLSL"},
    {L".comp",               L"GLSL"},
    {L".g",                  L"ANTLR Grammar"},
    {L".g4",                 L"ANTLR Grammar"},
    {L".gleam",              L"Gleam"},
    {L".go",                 L"Go"},
    {L".ʕ◔ϖ◔ʔ",              L"Go"},
    {L".gsp",                L"Grails"},
    {L".jenkinsfile",        L"Groovy"},
    {L".gvy",                L"Groovy"},
    {L".gtpl",               L"Groovy"},
    {L".grt",                L"Groovy"},
    {L".groovy",             L"Groovy"},
    {L".gant",               L"Groovy"},
    {L".gradle",             L"Gradle"},
//    {L".gradle.kts",         L"Gradle"},
    {L".h",                  L"C/C++ Header"},
    {L".H",                  L"C/C++ Header"},
    {L".hh",                 L"C/C++ Header"},
    {L".hpp",                L"C/C++ Header"},
    {L".hxx",                L"C/C++ Header"},
    {L".hb",                 L"Harbour"},
    {L".hrl",                L"Erlang"},
    {L".hsc",                L"Haskell"},
    {L".hs",                 L"Haskell"},
    {L".tfvars",             L"HCL"},
    {L".hcl",                L"HCL"},
    {L".tf",                 L"HCL"},
    {L".nomad",              L"HCL"},
    {L".hlsli",              L"HLSL"},
    {L".fxh",                L"HLSL"},
    {L".hlsl",               L"HLSL"},
    {L".shader",             L"HLSL"},
    {L".cg",                 L"HLSL"},
    {L".cginc",              L"HLSL"},
//    {L".haml.deface",        L"Haml"},
    {L".haml",               L"Haml"},
    {L".handlebars",         L"Handlebars"},
    {L".hbs",                L"Handlebars"},
    {L".ha",                 L"Hare"},
    {L".hxsl",               L"Haxe"},
    {L".hx",                 L"Haxe"},
    {L".HC",                 L"HolyC"},
    {L".hoon",               L"Hoon"},
    {L".xht",                L"HTML"},
//    {L".html.hl",            L"HTML"},
    {L".htm",                L"HTML"},
    {L".html",               L"HTML"},
    {L".heex",               L"HTML EEx"},
    {L".i3",                 L"Modula3"},
    {L".ice",                L"Slice"},
    {L".icl",                L"Clean"},
    {L".dcl",                L"Clean"},
    {L".dlm",                L"IDL"},
    {L".idl",                L"IDL"},
    {L".idr",                L"Idris"},
    {L".lidr",               L"Literate Idris"},
    {L".imba",               L"Imba"},
    {L".prefs",              L"INI"},
    {L".lektorproject",      L"INI"},
//    {L".buildozer.spec",     L"INI"},
    {L".ini",                L"INI"},
    {L".editorconfig",       L"INI"},
    {L".ism",                L"InstallShield"},
    {L".ipl",                L"IPL"},
    {L".pro",                L"IDL/Qt Project/Prolog/ProGuard"},
    {L".ig",                 L"Modula3"},
    {L".il",                 L"SKILL"},
    {L".ils",                L"SKILL++"},
    {L".inc",                L"PHP/Pascal/Fortran"},
    {L".ino",                L"Arduino Sketch"},
    {L".ipf",                L"Igor Pro"},
    {L".pde",                L"Arduino Sketch"}, // pre 1.0
    {L".itk",                L"Tcl/Tk"},
    {L".java",               L"Java"},
    {L".jcl",                L"JCL"}, // IBM Job Control Lang.
    {L".jl",                 L"Lisp/Julia"},
    {L".jai",                L"Jai"},
    {L".xsjslib",            L"JavaScript"},
    {L".xsjs",               L"JavaScript"},
    {L".ssjs",               L"JavaScript"},
    {L".sjs",                L"JavaScript"},
    {L".pac",                L"JavaScript"},
    {L".njs",                L"JavaScript"},
    {L".mjs",                L"JavaScript"},
    {L".cjs",                L"JavaScript"},
    {L".jss",                L"JavaScript"},
    {L".jsm",                L"JavaScript"},
    {L".jsfl",               L"JavaScript"},
    {L".jscad",              L"JavaScript"},
    {L".jsb",                L"JavaScript"},
    {L".jakefile",           L"JavaScript"},
    {L".jake",               L"JavaScript"},
    {L".bones",              L"JavaScript"},
    {L"._js",                L"JavaScript"},
    {L".js",                 L"JavaScript"},
    {L".es6",                L"JavaScript"},
    {L".jsf",                L"JavaServer Faces"},
    {L".jsx",                L"JSX"},
    {L".xhtml",              L"XHTML"},
    {L".jinja",              L"Jinja Template"},
    {L".jinja2",             L"Jinja Template"},
    {L".yyp",                L"JSON"},
    {L".webmanifest",        L"JSON"},
    {L".webapp",             L"JSON"},
    {L".topojson",           L"JSON"},
//    {L".tfstate.backup",     L"JSON"},
    {L".tfstate",            L"JSON"},
//    {L".mcmod.info",         L"JSON"},
    {L".mcmeta",             L"JSON"},
    {L".json-tmlanguage",    L"JSON"},
    {L".jsonl",              L"JSON"},
    {L".har",                L"JSON"},
    {L".gltf",               L"JSON"},
    {L".geojson",            L"JSON"},
//    {L".composer.lock",      L"JSON"},
    {L".avsc",               L"JSON"},
    {L".watchmanconfig",     L"JSON"},
    {L".tern-project",       L"JSON"},
    {L".tern-config",        L"JSON"},
    {L".htmlhintrc",         L"JSON"},
    {L".arcconfig",          L"JSON"},
    {L".json",               L"JSON"},
    {L".json5",              L"JSON5"},
    {L".jsp",                L"JSP"},
    {L".jspf",               L"JSP"},
    {L".junos",              L"Juniper Junos"},
    {L".vm",                 L"Velocity Template Language"},
    {L".kv",                 L"kvlang"},
    {L".ksc",                L"Kermit"},
    {L".ksh",                L"Korn Shell"},
    {L".ktm",                L"Kotlin"},
    {L".kt",                 L"Kotlin"},
    {L".kts",                L"Kotlin"},
    {L".hlean",              L"Lean"},
    {L".lean",               L"Lean"},
    {L".lhs",                L"Haskell"},
    {L".lex",                L"lex"},
    {L".l",                  L"lex"},
    {L".ld",                 L"Linker Script"},
    {L".lem",                L"Lem"},
    {L".less",               L"LESS"},
    {L".lfe",                L"LFE"},
    {L".liquid",             L"liquid"},
    {L".lsp",                L"Lisp"},
    {L".lisp",               L"Lisp"},
    {L".ll",                 L"LLVM IR"},
    {L".lgt",                L"Logtalk"},
    {L".logtalk",            L"Logtalk"},
    {L".wlua",               L"Lua"},
    {L".rbxs",               L"Lua"},
    {L".pd_lua",             L"Lua"},
    {L".p8",                 L"Lua"},
    {L".nse",                L"Lua"},
    {L".lua",                L"Lua"},
    {L".m3",                 L"Modula3"},
    {L".m4",                 L"m4"},
    {L".makefile",           L"make"},
    {L".Makefile",           L"make"},
    {L".mao",                L"Mako"},
    {L".mako",               L"Mako"},
    {L".workbook",           L"Markdown"},
    {L".ronn",               L"Markdown"},
    {L".mkdown",             L"Markdown"},
    {L".mkdn",               L"Markdown"},
    {L".mkd",                L"Markdown"},
    {L".mdx",                L"Markdown"},
    {L".mdwn",               L"Markdown"},
    {L".mdown",              L"Markdown"},
    {L".markdown",           L"Markdown"},
//    {L".contents.lr",        L"Markdown"},
    {L".md",                 L"Markdown"},
    {L".mc",                 L"Windows Message File"},
    {L".met",                L"Teamcenter met"},
    {L".mg",                 L"Modula3"},
    {L".mojom",              L"Mojo"},
//    {L".meson.build",        L"Meson"},
    {L".metal",              L"Metal"},
    {L".mk",                 L"make"},
    {L".ml4",                L"OCaml"},
    {L".eliomi",             L"OCaml"},
    {L".eliom",              L"OCaml"},
    {L".ml",                 L"OCaml"},
    {L".mli",                L"OCaml"},
    {L".mly",                L"OCaml"},
    {L".mll",                L"OCaml"},
    {L".m",                  L"MATLAB/Objective-C"},
    {L".mm",                 L"Objective-C++"},
    {L".msg",                L"Gencat NLS"},
    {L".nbp",                L"Mathematica"},
    {L".mathematica",        L"Mathematica"},
    {L".ma",                 L"Mathematica"},
    {L".cdf",                L"Mathematica"},
    {L".mt",                 L"Mathematica"},
    {L".wl",                 L"Mathematica"},
    {L".wlt",                L"Mathematica"},
    {L".mustache",           L"Mustache"},
    {L".wdproj",             L"MSBuild script"},
    {L".csproj",             L"MSBuild script"},
    {L".vcproj",             L"MSBuild script"},
    {L".wixproj",            L"MSBuild script"},
    {L".btproj",             L"MSBuild script"},
    {L".msbuild",            L"MSBuild script"},
    {L".sln",                L"Visual Studio Solution"},
    {L".mps",                L"MUMPS"},
    {L".mth",                L"Teamcenter mth"},
    {L".n",                  L"Nemerle"},
    {L".nlogo",              L"NetLogo"},
    {L".nls",                L"NetLogo"},
    {L".nims",               L"Nim"},
    {L".nimrod",             L"Nim"},
    {L".nimble",             L"Nim"},
//    {L".nim.cfg",            L"Nim"},
    {L".nim",                L"Nim"},
    {L".nix",                L"Nix"},
    {L".nut",                L"Squirrel"},
    {L".njk",                L"Nunjucks"},
    {L".odin",               L"Odin"},
    {L".oscript",            L"LiveLink OScript"},
    {L".bod",                L"Oracle PL/SQL"},
    {L".spc",                L"Oracle PL/SQL"},
    {L".fnc",                L"Oracle PL/SQL"},
    {L".prc",                L"Oracle PL/SQL"},
    {L".trg",                L"Oracle PL/SQL"},
    {L".pad",                L"Ada"},
    {L".page",               L"Visualforce Page"},
    {L".pas",                L"Pascal"},
    {L".pcc",                L"C++"},
    {L".rexfile",            L"Perl"},
    {L".psgi",               L"Perl"},
    {L".ph",                 L"Perl"},
//    {L".makefile.pl",        L"Perl"},
    {L".cpanfile",           L"Perl"},
    {L".al",                 L"Perl"},
    {L".ack",                L"Perl"},
    {L".perl",               L"Perl"},
    {L".pfo",                L"Fortran 77"},
    {L".pgc",                L"C"},
    {L".phpt",               L"PHP"},
    {L".phps",               L"PHP"},
    {L".phakefile",          L"PHP"},
    {L".ctp",                L"PHP"},
    {L".aw",                 L"PHP"},
//    {L".php_cs.dist",        L"PHP"},
    {L".php_cs",             L"PHP"},
    {L".php3",               L"PHP"},
    {L".php4",               L"PHP"},
    {L".php5",               L"PHP"},
    {L".php",                L"PHP"},
    {L".phtml",              L"PHP"},
    {L".pig",                L"Pig Latin"},
    {L".plh",                L"Perl"},
    {L".pl",                 L"Perl/Prolog"},
    {L".PL",                 L"Perl/Prolog"},
    {L".p6",                 L"Raku/Prolog"},
    {L".P6",                 L"Raku/Prolog"},
    {L".plx",                L"Perl"},
    {L".pm",                 L"Perl"},
    {L".pm6",                L"Raku"},
    {L".raku",               L"Raku"},
    {L".rakumod",            L"Raku"},
//    {L".pom.xml",            L"Maven"},
    {L".pom",                L"Maven"},
    {L".scad",               L"OpenSCAD"},
    {L".yap",                L"Prolog"},
    {L".prolog",             L"Prolog"},
    {L".P",                  L"Prolog"},
    {L".p",                  L"Pascal"},
    {L".pp",                 L"Pascal/Puppet"},
    {L".viw",                L"SQL"},
    {L".udf",                L"SQL"},
    {L".tab",                L"SQL"},
    {L".mysql",              L"SQL"},
    {L".cql",                L"SQL"},
    {L".psql",               L"SQL"},
    {L".xpy",                L"Python"},
    {L".wsgi",               L"Python"},
    {L".wscript",            L"Python"},
    {L".workspace",          L"Python"},
    {L".tac",                L"Python"},
    {L".snakefile",          L"Python"},
    {L".sconstruct",         L"Python"},
    {L".sconscript",         L"Python"},
    {L".pyt",                L"Python"},
    {L".pyp",                L"Python"},
    {L".pyi",                L"Python"},
    {L".pyde",               L"Python"},
    {L".py3",                L"Python"},
    {L".lmi",                L"Python"},
    {L".gypi",               L"Python"},
    {L".gyp",                L"Python"},
//    {L".build.bazel",        L"Python"},
    {L".buck",               L"Python"},
    {L".gclient",            L"Python"},
    {L".py",                 L"Python"},
    {L".pyw",                L"Python"},
    {L".ipynb",              L"Jupyter Notebook"},
    {L".pyj",                L"RapydScript"},
    {L".pxi",                L"Cython"},
    {L".pxd",                L"Cython"},
    {L".pyx",                L"Cython"},
    {L".qbs",                L"QML"},
    {L".qml",                L"QML"},
    {L".watchr",             L"Ruby"},
    {L".vagrantfile",        L"Ruby"},
    {L".thorfile",           L"Ruby"},
    {L".thor",               L"Ruby"},
    {L".snapfile",           L"Ruby"},
    {L".ru",                 L"Ruby"},
    {L".rbx",                L"Ruby"},
    {L".rbw",                L"Ruby"},
    {L".rbuild",             L"Ruby"},
    {L".rabl",               L"Ruby"},
    {L".puppetfile",         L"Ruby"},
    {L".podfile",            L"Ruby"},
    {L".mspec",              L"Ruby"},
    {L".mavenfile",          L"Ruby"},
    {L".jbuilder",           L"Ruby"},
    {L".jarfile",            L"Ruby"},
    {L".guardfile",          L"Ruby"},
    {L".god",                L"Ruby"},
    {L".gemspec",            L"Ruby"},
//    {L".gemfile.lock",       L"Ruby"},
    {L".gemfile",            L"Ruby"},
    {L".fastfile",           L"Ruby"},
    {L".eye",                L"Ruby"},
    {L".deliverfile",        L"Ruby"},
    {L".dangerfile",         L"Ruby"},
    {L".capfile",            L"Ruby"},
    {L".buildfile",          L"Ruby"},
    {L".builder",            L"Ruby"},
    {L".brewfile",           L"Ruby"},
    {L".berksfile",          L"Ruby"},
    {L".appraisals",         L"Ruby"},
    {L".pryrc",              L"Ruby"},
    {L".irbrc",              L"Ruby"},
    {L".rb",                 L"Ruby"},
    {L".podspec",            L"Ruby"},
    {L".rake",               L"Ruby"},
    {L".rex",                L"Oracle Reports"},
    {L".pprx",               L"Rexx"},
    {L".rexx",               L"Rexx"},
    {L".rhtml",              L"Ruby HTML"},
    {L".circom",             L"Circom"},
    {L".cairo",              L"Cairo"},
//    {L".rs.in",              L"Rust"},
    {L".rs",                 L"Rust"},
//    {L".rst.txt",            L"reStructuredText"},
//    {L".rest.txt",           L"reStructuredText"},
    {L".rest",               L"reStructuredText"},
    {L".rst",                L"reStructuredText"},
    {L".s",                  L"Assembly"},
    {L".S",                  L"Assembly"},
    {L".SCA",                L"Visual Fox Pro"},
    {L".sca",                L"Visual Fox Pro"},
    {L".sbt",                L"Scala"},
    {L".kojo",               L"Scala"},
    {L".scala",              L"Scala"},
    {L".sbl",                L"Softbridge Basic"},
    {L".SBL",                L"Softbridge Basic"},
    {L".sed",                L"sed"},
    {L".sp",                 L"SparForte"},
    {L".sol",                L"Solidity"},
    {L".p4",                 L"P4"},
    {L".ses",                L"Patran Command Language"},
    {L".pcl",                L"Patran Command Language"},
    {L".peg",                L"PEG"},
    {L".pegjs",              L"peg.js"},
    {L".peggy",              L"peggy"},
    {L".pest",               L"Pest"},
    {L".prisma",             L"Prisma Schema"},
    {L".tspeg",              L"tspeg"},
    {L".jspeg",              L"tspeg"},
    {L".pl1",                L"PL/I"},
    {L".plm",                L"PL/M"},
    {L".lit",                L"PL/M"},
    {L".iuml",               L"PlantUML"},
    {L".pu",                 L"PlantUML"},
    {L".puml",               L"PlantUML"},
    {L".plantuml",           L"PlantUML"},
    {L".wsd",                L"PlantUML"},
    {L".properties",         L"Properties"},
    {L".po",                 L"PO File"},
    {L".pony",               L"Pony"},
    {L".pbt",                L"PowerBuilder"},
    {L".sra",                L"PowerBuilder"},
    {L".srf",                L"PowerBuilder"},
    {L".srm",                L"PowerBuilder"},
    {L".srs",                L"PowerBuilder"},
    {L".sru",                L"PowerBuilder"},
    {L".srw",                L"PowerBuilder"},
    {L".jade",               L"Pug"},
    {L".pug",                L"Pug"},
    {L".purs",               L"PureScript"},
    {L".prefab",             L"Unity-Prefab"},
    {L".proto",              L"Protocol Buffers"},
    {L".mat",                L"Unity-Prefab"},
    {L".ps1",                L"PowerShell"},
    {L".psd1",               L"PowerShell"},
    {L".psm1",               L"PowerShell"},
    {L".prql",               L"PRQL"},
    {L".rsx",                L"R"},
    {L".rd",                 L"R"},
    {L".expr-dist",          L"R"},
    {L".rprofile",           L"R"},
    {L".R",                  L"R"},
    {L".r",                  L"R"},
    {L".raml",               L"RAML"},
    {L".ring",               L"Ring"},
    {L".rh",                 L"Ring"},
    {L".rform",              L"Ring"},
    {L".rktd",               L"Racket"},
    {L".rkt",                L"Racket"},
    {L".rktl",               L"Racket"},
    {L".Rmd",                L"Rmd"},
    {L".re",                 L"ReasonML"},
    {L".rei",                L"ReasonML"},
    {L".res",                L"ReScript"},
    {L".resi",               L"ReScript"},
    {L".scrbl",              L"Racket"},
    {L".sps",                L"Scheme"},
    {L".sc",                 L"Scheme"},
    {L".ss",                 L"Scheme"},
    {L".scm",                L"Scheme"},
    {L".sch",                L"Scheme"},
    {L".sls",                L"Scheme/SaltStack"},
    {L".sld",                L"Scheme"},
    {L".robot",              L"RobotFramework"},
    {L".rc",                 L"Windows Resource File"},
    {L".rc2",                L"Windows Resource File"},
    {L".sas",                L"SAS"},
    {L".sass",               L"Sass"},
    {L".scss",               L"SCSS"},
    {L".sh",                 L"Bourne Shell"},
    {L".smarty",             L"Smarty"},
    {L".sml",                L"Standard ML"},
    {L".sig",                L"Standard ML"},
    {L".fun",                L"Standard ML"},
    {L".slim",               L"Slim"},
    {L".e",                  L"Specman e"},
    {L".sql",                L"SQL"},
    {L".SQL",                L"SQL"},
//    {L".sproc.sql",          L"SQL Stored Procedure"},
//    {L".spoc.sql",           L"SQL Stored Procedure"},
//    {L".spc.sql",            L"SQL Stored Procedure"},
//    {L".udf.sql",            L"SQL Stored Procedure"},
//    {L".data.sql",           L"SQL Data"},
    {L".sss",                L"SugarSS"},
    {L".st",                 L"Smalltalk"},
    {L".rules",              L"Snakemake"},
    {L".smk",                L"Snakemake"},
    {L".styl",               L"Stylus"},
    {L".i",                  L"SWIG"},
    {L".svelte",             L"Svelte"},
    {L".sv",                 L"Verilog-SystemVerilog"},
    {L".svh",                L"Verilog-SystemVerilog"},
    {L".svg",                L"SVG"},
    {L".SVG",                L"SVG"},
    {L".v",                  L"Verilog-SystemVerilog"},
    {L".td",                 L"TableGen"},
    {L".tcl",                L"Tcl/Tk"},
    {L".tcsh",               L"C Shell"},
    {L".tk",                 L"Tcl/Tk"},
    {L".teal",               L"TEAL"},
    {L".mkvi",               L"TeX"},
    {L".mkiv",               L"TeX"},
    {L".mkii",               L"TeX"},
    {L".ltx",                L"TeX"},
    {L".lbx",                L"TeX"},
    {L".ins",                L"TeX"},
    {L".cbx",                L"TeX"},
    {L".bib",                L"TeX"},
    {L".bbx",                L"TeX"},
    {L".aux",                L"TeX"},
    {L".tex",                L"TeX"},
    {L".toml",               L"TOML"},
    {L".sty",                L"TeX"},
    {L".dtx",                L"TeX"},
    {L".bst",                L"TeX"},
    {L".txt",                L"Text"},
    {L".text",               L"Text"},
    {L".tres",               L"Godot Resource"},
    {L".tres",               L"Godot Resource"},
    {L".tscn",               L"Godot Scene"},
    {L".thrift",             L"Thrift"},
    {L".tla",                L"TLA+"},
    {L".tpl",                L"Smarty"},
    {L".trigger",            L"Apex Trigger"},
    {L".ttcn",               L"TTCN"},
    {L".ttcn2",              L"TTCN"},
    {L".ttcn3",              L"TTCN"},
    {L".ttcnpp",             L"TTCN"},
    {L".sdl",                L"TNSDL"},
    {L".ssc",                L"TNSDL"},
    {L".sdt",                L"TNSDL"},
    {L".spd",                L"TNSDL"},
    {L".sst",                L"TNSDL"},
    {L".rou",                L"TNSDL"},
    {L".cin",                L"TNSDL"},
    {L".cii",                L"TNSDL"},
    {L".interface",          L"TNSDL"},
    {L".in1",                L"TNSDL"},
    {L".in2",                L"TNSDL"},
    {L".in3",                L"TNSDL"},
    {L".in4",                L"TNSDL"},
    {L".inf",                L"TNSDL"},
    {L".tpd",                L"TITAN Project Descriptor"},
    {L".ts",                 L"TypeScript/Qt Linguist"},
    {L".mts",                L"TypeScript"},
    {L".tsx",                L"TypeScript"},
    {L".tss",                L"Titanium Style Sheet"},
    {L".twig",               L"Twig"},
    {L".typ",                L"Typst"},
    {L".um",                 L"Umka"},
    {L".ui",                 L"Qt/Glade"},
    {L".glade",              L"Glade"},
    {L".vala",               L"Vala"},
    {L".vapi",               L"Vala Header"},
    {L".vhw",                L"VHDL"},
    {L".vht",                L"VHDL"},
    {L".vhs",                L"VHDL"},
    {L".vho",                L"VHDL"},
    {L".vhi",                L"VHDL"},
    {L".vhf",                L"VHDL"},
    {L".vhd",                L"VHDL"},
    {L".VHD",                L"VHDL"},
    {L".vhdl",               L"VHDL"},
    {L".VHDL",               L"VHDL"},
    {L".bas",                L"Visual Basic"},
    {L".BAS",                L"Visual Basic"},
    {L".ctl",                L"Visual Basic"},
    {L".dsr",                L"Visual Basic"},
    {L".frm",                L"Visual Basic"},
    {L".frx",                L"Visual Basic"},
    {L".FRX",                L"Visual Basic"},
    {L".vba",                L"VB for Applications"},
    {L".VBA",                L"VB for Applications"},
    {L".vbhtml",             L"Visual Basic"},
    {L".VBHTML",             L"Visual Basic"},
    {L".vbproj",             L"Visual Basic .NET"},
    {L".vbp",                L"Visual Basic"},
    {L".vbs",                L"Visual Basic Script"},
    {L".VBS",                L"Visual Basic Script"},
    {L".vb",                 L"Visual Basic .NET"},
    {L".VB",                 L"Visual Basic .NET"},
    {L".vbw",                L"Visual Basic"},
    {L".vue",                L"Vuejs Component"},
    {L".vy",                 L"Vyper"},
    {L".webinfo",            L"ASP.NET"},
    {L".wsdl",               L"Web Services Description"},
    {L".x",                  L"Logos"},
    {L".xm",                 L"Logos"},
    {L".xpo",                L"X++"},
    {L".xmi",                L"XMI"},
    {L".XMI",                L"XMI"},
    {L".zcml",               L"XML"},
    {L".xul",                L"XML"},
    {L".xspec",              L"XML"},
    {L".xproj",              L"XML"},
    {L".xml.dist",           L"XML"},
    {L".xliff",              L"XML"},
    {L".xlf",                L"XML"},
    {L".xib",                L"XML"},
    {L".xacro",              L"XML"},
    {L".x3d",                L"XML"},
    {L".wsf",                L"XML"},
//    {L".web.release.config", L"XML"},
//    {L".web.debug.config",   L"XML"},
    {L".web.config",         L"XML"},
    {L".wxml",               L"WXML"},
    {L".wxss",               L"WXSS"},
    {L".vxml",               L"XML"},
    {L".vstemplate",         L"XML"},
    {L".vssettings",         L"XML"},
    {L".vsixmanifest",       L"XML"},
    {L".vcxproj",            L"XML"},
    {L".ux",                 L"XML"},
    {L".urdf",               L"XML"},
    {L".tmtheme",            L"XML"},
    {L".tmsnippet",          L"XML"},
    {L".tmpreferences",      L"XML"},
    {L".tmlanguage",         L"XML"},
    {L".tml",                L"XML"},
    {L".tmcommand",          L"XML"},
    {L".targets",            L"XML"},
    {L".sublime-snippet",    L"XML"},
    {L".sttheme",            L"XML"},
    {L".storyboard",         L"XML"},
    {L".srdf",               L"XML"},
    {L".shproj",             L"XML"},
    {L".sfproj",             L"XML"},
//    {L".settings.stylecop",  L"XML"},
    {L".scxml",              L"XML"},
    {L".rss",                L"XML"},
    {L".resx",               L"XML"},
    {L".rdf",                L"XML"},
    {L".pt",                 L"XML"},
    {L".psc1",               L"XML"},
    {L".ps1xml",             L"XML"},
    {L".props",              L"XML"},
    {L".proj",               L"XML"},
    {L".plist",              L"XML"},
    {L".pkgproj",            L"XML"},
//    {L".packages.config",    L"XML"},
    {L".osm",                L"XML"},
    {L".odd",                L"XML"},
    {L".nuspec",             L"XML"},
//    {L".nuget.config",       L"XML"},
    {L".nproj",              L"XML"},
    {L".ndproj",             L"XML"},
    {L".natvis",             L"XML"},
    {L".mjml",               L"XML"},
    {L".mdpolicy",           L"XML"},
    {L".launch",             L"XML"},
    {L".kml",                L"XML"},
    {L".jsproj",             L"XML"},
    {L".jelly",              L"XML"},
    {L".ivy",                L"XML"},
    {L".iml",                L"XML"},
    {L".grxml",              L"XML"},
    {L".gmx",                L"XML"},
    {L".fsproj",             L"XML"},
    {L".filters",            L"XML"},
    {L".dotsettings",        L"XML"},
//    {L".dll.config",         L"XML"},
    {L".ditaval",            L"XML"},
    {L".ditamap",            L"XML"},
    {L".depproj",            L"XML"},
    {L".ct",                 L"XML"},
    {L".csl",                L"XML"},
    {L".csdef",              L"XML"},
    {L".cscfg",              L"XML"},
    {L".cproject",           L"XML"},
    {L".clixml",             L"XML"},
    {L".ccxml",              L"XML"},
    {L".ccproj",             L"XML"},
    {L".builds",             L"XML"},
    {L".axml",               L"XML"},
//    {L".app.config",         L"XML"},
    {L".ant",                L"XML"},
    {L".admx",               L"XML"},
    {L".adml",               L"XML"},
    {L".project",            L"XML"},
    {L".classpath",          L"XML"},
    {L".xml",                L"XML"},
    {L".XML",                L"XML"},
    {L".mxml",               L"MXML"},
//    {L".xml.builder",        L"builder"},
    {L".build",              L"NAnt script"},
    {L".vim",                L"vim script"},
    {L".swift",              L"Swift"},
    {L".xaml",               L"XAML"},
    {L".wast",               L"WebAssembly"},
    {L".wat",                L"WebAssembly"},
    {L".wgsl",               L"WGSL"},
    {L".wxs",                L"WiX source"},
    {L".wxi",                L"WiX include"},
    {L".wxl",                L"WiX string localization"},
    {L".prw",                L"xBase"},
    {L".prg",                L"xBase"},
    {L".ch",                 L"xBase Header"},
    {L".xqy",                L"XQuery"},
    {L".xqm",                L"XQuery"},
    {L".xql",                L"XQuery"},
    {L".xq",                 L"XQuery"},
    {L".xquery",             L"XQuery"},
    {L".xsd",                L"XSD"},
    {L".XSD",                L"XSD"},
    {L".xslt",               L"XSLT"},
    {L".XSLT",               L"XSLT"},
    {L".xsl",                L"XSLT"},
    {L".XSL",                L"XSLT"},
    {L".xtend",              L"Xtend"},
    {L".yacc",               L"yacc"},
    {L".y",                  L"yacc"},
//    {L".yml.mysql",          L"YAML"},
    {L".yaml-tmlanguage",    L"YAML"},
    {L".syntax",             L"YAML"},
    {L".sublime-syntax",     L"YAML"},
    {L".rviz",               L"YAML"},
    {L".reek",               L"YAML"},
    {L".mir",                L"YAML"},
//    {L".glide.lock",         L"YAML"},
    {L".gemrc",              L"YAML"},
    {L".clang-tidy",         L"YAML"},
    {L".clang-format",       L"YAML"},
    {L".yaml",               L"YAML"},
    {L".yml",                L"YAML"},
    {L".zig",                L"Zig"},
    {L".zsh",                L"zsh"},
};

/**
 * @brief The global revision state used throughout the entire program run-time.
 */
PREVISION Revision = NULL;

/**
 * @brief Indicates whether ANSI escape sequences are supported.
 */
BOOL SupportAnsi;

//
// -------------------------------------------------------- Function Prototypes
//

/**
 * @brief This function retrieves the calling thread's last-error code value
 * and translates it into its corresponding error message.
 * @return A pointer to the error message string on success, or NULL on failure.
 */
_Ret_maybenull_
PWCHAR
RevGetLastKnownWin32Error(
    VOID
    );

/**
 * @brief This function appends one unicode string to another and returns the result.
 * @param String1 The first string (to which String2 will be appended).
 * @param String2 The second string (to be appended to String1).
 * @return A new string containing the concatenation of String1 and String2.
 *         NULL if the function failed.
 *         N.B. The caller is responsible for freeing the memory.
 */
_Ret_maybenull_
_Must_inspect_result_
PWCHAR
RevStringAppend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    );

/**
 * @brief This function prepends one unicode string to another and returns the result.
 * @param String1 Supplies the first string (to be appended to).
 * @param String2 Supplies the second string (to be prepended).
 * @return A new string containing the concatenation of String1 and String2.
 *         NULL if the function failed.
 *         N.B. The caller is responsible for freeing the memory.
 */
_Ret_maybenull_
_Must_inspect_result_
PWCHAR
RevStringPrepend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    );

/**
 * @brief This function is responsible for initializing the revision system.
 * @param InitParams Supplies the revision initialization parameters.
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevInitializeRevision(
    _In_ PREVISION_INIT_PARAMS InitParams
    );

/**
 * @brief This function is responsible for starting the revision system. It
 * ensures that the system has been initialized correctly before proceeding
 * with its operations.
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevStartRevision(
    VOID
    );

/**
 * @brief This function initializes a REVISION_RECORD structure.
 * @param Extension Supplies the file extension of the revision record.
 * @param LanguageOrFileType Supplies the language or file type of the revision record.
 * @return If the initialization is successful, returns a pointer to the new revision record; otherwise, NULL.
 */
_Must_inspect_result_
PREVISION_RECORD
RevInitializeRevisionRecord(
    _In_z_ PWCHAR Extension,
    _In_z_ PWCHAR LanguageOrFileType
    );

/**
 * @brief This function is designed to recursively traverse and enumerate files
 * and subdirectories within a given root directory path.
 * @param RootDirectoryPath Supplies the root directory path from which enumeration
 * should begin.
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevEnumerateRecursively(
    _In_z_ PWCHAR RootDirectoryPath
    );

/**
 * This function searches a table of file extension-to-language mappings to find the
 * programming language associated with the provided file extension.
 * @param Extension Supplies the file extension.
 * @return If a matching file extension is found in the mapping table, the function returns
 * the associated programming language as a string. If no match is found, the function returns NULL.
 */
PWCHAR
RevMapExtensionToLanguage(
    _In_z_ PWCHAR Extension
    );

/**
 * @brief This function checks if a REVISION_RECORD for a language/file type with a given
 * extension exists in the global revision's list of revision records.
 * @param Extension Supplies the file extension to search for.
 * @return If a matching REVISION_RECORD is found, returns a pointer to that record;
 * otherwise, returns NULL.
 */
_Must_inspect_result_
PREVISION_RECORD
RevFindRevisionRecordForLanguageByExtension(
    _In_z_ PWCHAR Extension
    );

/**
 * @brief This function checks if a file extension is in the extension table. File should
 * be revised only if it has valid (is in the table) extension.
 * @param FileName Supplies the name of the file to be checked.
 * @return TRUE if succeeded, FALSE if failed.
 */
BOOL
RevShouldReviseFile(
    _In_z_ PWCHAR FileName
    );

/**
 * @brief This function reads and revises the specified file.
 * @param FilePath Supplies the path to the file to be revised.
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevReviseFile(
    _In_z_ PWCHAR FilePath
    );

/**
 * @brief This function outputs the revision statistics to the console.
 */
VOID
RevOutputRevisionStatistics(
    VOID
    );

/**
 * @brief This function initializes a LIST_ENTRY structure that represents
 * the head of a doubly linked list.
 * @param ListHead Supplies a pointer to a LIST_ENTRY that represents the head of the list.
 */
FORCEINLINE
VOID
RevInitializeListHead(
    _Inout_ PLIST_ENTRY ListHead
    )
{
    ListHead->Flink = ListHead->Blink = ListHead;
}

/**
 * @brief This function checks whether a LIST_ENTRY is empty.
 * @param ListHead Supplies a pointer to a LIST_ENTRY that represents the head of the list.
 * @return TRUE if there are currently no entries in the list and FALSE otherwise.
 */
FORCEINLINE
BOOL
RevIsListEmpty(
    _In_ PLIST_ENTRY ListHead
    )
{
    return ListHead->Flink == ListHead;
}

/**
 * @brief This function inserts an entry at the tail of a list.
 * @param ListHead Supplies a pointer to a LIST_ENTRY that represents the head of the list.
 * @param Entry Supplies a pointer to a LIST_ENTRY that represents the entry to be inserted.
 */
FORCEINLINE
VOID
RevInsertTailList(
    _In_ PLIST_ENTRY ListHead,
    _In_ PLIST_ENTRY Entry
    )
{
    PLIST_ENTRY Blink;

    Blink = ListHead->Blink;
    Entry->Flink = ListHead;
    Entry->Blink = Blink;
    Blink->Flink = Entry;
    ListHead->Blink = Entry;
}

/**
 * @brief This function prints a formatted string in the specified color.
 * @param Color Supplies the text foreground color.
 * @param Format Supplies the format specifier.
 * @param ... Supplies additional parameters to be formatted and printed.
 */
FORCEINLINE
VOID
RevPrintEx(
    CONSOLE_FOREGROUND_COLOR Color,
    const wchar_t *Format,
    ...
)
{
    va_list args;
    const PWCHAR color = ConsoleForegroundColors[Color];

    if (SupportAnsi) {
        wprintf(color);
    }
    va_start(args, Format);
    vwprintf(Format, args);
    va_end(args);
    if (SupportAnsi) {
        wprintf(L"\033[0m");
    }
}

/**
 * @brief This function prints a formatted string in [default] green color.
 * @param Format Supplies the format specifier.
 * @param ... Supplies additional parameters to be formatted and printed.
 */
#define RevPrint(Format, ...)                           \
    do {                                                \
        RevPrintEx(Green, Format, __VA_ARGS__);         \
    } while (0)

/**
 * @brief This function outputs a red text error message to the standard error stream.
 * @param Message Supplies the error message.
 * @note This function respects the verbose mode setting from the global revision structure.
 * The logging is conditioned on whether verbose mode is enabled.
 */
#define RevLogError(Message, ...)                                                       \
    do {                                                                                \
        if (Revision && Revision->InitParams.IsVerboseMode) {                           \
            fprintf(stderr,                                                             \
                    SupportAnsi ?                                                       \
                        "\033[0;31m[ERROR]\n└───> (in %s@%d): " Message "\033[0m\n" :   \
                        "[ERROR]\n└───> (in %s@%d): " Message "\n",                     \
                        __FUNCTION__,                                                   \
                        __LINE__,                                                       \
                        ##__VA_ARGS__);                                                 \
        }                                                                               \
    } while (0)

/**
 * @brief This function outputs a yellow text warning message to the standard output stream.
 * @param Message Supplies the warning message.
 * @note This function respects the verbose mode setting from the global revision structure.
 * The logging is conditioned on whether verbose mode is enabled.
 */
#define RevLogWarning(Message, ...)                                                     \
    do {                                                                                \
        if (Revision && Revision->InitParams.IsVerboseMode) {                           \
            fprintf(stdout,                                                             \
                    SupportAnsi ?                                                       \
                        "\033[0;33m[WARNING]\n└───> (in %s@%d): " Message "\033[0m\n" : \
                        "[WARNING]\n└───> (in %s@%d): " Message "\n",                   \
                        __FUNCTION__,                                                   \
                        __LINE__,                                                       \
                        ##__VA_ARGS__);                                                 \
        }                                                                               \
    } while (0)

//
// ------------------------------------------------------------------ Functions
//

_Ret_maybenull_
PWCHAR
RevGetLastKnownWin32Error(
    VOID
    )
{
    PWCHAR messageBuffer;
    SIZE_T messageBufferSize;
    DWORD formatResult;
    const DWORD lastKnownError = GetLastError();

    /*
     * Attempt to format the error code into a human-readable string.
     */
    formatResult = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                                  NULL,
                                  lastKnownError,
                                  0,
                                  (LPWSTR) &messageBuffer,
                                  0,
                                  NULL);

    if (formatResult == 0) {
        /*
         * If FormatMessageW failed, convert the error code to a string
         * and return that.
         */

        /* N.B. The maximum error code value is a 5-digit number (15999). */
        messageBufferSize = (5 + 1) * sizeof(WCHAR);
        messageBuffer = (PWCHAR) malloc(messageBufferSize);
        if (messageBuffer == NULL) {
            RevLogError("Failed to allocate a message buffer (%d bytes).",
                        messageBufferSize);
            goto Exit;
        }

        swprintf_s(messageBuffer,
                   (5 + 1),
                   L"%lu",
                   lastKnownError);
    }

Exit:
    return messageBuffer;
}

_Ret_maybenull_
PWCHAR
RevStringAppend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    )
{
    SIZE_T string1Length, string2Length, resultStringLength;
    PWCHAR result;

    /*
     * Find the length of the result string.
     */
    string1Length = wcslen(String1);
    string2Length = wcslen(String2);
    resultStringLength = string1Length + string2Length + 1;

    /*
     * Allocate buffer for the new string.
     */
    result = (PWCHAR) malloc(resultStringLength * sizeof(WCHAR));
    if (result == NULL) {
        RevLogError("Failed to allocate string buffer (%d bytes).",
                    resultStringLength * sizeof(WCHAR));
        goto Exit;
    }

    /*
     * Copy the first string to the result.
     */
    if (wcscpy_s(result, resultStringLength, String1) != 0) {
        free(result);
        result = NULL;
        goto Exit;
    }

    /*
     * Concatenate the second string to the result.
     */
    if (wcscat_s(result, resultStringLength, String2) != 0) {
        free(result);
        result = NULL;
        goto Exit;
    }

Exit:
    return result;
}

_Ret_maybenull_
PWCHAR
RevStringPrepend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    )
{
    SIZE_T string1Length, string2Length, resultStringLength;
    PWCHAR result;

    if (String1 == NULL || String2 == NULL) {
        RevLogError("Invalid parameter/-s.");
        result = NULL;
        goto Exit;
    }

    /*
     * Find the length of the result string.
     */
    string1Length = wcslen(String1);
    string2Length = wcslen(String2);
    resultStringLength = string1Length + string2Length + 1;

    /*
     * Allocate buffer for the new string.
     */
    result = (PWCHAR) malloc(resultStringLength * sizeof(WCHAR));
    if (result == NULL) {
        RevLogError("Failed to allocate string buffer (%d bytes).",
                    resultStringLength * sizeof(WCHAR));
        goto Exit;
    }

    /*
     * Copy the second string to the beginning of the result.
     */
    if (wcscpy_s(result, resultStringLength, String2) != 0) {
        free(result);
        result = NULL;
        goto Exit;
    }

    /*
     * Concatenate the first string to the result.
     */
    if (wcscat_s(result, resultStringLength, String1) != 0) {
        free(result);
        result = NULL;
        goto Exit;
    }

Exit:
    return result;
}

BOOL
RevInitializeRevision(
    _In_ PREVISION_INIT_PARAMS InitParams
    )
{
    BOOL status = TRUE;

    if (Revision != NULL) {
        RevLogError("The revision is already initialized.");
        status = FALSE;
        goto Exit;
    }

    if (InitParams == NULL ||
        InitParams->RootDirectory == NULL) {

        RevLogError("Invalid parameter/-s.");
        status = FALSE;
        goto Exit;
    }

    /*
     * Initialize the revision structure.
     */

    Revision = (PREVISION)malloc(sizeof(REVISION));
    if (Revision == NULL) {
        RevLogError("Failed to allocate memory for the global revision structure (%d bytes).",
                    sizeof(REVISION));
        status = FALSE;
        goto Exit;
    }

    RtlZeroMemory(Revision, sizeof(REVISION));

    /*
     * Initialize the list of revision records and other fields.
     */
    RevInitializeListHead(&Revision->RevisionRecordListHead);
    Revision->InitParams = *InitParams;
    Revision->CountOfLinesTotal = 0;
    Revision->CountOfLinesBlank = 0;
    Revision->CountOfFiles = 0;

Exit:
    return status;
}

BOOL
RevStartRevision(
    VOID
    )
{
    BOOL status = TRUE;

    if (Revision == NULL || Revision->InitParams.RootDirectory == NULL) {
        RevLogError("The revision is not initialized/initialized correctly.");
        status = FALSE;
        goto Exit;
    }

    RevEnumerateRecursively(Revision->InitParams.RootDirectory);

Exit:
    return status;
}

PREVISION_RECORD
RevInitializeRevisionRecord(
    _In_z_ PWCHAR Extension,
    _In_z_ PWCHAR LanguageOrFileType
    )
{
    PREVISION_RECORD revisionRecord;

    revisionRecord = (PREVISION_RECORD)malloc(sizeof(REVISION_RECORD));
    if (revisionRecord == NULL) {
        RevLogError("Failed to allocate memory for the revision record (%d bytes).",
                    sizeof(REVISION_RECORD));
        return NULL;
    }

    revisionRecord->ExtensionMapping.Extension = Extension;
    revisionRecord->ExtensionMapping.LanguageOrFileType = LanguageOrFileType;
    revisionRecord->CountOfLinesTotal = 0;
    revisionRecord->CountOfLinesBlank = 0;
    revisionRecord->CountOfFiles = 0;
    RevInitializeListHead(&revisionRecord->ListEntry);

    return revisionRecord;
}

PWCHAR
RevMapExtensionToLanguage(
    _In_z_ PWCHAR Extension
    )
{
    LONG i = 0;

    if (Extension == NULL) {
        RevLogError("Extension is NULL.");
        return NULL;
    }

    do {
        if (wcscmp(Extension, ExtensionMappingTable[i].Extension) == 0) {
            return ExtensionMappingTable[i].LanguageOrFileType;
        }
        ++i;
    } while (i < ARRAYSIZE(ExtensionMappingTable));

    return NULL;
}

_Must_inspect_result_
PREVISION_RECORD
RevFindRevisionRecordForLanguageByExtension(
    _In_z_ PWCHAR Extension
    )
{
    PLIST_ENTRY entry;
    PWCHAR languageOrFileType;
    PREVISION_RECORD revisionRecord;

    if (Extension == NULL) {
        RevLogError("Extension is NULL.");
        return NULL;
    }

    /*
     * Map the provided file extension to a language or file type.
     */
    languageOrFileType = RevMapExtensionToLanguage(Extension);
    if (languageOrFileType == NULL) {
        RevLogError("No langauge/file type match was found for the extension \"%ls\".",
                    Extension);
        return NULL;
    }

    assert(Revision != NULL);

    entry = Revision->RevisionRecordListHead.Flink;

    while (entry != &Revision->RevisionRecordListHead) {
        revisionRecord = CONTAINING_RECORD(entry, REVISION_RECORD, ListEntry);

        /*
         * Check if there is a match.
         */
        if (wcscmp(revisionRecord->ExtensionMapping.LanguageOrFileType, languageOrFileType) == 0) {
            return revisionRecord;
        }

        entry = entry->Flink;
    }

    /*
     * If no matching language or file type was found for the provided extension,
     * initialize a new revision record.
     */
    revisionRecord = RevInitializeRevisionRecord(Extension,
                                                 languageOrFileType);
    if (revisionRecord == NULL) {
        RevLogError("Failed to initialize a revision record (\"%ls\", \"%ls\").",
                    Extension,
                    languageOrFileType);
        return NULL;
    }

    /*
     * Add the new revision record to the global list of revision records.
     */
    RevInsertTailList(&Revision->RevisionRecordListHead, &revisionRecord->ListEntry);

    return revisionRecord;
}

_Must_inspect_result_
BOOL
RevEnumerateRecursively(
    _In_z_ PWCHAR RootDirectoryPath
    )
{
    BOOL status = TRUE;
    HANDLE findFile = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW findFileData;
    PWCHAR subPath = NULL;
    PWCHAR searchPath = NULL;

    /*
     * Check validity of passed arguments.
     */
    if (RootDirectoryPath == NULL) {
        RevLogError("Invalid parameter/-s.");
        status = FALSE;
        goto Exit;
    }

    /*
     * Each directory path should indicate that we are examining all files.
     * Check if the passed RootDirectoryPath already includes the wildcard (an asterisk).
     */
    if (wcsstr(RootDirectoryPath, ASTERISK) == NULL) {
        /*
         * If no, append a wildcard character to the root path.
         */
        searchPath = RevStringAppend(RootDirectoryPath,
                                     ASTERISK);
        if (searchPath == NULL) {
            RevLogError("Failed to normalize the revision subdirectory path "
                        "(RevStringAppend failed).");
            status = FALSE;
            goto Exit;
        }
    }

    /*
     * Try to find a file or subdirectory with a name that matches the pattern.
     */
    findFile = FindFirstFileW(searchPath,
                              &findFileData);

    /*
     * Free after RevStringAppend.
     */
    free(searchPath);

    /*
     * Check if FindFirstFileW failed.
     */
    if (findFile == INVALID_HANDLE_VALUE) {
        RevLogError("Failed to find a file named \"%ls\" to start the enumeration. "
                    "The last known error: %ls",
                    RootDirectoryPath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    do {
        if (wcscmp(findFileData.cFileName, L".") == 0 ||
            wcscmp(findFileData.cFileName, L"..") == 0) {

            /*
             * We want to skip one dot (for the current location) and
             * two dots (for the parent directory).
             */
            continue;
        }

        /*
         * To construct a subpath, append the "\" to the RootDirectoryPath.
         */
        subPath = RevStringAppend(RootDirectoryPath,
                                           L"\\");
        if (subPath == NULL) {
            RevLogError("Failed to normalize the revision subdirectory path "
                        "(RevStringAppend failed).");
            status = FALSE;
            goto Exit;
        }

        /*
         * Then append the subdirectory name that need to be traversed next.
         * N.B. The wildcard character (an asterisk) is not needed to be added as
         * it is done before calling FindFirstFileW.
         */
        subPath = RevStringAppend(subPath,
                                  findFileData.cFileName);
        if (subPath == NULL) {
            RevLogError("Failed to normalize the revision subdirectory path "
                "(RevStringAppend failed).");
            status = FALSE;
            goto Exit;
        }

        /*
         * Check if found a subdirectory.
         */
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

            /*
             * Recursively traverse a subdirectory.
             */
            RevEnumerateRecursively(subPath);
        } else {

            /*
             * If found a file, check if the file should be revised, and if so, revise it.
             *
             * The revision should be performed only if the file extension has been recognized.
             * For this purpose it is enough to pass only the file name (findFileData.cFileName),
             * but for file revision the full path (subdirectoryPath) is required.
             */
            if (RevShouldReviseFile(findFileData.cFileName)) {
                if (!RevReviseFile(subPath)) {
                    RevLogError("RevReviseFile failed to revise the file \"%ls\".",
                                subPath);
                }
            } else {

                /* Increment the total count of ignored files. */
                Revision->CountOfIgnoredFiles += 1;
            }
        }

        /*
         * Free after RevStringAppend.
         */
        free(subPath);
    } while (FindNextFileW(findFile, &findFileData) != 0);

    FindClose(findFile);

Exit:
    return status;
}

_Must_inspect_result_
BOOL
RevShouldReviseFile(
    _In_z_ PWCHAR FileName
    )
{
    LONG i;
    PWCHAR fileExtension;

    if (FileName == NULL) {
        RevLogError("FilePath is NULL.");
        return FALSE;
    }

    /*
     * Find the file extension.
     */
    fileExtension = wcsrchr(FileName, L'.');
    if (fileExtension == NULL) {
        RevLogWarning("Failed to determine the extension for the file \"%ls\".",
                      FileName);
        return FALSE;
    }

    /*
     * Check if the extension matches any entries in the ExtensionMappingTable.
     */
    for (i = 0; i < ARRAYSIZE(ExtensionMappingTable); ++i) {
        if (wcscmp(ExtensionMappingTable[i].Extension, fileExtension) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

_Must_inspect_result_
BOOL
RevReviseFile(
    _In_z_ PWCHAR FilePath
    )
{
    BOOL status = TRUE;
    PREVISION_RECORD revisionRecord;
    ULONGLONG lineCountTotal = 0;
    ULONGLONG lineCountBlank = 0;
    PCHAR fileBuffer = NULL;
    PWCHAR fileExtension;
    HANDLE file;
    LARGE_INTEGER fileSize;
    DWORD bytesRead;
    BOOL isPreviousCharCarriageReturn;
    BOOL isNextCharCarriageReturn;
    BOOL isNextCharNewline;
    LONG i;

    /*
     * Attempt to open the file.
     */
    file = CreateFile(FilePath,
                      GENERIC_READ,
                      FILE_SHARE_READ,
                      NULL,
                      OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                      NULL);
    if (file == INVALID_HANDLE_VALUE) {
        RevLogError("Failed to open the file \"%ls\". The last known error: %ls.",
                    FilePath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    /*
     * Retrieve the size of the file
     */
    if (!GetFileSizeEx(file, &fileSize)) {
        RevLogError("Failed to retrieve the size of the file \"%ls\". The last known error: %ls.",
                    FilePath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    /*
     * Allocate buffer for the entire file.
     */
    fileBuffer = (PCHAR)malloc(fileSize.QuadPart * sizeof(CHAR));
    if (fileBuffer == NULL) {
        RevLogError("Failed to allocate a line buffer (%d bytes)",
                    fileSize.QuadPart * sizeof(CHAR));
        status = FALSE;
        goto Exit;
    }

    /*
     * Attempt to read the file.
     */
    if (!ReadFile(file,
                  fileBuffer,
                  fileSize.QuadPart * sizeof(CHAR),
                  &bytesRead,
                  NULL)) {
        RevLogError("Failed to read the file \"%ls\". The last known error: %ls.",
                    FilePath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    for (i = 0; i < bytesRead; i++) {
        if (fileBuffer[i] == '\n') {

            /*
             * Increment the total line count for every newline
             * character encountered.
             */
            ++lineCountTotal;

            /* Check if the previous character is a carriage return character. */
            isPreviousCharCarriageReturn = (i > 0) && (fileBuffer[i - 1] == '\r');

            /* Check if the next character is a carriage return character. */
            isNextCharCarriageReturn = (i < bytesRead - 2) && (fileBuffer[i + 1] == '\r');

            /* Check if the character after the next one is a newline character. */
            isNextCharNewline = (i < bytesRead - 2) && (fileBuffer[i + 2] == '\n');

            /*
             * Check is it a blank line?
             */
            if (isPreviousCharCarriageReturn &&
                isNextCharCarriageReturn &&
                isNextCharNewline) {

                /* If so, increment the blank line count. */
                ++lineCountBlank;
            }
        }
    }
    if (fileSize.QuadPart > 0) {
        /*
         * Increment the total line count for the last line.
         */
        ++lineCountTotal;

        /*
         * Check if the last line is a blank line.
         */
        if (fileBuffer[bytesRead - 2] == '\r' &&
            fileBuffer[bytesRead - 1] == '\n') {
            /* If so, increment the blank line count. */
            ++lineCountBlank;
        }
    }

    /*
     * Find the file extension.
     */
    fileExtension = wcsrchr(FilePath, L'.');
    if (fileExtension == NULL) {
        RevLogError("Failed to determine the extension for the file \"%ls\".", FilePath);
        status = FALSE;
        goto Exit;
    }

    /*
     * If there is a revision recourd for that language/file type in the revision
     * record list, we will update it. If this file type has not been encountered,
     * we create a new node for it in the list.
     */
    revisionRecord = RevFindRevisionRecordForLanguageByExtension(fileExtension);
    if (revisionRecord == NULL) {
        RevLogError("Failed to get/initialize the revision record for the file extension \"%ls\".",
                    fileExtension);
        status = FALSE;
        goto Exit;
    }

    /*
     * Update the count of lines for the extension.
     */
    revisionRecord->CountOfLinesTotal += lineCountTotal;
    revisionRecord->CountOfLinesBlank += lineCountBlank;
    revisionRecord->CountOfFiles += 1;

    /*
     * Update the count of lines for the revision.
     */
    Revision->CountOfLinesTotal += lineCountTotal;
    Revision->CountOfLinesBlank += lineCountBlank;
    Revision->CountOfFiles += 1;

Exit:
    CloseHandle(file);

    if (fileBuffer) {
        free(fileBuffer);
    }

    return status;
}

VOID
RevOutputRevisionStatistics(
    VOID
    )
{
    PLIST_ENTRY entry;
    PREVISION_RECORD revisionRecord;

    /*
     * The table header.
     */
    RevPrint(L"----------------------------------------------------------------------------------\n");
    RevPrint(L"%-25s%10s%22s%25s\n",
             L"File Type",
             L"Files",
             L"Blank",
             L"Total");
    RevPrint(L"----------------------------------------------------------------------------------\n");

    /*
     * Iterate through the revision record list and print statistics for each file type.
     */
    for (entry = Revision->RevisionRecordListHead.Flink;
         entry != &Revision->RevisionRecordListHead;
         entry = entry->Flink) {

        revisionRecord = CONTAINING_RECORD(entry, REVISION_RECORD, ListEntry);
        if (revisionRecord) {
            RevPrint(L"%-25s%10u%22u%25u\n",
                     revisionRecord->ExtensionMapping.LanguageOrFileType,
                     revisionRecord->CountOfFiles,
                     revisionRecord->CountOfLinesBlank,
                     revisionRecord->CountOfLinesTotal);
        }
    }

    /*
     * The table footer with total statistics.
     */
    RevPrint(L"----------------------------------------------------------------------------------\n");
    RevPrint(L"%-25s%10u%22u%25u\n",
             L"Total:",
             Revision->CountOfFiles,
             Revision->CountOfLinesBlank,
             Revision->CountOfLinesTotal);
    RevPrint(L"----------------------------------------------------------------------------------\n");
}

int
wmain(
    int argc,
    wchar_t *argv[]
    )
{
    int status = 0;
    BOOL measuringTime = TRUE;
    LARGE_INTEGER startQpc;
    LARGE_INTEGER endQpc;
    LARGE_INTEGER frequency;
    PWCHAR revisionPath = NULL;
    SIZE_T revisionPathLength;
    REVISION_INIT_PARAMS revisionInitParams;
    LONG i;

    SupportAnsi = SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
                                 ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    RevPrint(WelcomeString);

    /*
     * Process the command line arguments if any.
     */

    if (argc <= 1) {
        /*
         * The command line arguments were not passed at all, so a folder selection dialog
         * should be opened where the user can select a directory to perform the revision.
         */
        RevPrint(UsageString);
        goto Exit;
    }

    if (wcscmp(argv[1], L"-help") == 0 ||
        wcscmp(argv[1], L"-h") == 0 ||
        wcscmp(argv[1], L"-?") == 0) {
        /*
         * The only command line argument passed was '-help', '-h', or '-?', so show the
         * instruction for use.
         */
        RevPrint(UsageString);
        goto Exit;
    }

    /*
     * The first argument is the path to the root revision directory.
     */
    revisionPath = argv[1];

    revisionPathLength = wcslen(revisionPath);

    /*
     * As part of improving input parameter validation, remove trailing '\' symbols
     * replacing them with '\0' character.
     */
    while (revisionPathLength > 0 &&
           revisionPath[revisionPathLength - 1] == L'\\') {
        revisionPath[--revisionPathLength] = L'\0';
    }

    /*
     * Prepend L"\\?\" to the `argv[1]` if not prepended yet to avoid the obsolete
     * MAX_PATH limitation.
     */
    if (wcsncmp(revisionPath, MAX_PATH_FIX, wcslen(MAX_PATH_FIX)) != 0) {

        revisionPath = RevStringPrepend(revisionPath, MAX_PATH_FIX);
        if (revisionPath == NULL) {
            RevLogError("Failed to normalize the revision path (RevStringPrepend failed).");
            status = -1;
            goto Exit;
        }
    }

    /*
     * Now we are ready to set the root path of the revision directory.
     */
    revisionInitParams.RootDirectory = revisionPath;
    revisionInitParams.IsVerboseMode = FALSE;

    if (argc > 2) {
        /*
         * It is expected that in the case of multiple command line arguments:
         *  1) The first argument is the path to the root revision directory.
         *  2) The remaining parameters are for optional revision configuration overrides.
         *
         * Process additional parameters:
         */

        for (i = 2; i < argc; ++i) {

            /*
             * -v: Sets the IsVerboseMode configuration flag to TRUE.
             */
            if (wcscmp(argv[i], L"-v") == 0) {
                revisionInitParams.IsVerboseMode = TRUE;
            }

        }
    }

#ifndef NDEBUG
    /*
     * Always use verbose mode in debug builds.
     */
    if (revisionInitParams.IsVerboseMode == FALSE) {
        revisionInitParams.IsVerboseMode = TRUE;
    }
#endif

    /*
     * Initialize the revision engine.
     */
    status = RevInitializeRevision(&revisionInitParams);
    if (status == FALSE) {
        RevLogError("Failed to initialize the revision engine.");
        goto Exit;
    }

    if (!QueryPerformanceFrequency(&frequency)) {
        RevLogError("Failed to retrieves the frequency of the performance counter.");
        measuringTime = FALSE;
    }

    if (measuringTime) {
        QueryPerformanceCounter(&startQpc);
    }

    status = RevStartRevision();
    if (status == FALSE) {
        RevLogError("Failed to start the revision engine.");
        goto Exit;
    }

    if (measuringTime) {
        QueryPerformanceCounter(&endQpc);
    }

    RevOutputRevisionStatistics();

    RevPrintEx(Cyan,
               L"Time: %.3fs\tIgnored %d files\n",
               (double)(endQpc.QuadPart - startQpc.QuadPart) / frequency.QuadPart,
               Revision->CountOfIgnoredFiles);

    system("pause");

Exit:
    free(revisionPath);

    return status;
}
