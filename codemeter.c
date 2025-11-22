/*++

Copyright (c) 2023-2025  wnstngs. All rights reserved.

Module Name:

    codemeter.c
    
Abstract:

    This module implements CodeMeter, a program for counting lines of code.

    Throughout the code, the term "revision" refers to the entire process, which
    includes scanning files, counting the number of files, and calculating the
    total lines of code.

                     path ┌─────────────────┐ returns
    Init params ─────────►│    Revision     ├─────────► Statistics
                          └─────────────────┘

--*/

//
// -------------------------------------------------------------------- Includes
//

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <pathcch.h>

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif


//
// ------------------------------------------------------- Data Type Definitions
//

/**
 * This structure stores the initialization parameters of the
 * revision provided by the user at launch.
 */
typedef struct REVISION_INIT_PARAMS {
    /**
     * Path to the revision root directory.
     */
    _Field_z_ PWCHAR RootDirectory;

    /**
     * Indicates whether verbose revision mode is active.
     */
    BOOL IsVerboseMode;
} REVISION_INIT_PARAMS, *PREVISION_INIT_PARAMS;

/**
 * his enumeration defines the types of file extensions
 * (to be used in REVISION_RECORD_EXTENSION_MAPPING).
 */
typedef enum REVISION_RECORD_EXTENSION_TYPE {
    /**
     * File extension with a single dot, e.g. ".txt".
     */
    SingleDot,

    /**
     * File extension with multiple dots, e.g. ".CMakeLists.txt".
     */
    MultiDot
} REVISION_RECORD_EXTENSION_TYPE;

/**
 * This structure stores the mapping of file extensions to
 * programming languages.
 */
typedef struct REVISION_RECORD_EXTENSION_MAPPING {
    /**
     * File extension.
     */
    _Field_z_ PWCHAR Extension;

    /**
     * Programming language or file type.
     */
    _Field_z_ PWCHAR LanguageOrFileType;

    /**
     * Extension type which indicates whether the extension is a
     * single-dot or multi-dot extension.
     */
    /* TODO: Temporarily disabled. Re-evaluate the approach.
    REVISION_RECORD_EXTENSION_TYPE ExtensionType; */
} REVISION_RECORD_EXTENSION_MAPPING, *PREVISION_RECORD_EXTENSION_MAPPING;

/**
 * @brief This structure stores the statistics for a single revision record.
 *
 * A revision record represents a group of files with the same extension or
 * language mapping.
 */
typedef struct REVISION_RECORD {
    /**
     * Linked list entry.
     */
    LIST_ENTRY ListEntry;

    /**
     * Extension of the revision record file and recognized
     * programming language/file type based on extension.
     */
    REVISION_RECORD_EXTENSION_MAPPING ExtensionMapping;

    /**
     * Number of lines in the revision record.
     */
    ULONGLONG CountOfLinesTotal;

    /**
     * Number of blank lines in the revision record.
     */
    ULONGLONG CountOfLinesBlank;

    /**
     * Number of comment lines in the revision record.
     */
    ULONGLONG CountOfLinesComment;

    /**
     * Number of files in the revision record.
     */
    ULONG CountOfFiles;
} REVISION_RECORD, *PREVISION_RECORD;

/**
 * This structure stores the statistics of the entire revision.
 */
typedef struct REVISION {
    /**
     * Revision initialization parameters provided by the user.
     */
    REVISION_INIT_PARAMS InitParams;

    /**
     * List of revision records.
     */
    LIST_ENTRY RevisionRecordListHead;

    /**
     * Number of lines in the whole project.
     */
    ULONGLONG CountOfLinesTotal;

    /**
     * Number of blank lines in the whole project.
     */
    ULONGLONG CountOfLinesBlank;

    /**
     * Number of comment lines in the whole project.
     */
    ULONGLONG CountOfLinesComment;

    /**
     * Number of files in the whole project.
     */
    ULONG CountOfFiles;

    /**
     * Number of ignored files during the revision.
     */
    ULONG CountOfIgnoredFiles;
} REVISION, *PREVISION;

/**
 * This structure holds per-file line statistics.
 */
typedef struct LINE_STATS {
    /**
     * Total number of lines.
     */
    ULONGLONG CountOfLinesTotal;

    /**
     * Number of blank lines.
     */
    ULONGLONG CountOfLinesBlank;

    /**
     * Number of comment-only lines.
     */
    ULONGLONG CountOfLinesComment;
} LINE_STATS, *PLINE_STATS;

/**
 * This enumeration represents logical "language families" used for
 * comment parsing.
 */
typedef enum REV_LANGUAGE_FAMILY {
    RevLanguageFamilyUnknown = 0,
    RevLanguageFamilyCStyle,     /* // and /* ... *\/ style */
    RevLanguageFamilyHashStyle,  /* # ... */
    RevLanguageFamilyDoubleDash, /* -- ... (SQL, Haskell, etc.) */
    RevLanguageFamilySemicolon,  /* ; ... (some Lisps, assembly) */
    RevLanguageFamilyMax
} REV_LANGUAGE_FAMILY;

/**
 * This structure maps a language name substring to a language family.
 */
typedef struct LANGUAGE_FAMILY_MAPPING {
    _Field_z_ PWCHAR LanguageSubstring;
    REV_LANGUAGE_FAMILY LanguageFamily;
} LANGUAGE_FAMILY_MAPPING, *PLANGUAGE_FAMILY_MAPPING;

/**
 * This enumeration represents different console text colors.
 */
typedef enum CONSOLE_FOREGROUND_COLOR {
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan
} CONSOLE_FOREGROUND_COLOR;

//
// ------------------------------------------------------- Constants and Globals
//

#define REV_EXTENSION_HASH_BUCKET_COUNT 1024u

/**
 * @brief The string to be prepended to a path to avoid the MAX_PATH
 * limitation.
 */
#define MAX_PATH_FIX    L"\\\\?\\"

/**
 * @brief The string to be appended to a path to indicate all of its
 * contents.
 */
#define ASTERISK        L"\\*"

#define CARRIAGE_RETURN  '\r'
#define LINE_FEED        '\n'

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
 *
 * @note The order must be the same as in the enumeration
 * CONSOLE_FOREGROUND_COLOR.
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

// @formatter:off

/*
 * N.B. This table is intentionally small and data-driven.
 * Anything that does not match here is treated as C-style by default.
 */
LANGUAGE_FAMILY_MAPPING LanguageFamilyMappingTable[] = {
    {L"Python",         RevLanguageFamilyHashStyle},
    {L"Ruby",           RevLanguageFamilyHashStyle},
    {L"Perl",           RevLanguageFamilyHashStyle},
    {L"Shell",          RevLanguageFamilyHashStyle},
    {L"bash",           RevLanguageFamilyHashStyle},
    {L"make",           RevLanguageFamilyHashStyle},
    {L"Make",           RevLanguageFamilyHashStyle},
    {L"PowerShell",     RevLanguageFamilyHashStyle},
    {L"Raku",           RevLanguageFamilyHashStyle},
    {L"awk",            RevLanguageFamilyHashStyle},
    {L"SQL",            RevLanguageFamilyDoubleDash},
    {L"Haskell",        RevLanguageFamilyDoubleDash},
    {L"Lisp",           RevLanguageFamilySemicolon},
    {L"Scheme",         RevLanguageFamilySemicolon},
    {L"Assembly",       RevLanguageFamilySemicolon},
};

/**
 * @brief Mapping of file extensions that can be recognized to
 * human-readable descriptions of file types.
 *
 * TODO: Multi-dot extensions are commented out, we need to add support
 *       for them. The current algorithm counts everything after the last
 *       dot as an extension.
 *
 * TODO: Sort the table by extension and use binary search.
 *       Or build a hash table on first use and look up in O(1).
 */
REVISION_RECORD_EXTENSION_MAPPING ExtensionMappingTable[] = {
    {L".abap",               L"ABAP"},
    {L".asl",               L"ACPI Machine Language "},
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
//    {L".web.config",         L"XML"},
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

//@formatter:on

/**
 * @brief Simple open-addressing hash table over ExtensionMappingTable.
 *
 * This table is derived from ExtensionMappingTable at runtime and used
 * to speed up extension lookups.
 */
const REVISION_RECORD_EXTENSION_MAPPING *RevExtensionHashTable[
    REV_EXTENSION_HASH_BUCKET_COUNT
];

BOOL RevExtensionHashTableInitialized = FALSE;

/**
 * @brief The global revision state used throughout the entire program
 * run-time.
 */
PREVISION Revision = NULL;

/**
 * @brief Indicates whether ANSI escape sequences are supported.
 */
BOOL SupportAnsi;

//
// ------------------------------------------------ Internal Function Prototypes
//

_Ret_maybenull_
_Must_inspect_result_
PWCHAR
RevGetLastKnownWin32Error(
    VOID
    );

_Ret_maybenull_
_Must_inspect_result_
PWCHAR
RevStringAppend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    );

_Ret_maybenull_
_Must_inspect_result_
PWCHAR
RevStringPrepend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    );

ULONG
RevHashExtensionKey(
    _In_z_ PWCHAR Extension
    );

VOID
RevInitializeExtensionHashTable(
    VOID
    );

_Must_inspect_result_
BOOL
RevInitializeRevision(
    _In_ PREVISION_INIT_PARAMS InitParams
    );

_Must_inspect_result_
BOOL
RevStartRevision(
    VOID
    );

_Ret_maybenull_
_Must_inspect_result_
PREVISION_RECORD
RevInitializeRevisionRecord(
    _In_z_ PWCHAR Extension,
    _In_z_ PWCHAR LanguageOrFileType
    );

REV_LANGUAGE_FAMILY
RevGetLanguageFamily(
    _In_z_ PWCHAR LanguageOrFileType
    );

_Must_inspect_result_
BOOL
RevEnumerateRecursively(
    _In_z_ PWCHAR RootDirectoryPath
    );

_Ret_maybenull_
PWCHAR
RevMapExtensionToLanguage(
    _In_z_ PWCHAR Extension
    );

_Ret_maybenull_
_Must_inspect_result_
PREVISION_RECORD
RevFindRevisionRecordForLanguageByExtension(
    _In_z_ PWCHAR Extension
    );

VOID
RevCountLinesCStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _Inout_ PLINE_STATS LineStats
    );

VOID
RevCountLinesHashStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _Inout_ PLINE_STATS LineStats
    );

VOID
RevCountLinesLineCommentStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _In_ CHAR FirstCommentChar,
    _In_ CHAR SecondCommentChar,
    _Inout_ PLINE_STATS LineStats
    );

VOID
RevCountLinesWithFamily(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _In_ REV_LANGUAGE_FAMILY LanguageFamily,
    _Inout_ PLINE_STATS LineStats
    );

_Must_inspect_result_
BOOL
RevShouldReviseFile(
    _In_z_ PWCHAR FileName
    );

_Must_inspect_result_
BOOL
RevReviseFile(
    _In_z_ PWCHAR FilePath
    );

VOID
RevOutputRevisionStatistics(
    VOID
    );

//
// ------------------------------------------------------------------- Functions
//


/**
 * @brief This function initializes a LIST_ENTRY structure that
 * represents the head of a doubly linked list.
 *
 * @param ListHead Supplies a pointer to a LIST_ENTRY that represents the
 * head of the list.
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
 *
 * @param ListHead Supplies a pointer to a LIST_ENTRY that represents the
 * head of the list.
 *
 * @return TRUE if there are currently no entries in the list and FALSE
 * otherwise.
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
 *
 * @param ListHead Supplies a pointer to a LIST_ENTRY that represents the
 * head of the list.
 *
 * @param Entry Supplies a pointer to a LIST_ENTRY that represents the
 * entry to be inserted.
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
 *
 * @param Color Supplies the text foreground color.
 *
 * @param Format Supplies the format specifier.
 *
 * @param ... Supplies additional parameters to be formatted and printed.
 */
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
 * @brief This function prints a formatted string in [default] green
 * color.
 *
 * @param Format Supplies the format specifier.
 *
 * @param ... Supplies additional parameters to be formatted and printed.
 */
#define RevPrint(Format, ...)                           \
    do {                                                \
        RevPrintEx(Green, Format, __VA_ARGS__);         \
    } while (0)

/**
 * @brief This function outputs a red text error message to the standard
 * error stream.
 *
 * @param Message Supplies the error message.
 *
 * @note This function respects the verbose mode setting from the global
 * revision structure. The logging is conditioned on whether verbose mode
 * is enabled.
 */
#define RevLogError(Message, ...)                                                       \
    do {                                                                                \
        if (!Revision || Revision->InitParams.IsVerboseMode) {                          \
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
 * @brief This function outputs a yellow text warning message to the
 * standard output stream.
 *
 * @param Message Supplies the warning message.
 *
 * @note This function respects the verbose mode setting from the global
 * revision structure. The logging is conditioned on whether verbose mode
 * is enabled.
 */
#define RevLogWarning(Message, ...)                                                     \
    do {                                                                                \
        if (!Revision || Revision->InitParams.IsVerboseMode) {                          \
            fprintf(stdout,                                                             \
                    SupportAnsi ?                                                       \
                        "\033[0;33m[WARNING]\n└───> (in %s@%d): " Message "\033[0m\n" : \
                        "[WARNING]\n└───> (in %s@%d): " Message "\n",                   \
                        __FUNCTION__,                                                   \
                        __LINE__,                                                       \
                        ##__VA_ARGS__);                                                 \
        }                                                                               \
    } while (0)

/**
 * @brief This function retrieves the calling thread's last-error code
 * value and translates it into its corresponding error message.
 *
 * @return A pointer to the error message string on success,
 * or NULL on failure.
 */
_Ret_maybenull_
_Must_inspect_result_
PWCHAR
RevGetLastKnownWin32Error(
    VOID
    )
{
    static WCHAR messageBuffer[256];
    DWORD formatResult = {0};
    const DWORD lastKnownError = GetLastError();

    /*
     * Format the error code into a human-readable string.
     */
    formatResult = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                  FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL,
                                  lastKnownError,
                                  0,
                                  (LPWSTR)&messageBuffer,
                                  ARRAYSIZE(messageBuffer),
                                  NULL);

    if (formatResult == 0) {
        /*
         * If FormatMessageW failed, convert the error code to a string.
         */
        _snwprintf_s(messageBuffer,
                     ARRAYSIZE(messageBuffer),
                     _TRUNCATE,
                     L"%lu",
                     lastKnownError);
    }

    return messageBuffer;
}

/**
 * @brief This function appends one unicode string to another and returns
 * the result.
 *
 * @param String1 The first string (to which String2 will be appended).
 *
 * @param String2 The second string (to be appended to String1).
 *
 * @return A new string containing the concatenation of String1 and
 * String2. NULL if the function failed.
 *
 * @remarks The caller is responsible for freeing the memory.
 */
_Ret_maybenull_
_Must_inspect_result_
PWCHAR
RevStringAppend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    )
{
    SIZE_T string1Length;
    SIZE_T string2Length;
    SIZE_T resultStringLength;
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
        RevLogError("Failed to allocate string buffer (%llu bytes).",
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

/**
 * @brief This function prepends one unicode string to another and
 * returns the result.
 *
 * @param String1 Supplies the first string (to be appended to).
 *
 * @param String2 Supplies the second string (to be prepended).
 *
 * @return A new string containing the concatenation of String1 and
 * String2. NULL if the function failed.
 *
 * @remarks N.B. The caller is responsible for freeing the memory.
 */
_Ret_maybenull_
_Must_inspect_result_
PWCHAR
RevStringPrepend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    )
{
    SIZE_T string1Length;
    SIZE_T string2Length;
    SIZE_T resultStringLength;
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
        RevLogError("Failed to allocate string buffer (%llu bytes).",
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

/**
 * This function computes a hash code for an extension key using a simple
 * FNV-1a 32-bit hash over a lower-cased extension string.
 *
 * @param Extension Supplies the file extension (e.g. L".c").
 *
 * @return A 32-bit hash value for the extension.
 */
ULONG
RevHashExtensionKey(
    _In_z_ PWCHAR Extension
    )
{
    ULONG hash = 2166136261u;

    if (Extension == NULL) {
        return 0;
    }

    while (*Extension != L'\0') {

        WCHAR ch = *Extension;

        if (ch >= L'A' && ch <= L'Z') {
            ch = (WCHAR)(ch - L'A' + L'a');
        }

        hash ^= (ULONG)ch;
        hash *= 16777619u;

        Extension += 1;
    }

    return hash;
}

/**
 * This function initializes the extension hash table from the
 * ExtensionMappingTable.
 */
VOID
RevInitializeExtensionHashTable(
    VOID
    )
{
    ULONG i;

    if (RevExtensionHashTableInitialized) {
        return;
    }

    RtlZeroMemory((PVOID)RevExtensionHashTable, sizeof(RevExtensionHashTable));

    for (i = 0; i < ARRAYSIZE(ExtensionMappingTable); i += 1) {

        const REVISION_RECORD_EXTENSION_MAPPING *entry =
            &ExtensionMappingTable[i];

        ULONG hash = RevHashExtensionKey(entry->Extension);
        ULONG bucket = hash & (REV_EXTENSION_HASH_BUCKET_COUNT - 1);

        ULONG probes = 0;

        while (probes < REV_EXTENSION_HASH_BUCKET_COUNT) {

            if (RevExtensionHashTable[bucket] == NULL) {
                RevExtensionHashTable[bucket] = entry;
                break;
            }

            if (_wcsicmp(RevExtensionHashTable[bucket]->Extension,
                         entry->Extension) == 0) {
                //
                // Duplicate extension in the table, keep the first mapping.
                //
                break;
            }

            bucket = (bucket + 1) & (REV_EXTENSION_HASH_BUCKET_COUNT - 1);

            probes += 1;
        }
    }

    RevExtensionHashTableInitialized = TRUE;
}

/**
 * @brief This function is responsible for initializing the revision
 * system.
 *
 * @param InitParams Supplies the revision initialization parameters.
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
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

    if (InitParams == NULL || InitParams->RootDirectory == NULL) {

        RevLogError("Invalid parameter/-s.");
        status = FALSE;
        goto Exit;
    }

    //
    // Initialize the revision structure.
    //

    Revision = (PREVISION)malloc(sizeof(REVISION));
    if (Revision == NULL) {
        RevLogError("Failed to allocate memory for the global revision "
                    "structure (%llu bytes).",
                    sizeof(REVISION));
        status = FALSE;
        goto Exit;
    }

    //
    // Initialize the list of revision records and other fields.
    //
    RevInitializeListHead(&Revision->RevisionRecordListHead);
    Revision->InitParams = *InitParams;
    Revision->CountOfLinesTotal = 0;
    Revision->CountOfLinesBlank = 0;
    Revision->CountOfLinesComment = 0;
    Revision->CountOfFiles = 0;
    Revision->CountOfIgnoredFiles = 0;

Exit:
    return status;
}

/**
 * @brief This function is responsible for starting the revision system.
 * It ensures that the system has been initialized correctly before
 * proceeding with its operations.
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
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

    //
    // Pass a duplicate of the root directory so the recursive function can
    // allocate/free its own copy.
    //
    PWCHAR rootPathCopy = _wcsdup(Revision->InitParams.RootDirectory);
    if (rootPathCopy == NULL) {
        RevLogError("Failed to duplicate root directory path.");
        status = FALSE;
        goto Exit;
    }

    status = RevEnumerateRecursively(rootPathCopy);

    free(rootPathCopy);

Exit:

    return status;
}

/**
 * @brief This function initializes a REVISION_RECORD structure.
 *
 * @param Extension Supplies the file extension of the revision record.
 *
 * @param LanguageOrFileType Supplies the language or file type of the
 * revision record.
 *
 * @return If the initialization is successful, returns a pointer to the
 * new revision record; otherwise, NULL.
 */
_Ret_maybenull_
PREVISION_RECORD
RevInitializeRevisionRecord(
    _In_z_ PWCHAR Extension,
    _In_z_ PWCHAR LanguageOrFileType
    )
{
    PREVISION_RECORD revisionRecord = NULL;

    revisionRecord = (PREVISION_RECORD)malloc(sizeof(REVISION_RECORD));
    if (revisionRecord == NULL) {
        RevLogError("Failed to allocate memory for the revision record "
                    "(%llu bytes).",
                    sizeof(REVISION_RECORD));
        return NULL;
    }

    RevInitializeListHead(&revisionRecord->ListEntry);
    revisionRecord->ExtensionMapping.Extension = Extension;
    revisionRecord->ExtensionMapping.LanguageOrFileType = LanguageOrFileType;
    revisionRecord->CountOfLinesTotal = 0;
    revisionRecord->CountOfLinesBlank = 0;
    revisionRecord->CountOfLinesComment = 0;
    revisionRecord->CountOfFiles = 0;

    return revisionRecord;
}

/**
 * This function maps a file extension to a language/file type.
 *
 * @param Extension Supplies the file extension (e.g. L".c").
 *
 * @return Pointer to a language/file type string on success, NULL if
 *         the extension is unknown.
 */
_Ret_maybenull_
PWCHAR
RevMapExtensionToLanguage(
    _In_z_ PWCHAR Extension
    )
{
    ULONG hash;
    ULONG bucket;
    ULONG probes;

    if (Extension == NULL) {
        RevLogError("Extension is NULL.");
        return NULL;
    }

    RevInitializeExtensionHashTable();

    hash = RevHashExtensionKey(Extension);
    bucket = hash & (REV_EXTENSION_HASH_BUCKET_COUNT - 1);
    probes = 0;

    while (probes < REV_EXTENSION_HASH_BUCKET_COUNT) {

        const REVISION_RECORD_EXTENSION_MAPPING *entry =
            RevExtensionHashTable[bucket];

        if (entry == NULL) {
            break;
        }

        if (_wcsicmp(entry->Extension, Extension) == 0) {
            return entry->LanguageOrFileType;
        }

        bucket = (bucket + 1) & (REV_EXTENSION_HASH_BUCKET_COUNT - 1);
        probes += 1;
    }

    //
    // Fallback: linear scan if no entry was found in the hash table.
    //
    for (bucket = 0; bucket < ARRAYSIZE(ExtensionMappingTable); bucket += 1) {

        if (_wcsicmp(ExtensionMappingTable[bucket].Extension, Extension) == 0) {

            return ExtensionMappingTable[bucket].LanguageOrFileType;
        }

    }

    return NULL;
}

/**
 * @brief This function checks if a REVISION_RECORD for a language/file
 * type with a given extension exists in the global revision's list of
 * revision records.
 *
 * @param Extension Supplies the file extension to search for.
 *
 * @return If a matching REVISION_RECORD is found, returns a pointer to
 * that record; otherwise, returns NULL.
 */
_Ret_maybenull_
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
        if (wcscmp(revisionRecord->ExtensionMapping.LanguageOrFileType,
                   languageOrFileType) == 0) {
            return revisionRecord;
        }

        entry = entry->Flink;
    }

    /*
     * If no matching language or file type was found for the provided extension
     * initialize a new revision record.
     */
    revisionRecord = RevInitializeRevisionRecord(Extension,
                                                 languageOrFileType);
    if (revisionRecord == NULL) {
        RevLogError("Failed to initialize a revision record (\"%ls\",\"%ls\").",
                    Extension,
                    languageOrFileType);
        return NULL;
    }

    /*
     * Add the new revision record to the global list of revision records.
     */
    RevInsertTailList(&Revision->RevisionRecordListHead,
                      &revisionRecord->ListEntry);

    return revisionRecord;
}

/**
 * This function returns the language family for a given language/file type name.
 *
 * @param LanguageOrFileType Supplies the language or file type string
 *                           (as stored in ExtensionMappingTable).
 *
 * @return One of REV_LANGUAGE_FAMILY values. Defaults to
 *         RevLanguageFamilyCStyle if no specific mapping is found.
 */
REV_LANGUAGE_FAMILY
RevGetLanguageFamily(
    _In_z_ PWCHAR LanguageOrFileType
    )
{
    LONG index;

    if (LanguageOrFileType == NULL) {
        return RevLanguageFamilyCStyle;
    }

    for (index = 0; index < ARRAYSIZE(LanguageFamilyMappingTable); index += 1) {
        if (wcsstr(LanguageOrFileType,
                   LanguageFamilyMappingTable[index].LanguageSubstring)) {
            return LanguageFamilyMappingTable[index].LanguageFamily;
        }
    }

    //
    // Default for everything that is not explicitly configured.
    //
    return RevLanguageFamilyCStyle;
}

/**
 * @brief This function is designed to recursively traverse and enumerate
 * files and subdirectories within a given root directory path.
 *
 * @param RootDirectoryPath Supplies the root directory path from which
 * enumeration should begin.
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevEnumerateRecursively(
    _In_z_ PWCHAR RootDirectoryPath
    )
{
    BOOL status = TRUE;
    HANDLE findFile = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW findFileData;
    PWCHAR searchPath = NULL;
    SIZE_T rootLength = 0;
    BOOL hasTrailingSeparator = FALSE;

    //
    // Check the validity of passed arguments.
    //
    if (RootDirectoryPath == NULL) {
        RevLogError("Invalid parameter/-s.");
        status = FALSE;
        goto Exit;
    }

    //
    // RootDirectoryPath is expected to be a plain directory path without
    // wildcard characters to keep path handling more predictable.
    //
    if (wcschr(RootDirectoryPath, L'*') != NULL) {

        RevLogError("The root directory path \"%ls\" must not contain "
                    "wildcard characters.",
                    RootDirectoryPath);
        status = FALSE;
        goto Exit;
    }

    rootLength = wcslen(RootDirectoryPath);

    if (rootLength == 0) {
        RevLogError("Root directory path is empty.");
        status = FALSE;
        goto Exit;
    }

    //
    // Determine whether the root directory path already has a trailing
    // path separator.
    //
    {
        WCHAR lastChar = RootDirectoryPath[rootLength - 1];
        hasTrailingSeparator = (lastChar == L'\\' || lastChar == L'/');
    }

    //
    // Build the search path used by FindFirstFileW / FindNextFileW.
    // Examples:
    //   "C:\\src"   -> "C:\\src\\*"
    //   "C:\\src\\" -> "C:\\src\\*"
    //
    {
        //
        // "*" or "\\*"
        //
        SIZE_T extraChars;
        if (hasTrailingSeparator) {
            extraChars = 1;
        } else {
            extraChars = 2;
        }
        //
        // +1 for NUL
        //
        SIZE_T searchLength = rootLength + extraChars + 1;

        searchPath = (PWCHAR)malloc(searchLength * sizeof(WCHAR));

        if (searchPath == NULL) {
            RevLogError("Failed to allocate memory for search path.");
            status = FALSE;
            goto Exit;
        }

        wcscpy_s(searchPath, searchLength, RootDirectoryPath);

        if (!hasTrailingSeparator) {
            wcscat_s(searchPath, searchLength, L"\\");
        }

        wcscat_s(searchPath, searchLength, L"*");
    }

    //
    // Try to find a file or subdirectory with a name that matches the pattern.
    //
    findFile = FindFirstFileW(searchPath,
                              &findFileData);

    free(searchPath);
    searchPath = NULL;

    if (findFile == INVALID_HANDLE_VALUE) {

        RevLogError("Failed to start enumeration in directory \"%ls\". "
                    "The last known error: %ls.",
                    RootDirectoryPath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    do {

        PWCHAR subPath = NULL;

        //
        // Skip the current directory (".") and parent directory ("..").
        //
        if (wcscmp(findFileData.cFileName, L".") == 0 ||
            wcscmp(findFileData.cFileName, L"..") == 0) {

            continue;
        }

        //
        // Build the full path for the current entry:
        //   RootDirectoryPath [\\] FileName
        //
        {
            SIZE_T nameLength = wcslen(findFileData.cFileName);
            SIZE_T separatorChars;

            if (hasTrailingSeparator) {
                separatorChars = 0;
            } else {
                separatorChars = 1;
            }

            //
            // +1 for NUL
            //
            SIZE_T subPathLength = rootLength + separatorChars + nameLength + 1;

            subPath = (PWCHAR)malloc(subPathLength * sizeof(WCHAR));

            if (subPath == NULL) {
                RevLogError("Failed to allocate memory for subpath.");
                status = FALSE;
                break;
            }

            wcscpy_s(subPath, subPathLength, RootDirectoryPath);

            if (!hasTrailingSeparator) {
                wcscat_s(subPath, subPathLength, L"\\");
            }

            wcscat_s(subPath, subPathLength, findFileData.cFileName);
        }

        //
        // Check if a directory was found.
        //
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

            //
            // Skip reparse points to avoid infinite loops.
            //
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                RevLogWarning("Skipping reparse point: %ls", subPath);
                free(subPath);
                continue;
            }

            //
            // Recursively traverse a subdirectory. Pass a duplicate of the
            // path so that the callee owns and may free its buffer without
            // affecting the caller.
            //
            {
                PWCHAR subPathCopy = _wcsdup(subPath);
                free(subPath);
                subPath = NULL;

                if (subPathCopy == NULL) {
                    RevLogError("Failed to duplicate subdirectory path.");
                    status = FALSE;
                    break;
                }

                if (!RevEnumerateRecursively(subPathCopy)) {
                    RevLogError("Recursive subdirectory traversal failed for "
                                "\"%ls\".",
                                subPathCopy);
                    status = FALSE;
                }

                free(subPathCopy);

                if (!status) {
                    break;
                }
            }

        } else {

            //
            // A regular file was found. If the file extension is recognized,
            // revise the file; otherwise, count it as ignored.
            //
            if (RevShouldReviseFile(findFileData.cFileName)) {

                if (!RevReviseFile(subPath)) {
                    RevLogError("RevReviseFile failed to revise file \"%ls\".",
                                subPath);
                    //
                    // Do not treat this as a fatal error for enumeration.
                    // Continue with other files.
                    //
                }

                free(subPath);

            } else {

                Revision->CountOfIgnoredFiles += 1;
                free(subPath);
            }
        }

    } while (FindNextFileW(findFile, &findFileData) != 0);

    //
    // If FindNextFileW failed for a reason other than "no more files",
    // treat it as an error.
    //
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        RevLogError("FindNextFileW failed while enumerating directory \"%ls\". "
                    "The last known error: %ls.",
                    RootDirectoryPath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
    }

Exit:

    if (findFile != INVALID_HANDLE_VALUE) {
        FindClose(findFile);
    }

    return status;
}

/**
 * @brief This function checks whether the specified file should be revised.
 *
 * The file is considered for revision if its extension is recognized in the
 * ExtensionMappingTable.
 *
 * @param FileName Supplies the file name (without path).
 *
 * @return TRUE if the file should be revised, FALSE otherwise.
 */
_Must_inspect_result_
BOOL
RevShouldReviseFile(
    _In_z_ PWCHAR FileName
    )
{
    PWCHAR fileExtension = NULL;
    PWCHAR languageOrFileType = NULL;

    if (FileName == NULL) {
        RevLogError("FileName is NULL.");
        return FALSE;
    }

    //
    // Find the file extension.
    //
    fileExtension = wcsrchr(FileName, L'.');
    if (fileExtension == NULL) {
        RevLogWarning("Failed to determine the extension for the file \"%ls\".",
                      FileName);
        return FALSE;
    }

    //
    // Check if the extension can be mapped to a known language/file type.
    //
    languageOrFileType = RevMapExtensionToLanguage(fileExtension);
    if (languageOrFileType == NULL) {
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief This function counts lines using C-style comments.
 *
 * Supported syntax:
 *   - Line comments:   // ... until end-of-line.
 *   - Block comments:  /* ... *\/ (no nesting).
 *   - String literals: "..." and '...' with simple backslash escaping.
 *
 * Lines are classified as:
 *   - Blank:   only whitespace.
 *   - Comment: only comment text (no code tokens).
 *   - Code:    any line that contains code, even if it also has comments.
 *
 * @param Buffer Supplies the file contents.
 *
 * @param Length Supplies the length of Buffer in bytes.
 *
 * @param LineStats Supplies a pointer to a LINE_STATS structure that
 *        receives the results.
 */
VOID
RevCountLinesCStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _Inout_ PLINE_STATS LineStats
    )
{
    SIZE_T index = {0};
    BOOL inBlockComment = FALSE;
    BOOL inString = FALSE;
    CHAR stringDelim = 0;
    BOOL sawCode = FALSE;
    BOOL sawComment = FALSE;
    BOOL sawNonWhitespace = FALSE;
    BOOL previousWasCR = FALSE;

    if (Buffer == NULL || LineStats == NULL || Length == 0) {
        return;
    }

    LineStats->CountOfLinesTotal = 0;
    LineStats->CountOfLinesBlank = 0;
    LineStats->CountOfLinesComment = 0;

    for (index = 0; index < Length; index += 1) {

        CHAR currentChar = Buffer[index];
        CHAR nextChar;

        if (index + 1 < Length) {
            nextChar = Buffer[index + 1];
        } else {
            nextChar = 0;
        }

        //
        // Handle CR and LF as line terminators, merge CRLF into a single
        // logical newline.
        //
        if (currentChar == CARRIAGE_RETURN || currentChar == LINE_FEED) {

            if (previousWasCR && currentChar == LINE_FEED) {
                previousWasCR = FALSE;
                continue;
            }

            LineStats->CountOfLinesTotal += 1;

            if (!sawNonWhitespace &&
                !sawComment &&
                !sawCode &&
                !inBlockComment) {

                LineStats->CountOfLinesBlank += 1;

            } else if (!sawCode &&
                       (sawComment || inBlockComment)) {

                LineStats->CountOfLinesComment += 1;
            }

            sawCode = FALSE;
            sawComment = FALSE;
            sawNonWhitespace = FALSE;

            previousWasCR = (currentChar == CARRIAGE_RETURN);

            continue;
        }

        previousWasCR = FALSE;

        if (!isspace((UCHAR)currentChar)) {
            sawNonWhitespace = TRUE;
        }

        //
        // Handle characters inside a block comment.
        //
        if (inBlockComment) {
            sawComment = TRUE;

            if (currentChar == '*' && nextChar == '/') {
                //
                // Consume '/'
                //
                inBlockComment = FALSE;
                index += 1;
            }

            continue;
        }

        //
        // Handle characters inside a string literal.
        //
        if (inString) {

            if (currentChar == '\\' && (index + 1) < Length) {

                //
                // Skip escaped character.
                //
                index += 1;

            } else if (currentChar == stringDelim) {
                inString = FALSE;
            }

            sawCode = TRUE;
            continue;
        }

        //
        // Outside strings and block comments.
        //

        //
        // Line comment: // ...
        //
        if (currentChar == '/' && nextChar == '/') {

            sawComment = TRUE;

            //
            // The rest of the line is considered comment and will be handled
            // when a newline is encountered.
            //
            continue;
        }

        //
        // Block comment: /* ... *\/
        //
        if (currentChar == '/' && nextChar == '*') {
            //
            // Consume '*'
            //
            inBlockComment = TRUE;
            sawComment = TRUE;
            index += 1;
            continue;
        }

        //
        // Start of string literal.
        //
        if (currentChar == '"' || currentChar == '\'') {
            inString = TRUE;
            stringDelim = currentChar;
            sawCode = TRUE;
            continue;
        }

        //
        // Any other non-whitespace character outside comments and strings
        // is treated as code.
        //
        if (!isspace((UCHAR)currentChar)) {
            sawCode = TRUE;
        }
    }

    //
    // Handle the final line when the file does not end with a newline.
    //
    if (sawNonWhitespace || sawComment || sawCode || inBlockComment) {

        LineStats->CountOfLinesTotal += 1;

        if (!sawNonWhitespace && !sawComment && !sawCode && !inBlockComment) {

            LineStats->CountOfLinesBlank += 1;

        } else if (!sawCode &&
                   (sawComment || inBlockComment)) {

            LineStats->CountOfLinesComment += 1;
        }
    }
}

/**
 * This function counts lines using hash-style comments ("# ...").
 *
 * @param Buffer Supplies the file contents.
 *
 * @param Length Supplies the length of Buffer in bytes.
 *
 * @param LineStats Supplies a pointer to a LINE_STATS structure that
 *                  receives the results.
 */
VOID
RevCountLinesHashStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _Inout_ PLINE_STATS LineStats
    )
{
    RevCountLinesLineCommentStyle(Buffer,
                                  Length,
                                  '#',
                                  0,
                                  LineStats);
}

/**
 * @brief This function is a helper that counts lines for languages that
 *        use only line comments with a fixed prefix (e.g. '#', ';', '--').
 *
 * Supported syntax:
 *   - Line comments:   FirstCommentChar [SecondCommentChar] ... until EOL.
 *   - String literals: "..." and '...' with simple backslash escaping.
 *
 * Lines are classified as:
 *   - Blank:   only whitespace.
 *   - Comment: only comment text (no code tokens).
 *   - Code:    any line that contains code, even if it also has comments.
 *
 * @param Buffer Supplies the file contents.
 *
 * @param Length Supplies the length of Buffer in bytes.
 *
 * @param FirstCommentChar  Supplies the first character of the comment
 *                          prefix (e.g. '#', '-', ';').
 *
 * @param SecondCommentChar Supplies the second character of the comment
 *                          prefix, or 0 for a single-character prefix.
 *
 * @param LineStats Supplies a pointer to a LINE_STATS structure that
 *                  receives the results.
 */
VOID
RevCountLinesLineCommentStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _In_ CHAR FirstCommentChar,
    _In_ CHAR SecondCommentChar,
    _Inout_ PLINE_STATS LineStats
    )
{
    SIZE_T index;
    BOOL inString = FALSE;
    CHAR stringDelim = 0;
    BOOL inLineComment = FALSE;
    BOOL sawCode = FALSE;
    BOOL sawComment = FALSE;
    BOOL sawNonWhitespace = FALSE;
    BOOL previousWasCR = FALSE;

    if (Buffer == NULL || LineStats == NULL || Length == 0) {
        return;
    }

    LineStats->CountOfLinesTotal = 0;
    LineStats->CountOfLinesBlank = 0;
    LineStats->CountOfLinesComment = 0;

    for (index = 0; index < Length; index += 1) {

        CHAR currentChar = Buffer[index];
        CHAR nextChar;

        if (index + 1 < Length) {
            nextChar = Buffer[index + 1];
        } else {
            nextChar = 0;
        }

        //
        // Handle CR and LF as line terminators, merge CRLF into a single
        // logical newline.
        //
        if (currentChar == CARRIAGE_RETURN || currentChar == LINE_FEED) {

            if (previousWasCR && currentChar == LINE_FEED) {
                previousWasCR = FALSE;
                continue;
            }

            LineStats->CountOfLinesTotal += 1;

            if (!sawNonWhitespace && !sawComment && !sawCode) {
                LineStats->CountOfLinesBlank += 1;
            } else if (!sawCode && sawComment) {
                LineStats->CountOfLinesComment += 1;
            }

            sawCode = FALSE;
            sawComment = FALSE;
            sawNonWhitespace = FALSE;
            inLineComment = FALSE;

            previousWasCR = (currentChar == CARRIAGE_RETURN);

            continue;
        }

        previousWasCR = FALSE;

        if (!isspace((UCHAR)currentChar)) {
            sawNonWhitespace = TRUE;
        }

        //
        // Handle characters inside a line comment.
        //
        if (inLineComment) {
            sawComment = TRUE;
            continue;
        }

        //
        // Handle characters inside a string literal.
        //
        if (inString) {
            if (currentChar == '\\' && (index + 1) < Length) {
                //
                // Skip escaped character.
                //
                index += 1;
            } else if (currentChar == stringDelim) {
                inString = FALSE;
            }

            sawCode = TRUE;
            continue;
        }

        //
        // Outside strings and comments.
        //

        //
        // Start of string literal.
        //
        if (currentChar == '"' || currentChar == '\'') {
            inString = TRUE;
            stringDelim = currentChar;
            sawCode = TRUE;
            continue;
        }

        //
        // Start of line comment (single-char or two-char prefix).
        //
        if (SecondCommentChar == 0) {

            if (currentChar == FirstCommentChar) {
                inLineComment = TRUE;
                sawComment = TRUE;
                continue;
            }

        } else {

            if (currentChar == FirstCommentChar &&
                nextChar == SecondCommentChar) {

                inLineComment = TRUE;
                sawComment = TRUE;

                //
                // Consume the second character of the prefix so it isn't
                // re-processed.
                //
                index += 1;
                continue;
            }
        }

        //
        // Any other non-whitespace character outside comments and strings
        // is treated as code.
        //
        if (!isspace((UCHAR)currentChar)) {
            sawCode = TRUE;
        }
    }

    //
    // Handle the final line when the file does not end with a newline.
    //
    if (sawNonWhitespace || sawComment || sawCode) {

        LineStats->CountOfLinesTotal += 1;

        if (!sawNonWhitespace && !sawComment) {
            LineStats->CountOfLinesBlank += 1;

        } else if (!sawCode && sawComment) {
            LineStats->CountOfLinesComment += 1;
        }
    }
}

/**
 * This function counts lines using a language family strategy.
 *
 * @param Buffer Supplies the file contents.
 *
 * @param Length Supplies the length of Buffer in bytes.
 *
 * @param LanguageFamily Supplies the language family that determines
 *                       which comment syntax is used.
 *
 * @param LineStats Supplies a pointer to a LINE_STATS structure that
 *                  receives the results.
 */
VOID
RevCountLinesWithFamily(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _In_ REV_LANGUAGE_FAMILY LanguageFamily,
    _Inout_ PLINE_STATS LineStats
    )
{
    switch (LanguageFamily) {

    case RevLanguageFamilyHashStyle:
        //
        // Languages with "#" line comments.
        //
        RevCountLinesHashStyle(Buffer,
                               Length,
                               LineStats);
        break;

    case RevLanguageFamilyDoubleDash:
        //
        // Languages with "--" line comments.
        //
        RevCountLinesLineCommentStyle(Buffer,
                                      Length,
                                      '-',
                                      '-',
                                      LineStats);
        break;

    case RevLanguageFamilySemicolon:
        //
        // Languages with ";" line comments.
        //
        RevCountLinesLineCommentStyle(Buffer,
                                      Length,
                                      ';',
                                      0,
                                      LineStats);
        break;

    case RevLanguageFamilyCStyle:
    case RevLanguageFamilyUnknown:
    default:
        //
        // Languages with // and /* ... */ comments.
        //
        RevCountLinesCStyle(Buffer,
                            Length,
                            LineStats);
        break;
    }
}

/**
 * @brief This function reads and revises the specified file.
 *
 * The function reads the file contents into memory, determines the
 * appropriate comment syntax based on the file extension and language
 * mapping, counts total/blank/comment lines and updates the global
 * revision statistics.
 *
 * @param FilePath Supplies the path to the file to be revised.
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevReviseFile(
    _In_z_ PWCHAR FilePath
    )
{
    BOOL status = TRUE;
    PREVISION_RECORD revisionRecord = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER fileSize;
    PCHAR fileBuffer = NULL;
    DWORD fileBufferSize = 0;
    DWORD bytesRead = 0;
    PWCHAR fileExtension = NULL;
    PWCHAR languageOrFileType = NULL;
    REV_LANGUAGE_FAMILY languageFamily = RevLanguageFamilyUnknown;
    LINE_STATS lineStats = {0};

    if (FilePath == NULL) {
        RevLogError("FilePath is NULL.");
        status = FALSE;
        goto Exit;
    }

    //
    // Determine the file extension and mapping before reading.
    //
    fileExtension = wcsrchr(FilePath, L'.');
    if (fileExtension == NULL) {
        RevLogWarning("Failed to determine the extension for the file \"%ls\".",
                      FilePath);
        status = FALSE;
        goto Exit;
    }

    languageOrFileType = RevMapExtensionToLanguage(fileExtension);

    if (languageOrFileType == NULL) {
        //
        // Should not normally reach here because RevShouldReviseFile()
        // already filtered by extension.
        //
        RevLogWarning("No language mapping found for extension \"%ls\".",
                      fileExtension);
        status = FALSE;
        goto Exit;
    }

    languageFamily = RevGetLanguageFamily(languageOrFileType);

    //
    // Attempt to open the file.
    //
    file = CreateFile(FilePath,
                      GENERIC_READ,
                      FILE_SHARE_READ,
                      NULL,
                      OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                      NULL);

    if (file == INVALID_HANDLE_VALUE) {
        RevLogError("Failed to open the file \"%ls\". Error: %ls.",
                    FilePath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    //
    // Retrieve the size of the file
    //
    if (!GetFileSizeEx(file, &fileSize)) {
        RevLogError("Failed to retrieve the size of the file \"%ls\". "
                    "The last known error: %ls.",
                    FilePath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    //
    // Empty file, nothing to count.
    //
    if (fileSize.QuadPart == 0) {
        goto UpdateStats;
    }

    //
    // Allocate buffer for the entire file.
    // N.B. Currently, reading only ANSI files is supported, so the read buffer
    // is allocated using sizeof(CHAR).
    //
    if (fileSize.QuadPart > MAXDWORD) {
        RevLogError("File \"%ls\" is too large (%lld bytes) "
                    "for single-buffer read.",
                    FilePath,
                    fileSize.QuadPart);
        status = FALSE;
        goto Exit;
    }

    fileBufferSize = (DWORD)fileSize.QuadPart;

    fileBuffer = (PCHAR)malloc(fileBufferSize);

    if (fileBuffer == NULL) {
        RevLogError("Failed to allocate %llu bytes for file buffer",
                    fileSize.QuadPart * sizeof(CHAR));
        status = FALSE;
        goto Exit;
    }

    //
    // Read entire file contents into buffer.
    //
    if (!ReadFile(file,
                  fileBuffer,
                  fileBufferSize,
                  &bytesRead,
                  NULL)) {

        RevLogError("Failed to read file \"%ls\". Error: %ls.",
                    FilePath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    if (bytesRead == 0) {
        //
        // Nothing read, treat as empty.
        //
        goto UpdateStats;
    }

    //
    // Count lines using a language family-aware strategy.
    //
    RevCountLinesWithFamily(fileBuffer,
                            bytesRead,
                            languageFamily,
                            &lineStats);

UpdateStats:

    //
    // If there is a revision record for that language/file type in the revision
    // record list, we'll update it. If this file type hasn't been encountered,
    // we create a new node for it in the list.
    //
    revisionRecord = RevFindRevisionRecordForLanguageByExtension(fileExtension);

    if (revisionRecord == NULL) {
        RevLogError("Failed to find/initialize the revision record for the file"
                    " extension \"%ls\".",
                    fileExtension);
        status = FALSE;
        goto Exit;
    }

    //
    // Update the stats for the extension.
    //
    revisionRecord->CountOfLinesTotal += lineStats.CountOfLinesTotal;
    revisionRecord->CountOfLinesBlank += lineStats.CountOfLinesBlank;
    revisionRecord->CountOfLinesComment += lineStats.CountOfLinesComment;
    revisionRecord->CountOfFiles += 1;

    //
    // Update the stats for the entire revision.
    //
    Revision->CountOfLinesTotal += lineStats.CountOfLinesTotal;
    Revision->CountOfLinesBlank += lineStats.CountOfLinesBlank;
    Revision->CountOfLinesComment += lineStats.CountOfLinesComment;
    Revision->CountOfFiles += 1;

Exit:

    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }

    if (fileBuffer) {
        free(fileBuffer);
    }

    //
    // N.B. Don't free FilePath here, it's managed by the caller.
    //

    return status;
}

/**
 * This function outputs the revision statistics to the console.
 */
VOID
RevOutputRevisionStatistics(
    VOID
    )
{
    PLIST_ENTRY entry = NULL;
    PREVISION_RECORD revisionRecord = NULL;

    //
    // The table header.
    //

    RevPrint(L"----------------------------------------------------------------"
             "---------------------------------------------\n");
    RevPrint(L"%-25s%10s%15s%15s%15s%15s\n",
             L"File Type",
             L"Files",
             L"Blank",
             L"Comment",
             L"Code",
             L"Total");
    RevPrint(L"----------------------------------------------------------------"
             "---------------------------------------------\n");

    //
    // Iterate through the list of revision records and print statistics
    // for each one.
    //

    entry = Revision->RevisionRecordListHead.Flink;

    while (entry != &Revision->RevisionRecordListHead) {

        ULONGLONG total;
        ULONGLONG blank;
        ULONGLONG comment;
        ULONGLONG code;

        revisionRecord = CONTAINING_RECORD(entry,
                                           REVISION_RECORD,
                                           ListEntry);

        total = revisionRecord->CountOfLinesTotal;
        blank = revisionRecord->CountOfLinesBlank;
        comment = revisionRecord->CountOfLinesComment;

        if (total >= blank + comment) {
            code = total - blank - comment;
        } else {
            code = 0;
        }

        RevPrint(L"%-25s%10u%15llu%15llu%15llu%15llu\n",
                 revisionRecord->ExtensionMapping.LanguageOrFileType,
                 revisionRecord->CountOfFiles,
                 blank,
                 comment,
                 code,
                 total);

        entry = entry->Flink;
    }

    //
    // The table footer with total statistics.
    //

    RevPrint(L"----------------------------------------------------------------"
             "---------------------------------------------\n");

    {
        ULONGLONG total;
        ULONGLONG blank;
        ULONGLONG comment;
        ULONGLONG code;

        total = Revision->CountOfLinesTotal;
        blank = Revision->CountOfLinesBlank;
        comment = Revision->CountOfLinesComment;

        if (total >= blank + comment) {
            code = total - blank - comment;
        } else {
            code = 0;
        }

        RevPrint(L"%-25s%10u%15llu%15llu%15llu%15llu\n",
                 L"Total:",
                 Revision->CountOfFiles,
                 blank,
                 comment,
                 code,
                 total);
    }

    RevPrint(L"----------------------------------------------------------------"
             "---------------------------------------------\n");
}

int
wmain(
    int argc,
    wchar_t *argv[]
    )
{
    int status = 0;
    HRESULT hr = 0;
    BOOL measuringTime = TRUE;
    double resultTime = 0;
    LARGE_INTEGER startQpc = {0};
    LARGE_INTEGER endQpc = {0};
    LARGE_INTEGER frequency = {0};
    REVISION_INIT_PARAMS revisionInitParams = {0};
    LONG index = 0;

    SupportAnsi = SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
                                 ENABLE_PROCESSED_OUTPUT |
                                 ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    if (!SupportAnsi) {
        RevLogWarning("Failed to enable ANSI escape sequences.");
    }

    RevPrint(WelcomeString);

    /*
     * Process the command line arguments if any.
     */

     if (argc <= 1) {
         /*
          * The command line arguments were not passed at all, so a folder
          * selection dialog should be opened where the user can select a
          * directory to perform the revision.
          */
         RevPrint(UsageString);
         goto Exit;
     }

     if (wcscmp(argv[1], L"-help") == 0 ||
         wcscmp(argv[1], L"-h") == 0 ||
         wcscmp(argv[1], L"-?") == 0) {
         /*
          * The only command line argument passed was '-help', '-h', or '-?',
          * so show the instruction for use.
          */
         RevPrint(UsageString);
         goto Exit;
     }

    hr = PathAllocCanonicalize(argv[1],
                               PATHCCH_ALLOW_LONG_PATHS |
                               PATHCCH_FORCE_ENABLE_LONG_NAME_PROCESS,
                               &revisionInitParams.RootDirectory);
    if (FAILED(hr)) {
        RevLogError("Path normalization failed: 0x%08X", hr);
        status = -1;
        goto Exit;
    }

    if (argc > 2) {
        /*
         * It is expected that in the case of multiple command line arguments:
         *  1) The first argument is the path to the root revision directory.
         *  2) The remaining parameters are for optional revision configuration
         *     overrides.
         *
         * Process additional parameters:
         */

        for (index = 2; index < argc; ++index) {

            /*
             * -v: Sets the IsVerboseMode configuration flag to TRUE.
             */
            if (wcscmp(argv[index], L"-v") == 0) {
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
#else
    revisionInitParams.IsVerboseMode = FALSE;
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
        RevLogError("Failed to retrieve the frequency of the performance "
                    "counter.");
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

    if (measuringTime) {
        resultTime =
            (double)(endQpc.QuadPart - startQpc.QuadPart) / frequency.QuadPart;
        RevPrintEx(Cyan,
                   L"Time: %.3fs\n",
                   resultTime);
    }

    if (Revision->CountOfIgnoredFiles > 0) {
        RevPrintEx(Cyan,
                   L"\tIgnored %d files\n",
                   Revision->CountOfIgnoredFiles);
    }

#ifndef NDEBUG
    system("pause");
#endif

Exit:

    if (revisionInitParams.RootDirectory != NULL) {
        LocalFree(revisionInitParams.RootDirectory);
    }

    return status;
}
