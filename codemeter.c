/*++

Copyright (c) 2023-2025  wnstngs. All rights reserved.

Module Name:

    codemeter.c
    
Abstract:

    This module implements CodeMeter, a program for counting lines of code.

    Throughout the code, the term revision refers to the entire process, which
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

typedef struct ENUMERATION_OPTIONS {
    /**
     * If TRUE, the enumerator will recursively traverse subdirectories.
     * If FALSE, only the top-level directory is enumerated.
     */
    BOOL ShouldRecurseIntoSubdirectories;

    // BOOL FollowReparsePoints;
    // BOOL IncludeHiddenFiles;
    // BOOL ProcessDirectoriesAsItems;
} ENUMERATION_OPTIONS, *PENUMERATION_OPTIONS;

/**
 * This enumeration stores file processing backend kind for the revision engine.
 */
typedef enum REV_FILE_BACKEND_KIND {
    /**
     * Choose the best available backend for this platform.
     */
    RevFileBackendAuto = 0,

    /**
     * Process files on the enumeration thread.
     */
    RevFileBackendSynchronous,

    /**
     * Process files on a dedicated worker thread pool.
     */
    RevFileBackendThreadPool,

    // Reserved for future asynchronous backends:
    // RevFileBackendIocp,
    // RevFileBackendIoRing,

    RevFileBackendMax
} REV_FILE_BACKEND_KIND;


/**
 * This structure stores the initialization parameters of the
 * revision provided by the user at launch.
 */
typedef struct REVISION_CONFIG {
    /**
     * Path to the revision root directory. May be a directory or a single file.
     */
    _Field_z_ PWCHAR RootDirectory;

    /**
     * Indicates whether verbose revision mode is active.
     */
    BOOL IsVerboseMode;

    /**
     * Enumeration options that control how directory traversal is performed.
     */
    ENUMERATION_OPTIONS EnumerationOptions;

    /**
     * File processing backend kind. If RevFileBackendAuto is specified
     * (the default when zero-initialized), the engine will choose the
     * most appropriate backend for the current platform.
     */
    REV_FILE_BACKEND_KIND BackendKind;

    /**
     * Desired worker thread count for backends that support it
     * (e.g., thread pool). If zero, a default based on the number
     * of processors is used.
     */
    ULONG WorkerThreadCount;
} REVISION_CONFIG, *PREVISION_CONFIG;

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

//
// This is a forward declaration of the backend vtable type so that REVISION can
// refer to it via a pointer.
//
struct REVISION_FILE_BACKEND_VTABLE;

/**
 * This structure represents a single revision run over a project.
 */
typedef struct REVISION {
    /**
     * Revision initialization parameters provided by the user.
     */
    REVISION_CONFIG Config;

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

    /**
     * Effective backend kind chosen for this revision.
     */
    REV_FILE_BACKEND_KIND BackendKind;

    /**
     * Backend vtable used to schedule and execute per-file processing.
     */
    const struct REVISION_FILE_BACKEND_VTABLE *BackendVtable;

    /**
     * Opaque backend-specific context (e.g. thread pool state).
     */
    PVOID BackendContext;

    /**
     * Protects global counters and the revision record list.
     *
     * All updates to Revision->CountOf* fields and all accesses to the
     * RevisionRecordListHead must hold this lock.
     */
    CRITICAL_SECTION StatsLock;
} REVISION, *PREVISION;

/**
 * Virtual table for file processing backends.
 *
 * Backends are responsible for scheduling and executing RevReviseFile()
 * calls, possibly on a different set of threads.
 */
typedef struct REVISION_FILE_BACKEND_VTABLE {
    /**
     * @brief Initializes the backend for the given revision.
     *
     * @param Revision Supplies the revision instance.
     *
     * @return TRUE if the backend was initialized successfully,
     *         FALSE otherwise.
     */
    _Must_inspect_result_
    BOOL
    (*Initialize)(
        _Inout_ PREVISION Revision
        );

    /**
     * @brief Submits a file for processing.
     *
     * @param Revision Supplies the revision instance.
     *
     * @param FullPath Supplies the full path to the file.
     *
     * @param FindData Supplies basic metadata obtained during enumeration.
     *
     * @return TRUE if the file was accepted for processing, FALSE otherwise.
     */
    _Must_inspect_result_
    BOOL
    (*SubmitFile)(
        _Inout_ PREVISION Revision,
        _In_z_ PWCHAR FullPath,
        _In_ const WIN32_FIND_DATAW *FindData
        );

    /**
     * @brief Drains all outstanding work and shuts down the backend.
     *
     * @param Revision Supplies the revision instance.
     *
     * @return TRUE if the backend was shut down cleanly, FALSE otherwise.
     */
    _Must_inspect_result_
    BOOL
    (*DrainAndShutdown)(
        _Inout_ PREVISION Revision
        );
} REVISION_FILE_BACKEND_VTABLE, *PREVISION_FILE_BACKEND_VTABLE;

/**
 * @brief Work item used by the thread pool backend to represent a single file.
 */
typedef struct REVISION_THREAD_POOL_WORK_ITEM {

    /**
     * Pointer to the next work item in the queue.
     */
    struct REVISION_THREAD_POOL_WORK_ITEM *Next;

    /**
     * Full path to the file to revise. The string is owned by the work
     * item and must be freed when processing completes.
     */
    PWCHAR FilePath;

    /**
     * Copy of the WIN32_FIND_DATAW structure from enumeration.
     */
    WIN32_FIND_DATAW FindData;

} REVISION_THREAD_POOL_WORK_ITEM, *PREVISION_THREAD_POOL_WORK_ITEM;

/**
 * @brief Backend context for the thread pool implementation.
 *
 * The context is opaque to the rest of the engine and is stored in
 * Revision->BackendContext.
 */
typedef struct REVISION_THREAD_POOL_BACKEND_CONTEXT {

    /**
     * Array of worker thread handles.
     */
    PHANDLE WorkerThreads;

    /**
     * Number of worker threads in the pool.
     */
    ULONG WorkerThreadCount;

    /**
     * Singly linked list representing the work queue head.
     */
    PREVISION_THREAD_POOL_WORK_ITEM WorkHead;

    /**
     * Pointer to the tail of the work queue.
     */
    PREVISION_THREAD_POOL_WORK_ITEM WorkTail;

    /**
     * Protects the work queue and related state.
     */
    CRITICAL_SECTION QueueLock;

    /**
     * Signals that the work queue is not empty.
     */
    CONDITION_VARIABLE QueueNotEmpty;

    /**
     * Signals that the work queue is fully drained and no workers are active.
     */
    CONDITION_VARIABLE QueueDrained;

    /**
     * When TRUE, no new work items may be enqueued.
     */
    BOOL StopEnqueuing;

    /**
     * Number of worker threads currently processing a work item.
     */
    ULONG ActiveWorkers;

} REVISION_THREAD_POOL_BACKEND_CONTEXT, *PREVISION_THREAD_POOL_BACKEND_CONTEXT;

/**
 * This structure holds per-file line statistics.
 */
typedef struct FILE_LINE_STATS {
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
} FILE_LINE_STATS, *PFILE_LINE_STATS;

/**
 * This structure describes a view over the raw file buffer that should
 * be used for line counting.
 */
typedef struct FILE_BUFFER_VIEW {
    /**
     * Allocated buffer containing the file bytes.
     */
    PCHAR Buffer;

    /**
     * Number of bytes allocated in Buffer.
     */
    DWORD BufferSize;

    /**
     * Number of bytes actually read from the disk.
     */
    DWORD DataLength;

    /**
     * Byte offset into Buffer where the meaningful text starts
     * (e.g., after a BOM).
     */
    DWORD ContentOffset;

    /**
     * Number of bytes of the meaningful text starting at
     * Buffer + ContentOffset.
     */
    DWORD ContentLength;

    /**
     * TRUE if the content appears to be text in a supported encoding
     * (ANSI/UTF-8), FALSE if it appears to be binary or in an
     * unsupported encoding (e.g., UTF-16).
     */
    BOOL IsText;
} FILE_BUFFER_VIEW, *PFILE_BUFFER_VIEW;

/**
 * This enumeration represents logical "language families" used for
 * comment parsing.
 */
typedef enum COMMENT_STYLE_FAMILY {
    LanguageFamilyUnknown = 0,

    /**
     * C-like syntax:
     *   - Line comments:   // ...
     *   - Block comments:  /* ... *\/
     */
    LanguageFamilyCStyle,

    /**
     * Hash-style line comments:
     *   - Line comments:   # ...
     */
    LanguageFamilyHashStyle,

    /**
     * Double-dash line comments:
     *   - Line comments:   -- ...
     *     (SQL, Haskell, etc.)
     */
    LanguageFamilyDoubleDash,

    /**
     * Semicolon line comments:
     *   - Line comments:   ; ...
     *     (some Lisps, assembly dialects, etc.)
     */
    LanguageFamilySemicolon,

    /**
     * Percent-style line comments:
     *   - Line comments:   % ...
     *     (TeX/LaTeX, MATLAB, Octave, PostScript, etc.)
     */
    LanguageFamilyPercent,

    /**
     * XML-style block comments:
     *   - Block comments:  <!-- ... -->
     *     (XML, HTML, XAML, XSLT, etc.)
     */
    LanguageFamilyXmlStyle,

    /**
     * Languages with no recognized comment syntax.
     * Everything that is not whitespace is treated as code.
     */
    LanguageFamilyNoComments,

    LanguageFamilyMax
} COMMENT_STYLE_FAMILY;

/**
 * This structure maps a language name substring to a language family.
 */
typedef struct COMMENT_STYLE_MAPPING {
    _Field_z_ PWCHAR LanguageSubstring;
    COMMENT_STYLE_FAMILY LanguageFamily;
} COMMENT_STYLE_MAPPING, *PCOMMENT_STYLE_MAPPING;

/**
 * @brief Callback prototype for processing files and directories during
 *        enumeration.
 *
 * The callback is invoked once for every file or directory discovered.
 *
 * @param FullPath Supplies the full path to the file or directory.
 *
 * @param FindData Supplies the corresponding WIN32_FIND_DATAW structure.
 *
 * @param Context Supplies an optional user-defined context pointer that
 *                was passed to the enumerator.
 *
 * @return TRUE to continue enumeration, FALSE to stop.
 */
typedef BOOL (*PFILE_VISITOR)(
    _In_z_ PWCHAR FullPath,
    _In_ const WIN32_FIND_DATAW *FindData,
    _In_opt_ PVOID Context
    );

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

#define EXTENSION_HASH_BUCKET_COUNT 1024u

/**
 * Maximum length (in characters) of an extension key we support.
 */
#define MAX_EXTENSION_CCH 64


/**
 * The string to be prepended to a path to avoid the MAX_PATH limitation.
 */
#define MAX_PATH_FIX    L"\\\\?\\"

/**
 * The string to be appended to a path to indicate all of its contents.
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
    "CodeMeter.exe \"C:\\\\MyProject\" -v -backend threadpool -nr\n\n"
    "OPTIONS:\n\n"
    "\t-help, -h, -?\n"
    "\t    Print a help message and exit.\n\n"
    "\t-v\n"
    "\t    Enable verbose logging mode.\n\n"
    "\t-nr, -norecurse\n"
    "\t    Do not recurse into subdirectories; only process the\n"
    "\t    top-level directory.\n\n"
    "\t-backend <auto|sync|threadpool>\n"
    "\t    Select the file processing backend. Default is 'auto'.\n\n"
    "\t-threads <N>\n"
    "\t    Limit the number of worker threads used by the backend.\n"
    "\t    Only meaningful for the thread pool backend.\n\n";

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

/**
 * This table stores the language-to-family mappings.
 *
 * N.B. This table is intentionally small and data-driven.
 * Anything that does not match here is treated as C-style by default.
 */
const COMMENT_STYLE_MAPPING LanguageFamilyMappingTable[] = {
    {L"Python",         LanguageFamilyHashStyle},
    {L"Ruby",           LanguageFamilyHashStyle},
    {L"Perl",           LanguageFamilyHashStyle},
    {L"Shell",          LanguageFamilyHashStyle},
    {L"bash",           LanguageFamilyHashStyle},
    {L"make",           LanguageFamilyHashStyle},
    {L"Make",           LanguageFamilyHashStyle},
    {L"PowerShell",     LanguageFamilyHashStyle},
    {L"Raku",           LanguageFamilyHashStyle},
    {L"awk",            LanguageFamilyHashStyle},

    {L"SQL",            LanguageFamilyDoubleDash},
    {L"Haskell",        LanguageFamilyDoubleDash},

    {L"Lisp",           LanguageFamilySemicolon},
    {L"Scheme",         LanguageFamilySemicolon},
    {L"Assembly",       LanguageFamilySemicolon},

    //
    // Percent-style languages.
    //
    {L"TeX",            LanguageFamilyPercent},
    {L"LaTeX",          LanguageFamilyPercent},
    {L"MATLAB",         LanguageFamilyPercent},
    {L"Octave",         LanguageFamilyPercent},
    {L"PostScript",     LanguageFamilyPercent},

    //
    // XML-style block comment languages.
    // HTML is treated similarly here; script/style blocks aren't special-cased.
    //
    {L"XML",            LanguageFamilyXmlStyle},
    {L"HTML",           LanguageFamilyXmlStyle},
    {L"XHTML",          LanguageFamilyXmlStyle},
    {L"XAML",           LanguageFamilyXmlStyle},
    {L"XSLT",           LanguageFamilyXmlStyle},
};

/**
 * Mapping of file extensions that can be recognized to
 * human-readable descriptions of file types.
 */
const REVISION_RECORD_EXTENSION_MAPPING ExtensionMappingTable[] = {
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
    {L".blade.php",          L"Blade"},
    {L".build.xml",          L"Ant"},
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
    {L".riemann.config",     L"Clojure"},
    {L".hic",                L"Clojure"},
    {L".cljx",               L"Clojure"},
    {L".cljscm",             L"Clojure"},
    {L".cljs.hl",            L"Clojure"},
    {L".cl2",                L"Clojure"},
    {L".boot",               L"Clojure"},
    {L".clj",                L"Clojure"},
    {L".cljs",               L"ClojureScript"},
    {L".cljc",               L"ClojureC"},
    {L".cls",                L"Visual Basic/TeX/Apex Class"},
    {L".cmake.in",           L"CMake"},
    {L".CMakeLists.txt",     L"CMake"},
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
    {L".designer.cs",        L"C# Designer"},
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
    {L".rebar.lock",         L"Erlang"},
    {L".rebar.config.lock",  L"Erlang"},
    {L".rebar.config",       L"Erlang"},
    {L".emakefile",          L"Erlang"},
    {L".app.src",            L"Erlang"},
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
    {L".gradle.kts",         L"Gradle"},
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
    {L".haml.deface",        L"Haml"},
    {L".haml",               L"Haml"},
    {L".handlebars",         L"Handlebars"},
    {L".hbs",                L"Handlebars"},
    {L".ha",                 L"Hare"},
    {L".hxsl",               L"Haxe"},
    {L".hx",                 L"Haxe"},
    {L".HC",                 L"HolyC"},
    {L".hoon",               L"Hoon"},
    {L".xht",                L"HTML"},
    {L".html.hl",            L"HTML"},
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
    {L".buildozer.spec",     L"INI"},
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
    {L".tfstate.backup",     L"JSON"},
    {L".tfstate",            L"JSON"},
    {L".mcmod.info",         L"JSON"},
    {L".mcmeta",             L"JSON"},
    {L".json-tmlanguage",    L"JSON"},
    {L".jsonl",              L"JSON"},
    {L".har",                L"JSON"},
    {L".gltf",               L"JSON"},
    {L".geojson",            L"JSON"},
    {L".composer.lock",      L"JSON"},
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
    {L".contents.lr",        L"Markdown"},
    {L".md",                 L"Markdown"},
    {L".mc",                 L"Windows Message File"},
    {L".met",                L"Teamcenter met"},
    {L".mg",                 L"Modula3"},
    {L".mojom",              L"Mojo"},
    {L".meson.build",        L"Meson"},
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
    {L".nim.cfg",            L"Nim"},
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
    {L".makefile.pl",        L"Perl"},
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
    {L".php_cs.dist",        L"PHP"},
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
    {L".pom.xml",            L"Maven"},
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
    {L".build.bazel",        L"Python"},
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
    {L".gemfile.lock",       L"Ruby"},
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
    {L".rs.in",              L"Rust"},
    {L".rs",                 L"Rust"},
    {L".rst.txt",            L"reStructuredText"},
    {L".rest.txt",           L"reStructuredText"},
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
    {L".sproc.sql",          L"SQL Stored Procedure"},
    {L".spoc.sql",           L"SQL Stored Procedure"},
    {L".spc.sql",            L"SQL Stored Procedure"},
    {L".udf.sql",            L"SQL Stored Procedure"},
    {L".data.sql",           L"SQL Data"},
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
    {L".web.release.config", L"XML"},
    {L".web.debug.config",   L"XML"},
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
    {L".settings.stylecop",  L"XML"},
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
    {L".packages.config",    L"XML"},
    {L".osm",                L"XML"},
    {L".odd",                L"XML"},
    {L".nuspec",             L"XML"},
    {L".nuget.config",       L"XML"},
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
    {L".dll.config",         L"XML"},
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
    {L".app.config",         L"XML"},
    {L".ant",                L"XML"},
    {L".admx",               L"XML"},
    {L".adml",               L"XML"},
    {L".project",            L"XML"},
    {L".classpath",          L"XML"},
    {L".xml",                L"XML"},
    {L".XML",                L"XML"},
    {L".mxml",               L"MXML"},
    {L".xml.builder",        L"builder"},
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
    {L".yml.mysql",          L"YAML"},
    {L".yaml-tmlanguage",    L"YAML"},
    {L".syntax",             L"YAML"},
    {L".sublime-syntax",     L"YAML"},
    {L".rviz",               L"YAML"},
    {L".reek",               L"YAML"},
    {L".mir",                L"YAML"},
    {L".glide.lock",         L"YAML"},
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
    EXTENSION_HASH_BUCKET_COUNT
];

BOOL RevExtensionHashTableInitialized = FALSE;

/**
 * @brief The global revision state used throughout the entire program
 * run-time.
 */
PREVISION RevisionState = NULL;

/**
 * @brief Indicates whether ANSI escape sequences are supported.
 */
BOOL SupportAnsi;

//
// ------------------------------------------------ Internal Function Prototypes
//

_Ret_notnull_
static
PWCHAR
RevGetLastKnownWin32Error(
    VOID
    );

_Ret_maybenull_
_Must_inspect_result_
static
PWCHAR
RevStringAppend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    );

_Ret_maybenull_
_Must_inspect_result_
static
PWCHAR
RevStringPrepend(
    _In_z_ PWCHAR String1,
    _In_z_ PWCHAR String2
    );

static
FORCEINLINE
ULONG
RevHashExtensionKey(
    _In_z_ PWCHAR Extension
    );

static
VOID
RevInitializeExtensionHashTable(
    VOID
    );

_Must_inspect_result_
BOOL
RevReadFileIntoBufferView(
    _In_z_ PWCHAR FilePath,
    _Out_ PFILE_BUFFER_VIEW View
    );

_Must_inspect_result_
BOOL
RevInitializeFileBackend(
    _Inout_ PREVISION Revision
    );

_Must_inspect_result_
BOOL
RevDrainAndShutdownFileBackend(
    _Inout_ PREVISION Revision
    );

_Must_inspect_result_
BOOL
RevInitializeRevision(
    _In_ PREVISION_CONFIG InitParams
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

COMMENT_STYLE_FAMILY
RevGetLanguageFamily(
    _In_z_ PWCHAR LanguageOrFileType
    );

_Must_inspect_result_
BOOL
RevResolveExtensionForFileName(
    _In_z_ const WCHAR *FileName,
    _Out_writes_(ExtensionBufferCch) PWCHAR ExtensionBuffer,
    _In_ SIZE_T ExtensionBufferCch,
    _Outptr_result_maybenull_ PWCHAR *LanguageOrFileType
    );

_Must_inspect_result_
BOOL
RevResolveExtensionForPath(
    _In_z_ const WCHAR *FilePath,
    _Out_writes_(ExtensionBufferCch) PWCHAR ExtensionBuffer,
    _In_ SIZE_T ExtensionBufferCch,
    _Outptr_result_maybenull_ PWCHAR *LanguageOrFileType
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

FORCEINLINE
VOID
RevClassifyCompletedLine(
    _In_ BOOL SawNonWhitespace,
    _In_ BOOL SawCode,
    _In_ BOOL SawComment,
    _In_ BOOL InBlockComment,
    _Inout_ PFILE_LINE_STATS FileLineStats
    );

FORCEINLINE
VOID
RevMaybeClassifyLastLine(
    _In_ BOOL SawNonWhitespace,
    _In_ BOOL SawCode,
    _In_ BOOL SawComment,
    _In_ BOOL InBlockComment,
    _Inout_ PFILE_LINE_STATS FileLineStats
    );

VOID
RevCountLinesCStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _Inout_ PFILE_LINE_STATS FileLineStats
    );

VOID
RevCountLinesLineCommentStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _In_ CHAR FirstCommentChar,
    _In_ CHAR SecondCommentChar,
    _Inout_ PFILE_LINE_STATS FileLineStats
    );

VOID
RevCountLinesXmlStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _Inout_ PFILE_LINE_STATS FileLineStats
    );

VOID
RevCountLinesWithFamily(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _In_ COMMENT_STYLE_FAMILY LanguageFamily,
    _Inout_ PFILE_LINE_STATS FileLineStats
    );

_Must_inspect_result_
BOOL
RevEnumerateDirectoryWithVisitor(
    _In_z_ PWCHAR RootDirectoryPath,
    _In_ PFILE_VISITOR Visitor,
    _Inout_opt_ PVOID Context,
    _In_opt_ PENUMERATION_OPTIONS Options
    );

_Must_inspect_result_
BOOL
RevRevisionFileVisitor(
    _In_z_ PWCHAR FullPath,
    _In_ const WIN32_FIND_DATAW *FindData,
    _Inout_opt_ PVOID Context
    );

_Must_inspect_result_
BOOL
RevShouldReviseFile(
    _In_z_ const WCHAR *FileName
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

_Must_inspect_result_
BOOL
RevParseBackendKind(
    _In_z_ PWCHAR Value,
    _Out_ REV_FILE_BACKEND_KIND *BackendKind
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
#define RevPrint(...)                           \
    do {                                        \
        RevPrintEx(Green, __VA_ARGS__);         \
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
#define RevLogError(...)                                                       \
    do {                                                                       \
        if (!RevisionState || RevisionState->Config.IsVerboseMode) {           \
            fprintf(stderr,                                                    \
                    SupportAnsi ?                                              \
                    "\033[0;31m[ERROR]\n└───> (in %s@%d): " :                  \
                    "[ERROR]\n└───> (in %s@%d): ",                             \
                    __FUNCTION__,                                              \
                    __LINE__);                                                 \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, SupportAnsi ? "\033[0m\n" : "\n");                 \
        }                                                                      \
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
#define RevLogWarning(...)                                                     \
    do {                                                                       \
        if (!RevisionState || RevisionState->Config.IsVerboseMode) {           \
            fprintf(stdout,                                                    \
                    SupportAnsi ?                                              \
                    "\033[0;33m[WARNING]\n└───> (in %s@%d): " :                \
                    "[WARNING]\n└───> (in %s@%d): ",                           \
                    __FUNCTION__,                                              \
                    __LINE__);                                                 \
            fprintf(stdout, __VA_ARGS__);                                      \
            fprintf(stdout, SupportAnsi ? "\033[0m\n" : "\n");                 \
        }                                                                      \
    } while (0)

/**
 * @brief This function retrieves the calling thread's last-error code
 * value and translates it into its corresponding error message.
 *
 * @return A pointer to the error message string on success,
 * or NULL on failure.
 */
_Ret_notnull_
PWCHAR
static
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
static
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
static
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
static
FORCEINLINE
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
static
VOID
RevInitializeExtensionHashTable(
    VOID
    )
{
    ULONG i;

    if (RevExtensionHashTableInitialized) {
        return;
    }

    ZeroMemory((PVOID)RevExtensionHashTable, sizeof(RevExtensionHashTable));

    for (i = 0; i < ARRAYSIZE(ExtensionMappingTable); i += 1) {

        const REVISION_RECORD_EXTENSION_MAPPING *entry =
            &ExtensionMappingTable[i];

        ULONG hash = RevHashExtensionKey(entry->Extension);
        ULONG bucket = hash & (EXTENSION_HASH_BUCKET_COUNT - 1);

        ULONG probes = 0;

        while (probes < EXTENSION_HASH_BUCKET_COUNT) {

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

            bucket = (bucket + 1) & (EXTENSION_HASH_BUCKET_COUNT - 1);

            probes += 1;
        }
    }

    RevExtensionHashTableInitialized = TRUE;
}

/**
 * @brief Read the entire file into memory and construct a buffer view
 *        suitable for line counting.
 *
 * @param FilePath Supplies the path to the file to be read.
 *
 * @param View Receives the buffer view description.
 *
 * @return TRUE if succeeded, FALSE if failed.
 *
 * @remarks On success, View is initialized and the caller
 *          is responsible for freeing View->Buffer using free().
 */
_Must_inspect_result_
BOOL
RevReadFileIntoBufferView(
    _In_z_ PWCHAR FilePath,
    _Out_ PFILE_BUFFER_VIEW View
    )
{
    BOOL status = TRUE;
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER fileSize;
    DWORD bytesRead = 0;

    if (FilePath == NULL || View == NULL) {
        RevLogError("RevReadFileIntoBufferView received invalid parameter/-s.");
        return FALSE;
    }

    ZeroMemory(View, sizeof(*View));

    //
    // Open the file for read-only access.
    //
    file = CreateFileW(FilePath,
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
    // Obtain its size.
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
    // Empty file: nothing to read, but this is not an error.
    //
    if (fileSize.QuadPart == 0) {
        View->Buffer = NULL;
        View->BufferSize = 0;
        View->DataLength = 0;
        View->ContentOffset = 0;
        View->ContentLength = 0;
        View->IsText = TRUE;
        goto Exit;
    }

    //
    // For now, we do not support reading files larger than 4 GiB into a
    // single buffer. They are simply skipped for counting purposes.
    //
    if (fileSize.QuadPart > MAXDWORD) {
        RevLogWarning("Skipping file \"%ls\" because its size (%lld bytes) "
                      "exceeds the supported limit.",
                      FilePath,
                      fileSize.QuadPart);
        View->IsText = FALSE;
        goto Exit;
    }

    View->BufferSize = (DWORD)fileSize.QuadPart;
    View->Buffer = (PCHAR)malloc(View->BufferSize);

    if (View->Buffer == NULL) {
        RevLogError("Failed to allocate %llu bytes for file buffer.",
                    fileSize.QuadPart * sizeof(CHAR));
        status = FALSE;
        goto Exit;
    }

    //
    // Read the entire content into a heap buffer.
    //
    if (!ReadFile(file,
                  View->Buffer,
                  View->BufferSize,
                  &bytesRead,
                  NULL)) {

        RevLogError("Failed to read the file \"%ls\". Error: %ls.",
                    FilePath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    View->DataLength = bytesRead;

    if (bytesRead == 0) {
        //
        // Treat as empty file.
        //
        View->ContentOffset = 0;
        View->ContentLength = 0;
        View->IsText = TRUE;
        goto Exit;
    }

    //
    // BOM / encoding detection.
    //
    {
        DWORD offset = 0;

        if (bytesRead >= 3 &&
            (UCHAR)View->Buffer[0] == 0xEF &&
            (UCHAR)View->Buffer[1] == 0xBB &&
            (UCHAR)View->Buffer[2] == 0xBF) {

            //
            // UTF-8 BOM.
            //
            offset = 3;

        } else if (bytesRead >= 2) {

            UCHAR b0 = (UCHAR)View->Buffer[0];
            UCHAR b1 = (UCHAR)View->Buffer[1];

            if ((b0 == 0xFF && b1 == 0xFE) ||
                (b0 == 0xFE && b1 == 0xFF)) {

                //
                // UTF-16 BOM: currently unsupported for parsing; we do not
                // attempt to convert to UTF-8 here.
                //
                RevLogWarning("File \"%ls\" appears to be UTF-16 encoded; "
                              "skipping line counting for this file.",
                              FilePath);

                View->IsText = FALSE;
                View->ContentOffset = 0;
                View->ContentLength = 0;
                goto Exit;
            }
        }

        View->ContentOffset = offset;
        View->ContentLength =
            (bytesRead > offset) ? (bytesRead - offset) : 0;
    }

    //
    // Simple binary heuristic: if the first few KBs contain NUL bytes,
    // assume this is a binary file and skip counting.
    //
    if (View->ContentLength > 0) {

        DWORD inspectLength;

        if (View->ContentLength < 4096) {
            inspectLength = View->ContentLength;
        } else {
            inspectLength = 4096;
        }

        DWORD index;

        for (index = 0; index < inspectLength; index += 1) {

            if (View->Buffer[View->ContentOffset + index] == '\0') {

                RevLogWarning("File \"%ls\" appears to be binary; "
                              "skipping line counting for this file.",
                              FilePath);

                View->IsText = FALSE;
                View->ContentLength = 0;

                goto Exit;
            }
        }
    }

    View->IsText = TRUE;

Exit:

    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }

    if (!status) {
        if (View->Buffer != NULL) {
            free(View->Buffer);
        }
        ZeroMemory(View, sizeof(*View));
    }

    return status;
}

//
// Synchronous backend.
//
// All files are revised on the calling thread.
//

/**
 * @brief Initializes the synchronous file backend.
 *
 * In this mode, all RevReviseFile() calls are executed on the caller
 * thread. No additional resources are required.
 *
 * @param Revision Supplies the revision instance.
 *
 * @return TRUE (always).
 */
_Must_inspect_result_
static
BOOL
RevSynchronousBackendInitialize(
    _Inout_ PREVISION Revision
    )
{
    UNREFERENCED_PARAMETER(Revision);
    return TRUE;
}

/**
 * @brief Submits a single file to the synchronous backend.
 *
 * In this mode, the file is processed immediately by calling
 * RevReviseFile() on the caller thread.
 */
_Must_inspect_result_
static
BOOL
RevSynchronousBackendSubmitFile(
    _Inout_ PREVISION Revision,
    _In_z_ PWCHAR FullPath,
    _In_ const WIN32_FIND_DATAW *FindData
    )
{
    UNREFERENCED_PARAMETER(FindData);

    if (Revision == NULL || FullPath == NULL) {
        return FALSE;
    }

    //
    // In the synchronous backend, we simply revise the file immediately
    // on the caller thread.
    //
    return RevReviseFile(FullPath);
}

/**
 * @brief Drains and shuts down the synchronous backend.
 *
 * There is nothing to drain for the synchronous backend. This function
 * is provided for uniformity with other backends.
 */
_Must_inspect_result_
static
BOOL
RevSynchronousBackendDrainAndShutdown(
    _Inout_ PREVISION Revision
    )
{
    UNREFERENCED_PARAMETER(Revision);

    //
    // No outstanding work to drain for synchronous backend.
    //
    return TRUE;
}

/**
 * Static vtable instance for the synchronous backend.
 */
static const REVISION_FILE_BACKEND_VTABLE RevSynchronousBackendVtable = {
    RevSynchronousBackendInitialize,
    RevSynchronousBackendSubmitFile,
    RevSynchronousBackendDrainAndShutdown
};

//
// Thread pool backend.
//

/**
 * Worker thread procedure for the thread pool backend.
 *
 * @param Parameter Supplies a pointer to a thread pool backend context.
 *
 * @return Always returns 0.
 */
DWORD
WINAPI
RevThreadPoolWorkerThread(
    _In_ LPVOID Parameter
    )
{
    //
    // Global revision pointer published during RevInitializeRevision().
    //
    PREVISION revision = RevisionState;
    PREVISION_THREAD_POOL_BACKEND_CONTEXT context = Parameter;

    if (context == NULL) {
        return 0;
    }

    for (;;) {

        PREVISION_THREAD_POOL_WORK_ITEM workItem = NULL;

        //
        // Pull the next work item from the queue, waiting if necessary.
        //
        EnterCriticalSection(&context->QueueLock);

        while (context->WorkHead == NULL && !context->StopEnqueuing) {

            SleepConditionVariableCS(&context->QueueNotEmpty,
                                     &context->QueueLock,
                                     INFINITE);
        }

        //
        // No work and enqueuing has stopped, so it's time to exit.
        //
        if (context->WorkHead == NULL && context->StopEnqueuing) {

            LeaveCriticalSection(&context->QueueLock);
            break;
        }

        //
        // Pop one item from the head of the queue.
        //
        workItem = context->WorkHead;
        context->WorkHead = workItem->Next;

        if (context->WorkHead == NULL) {
            context->WorkTail = NULL;
        }

        context->ActiveWorkers += 1;

        LeaveCriticalSection(&context->QueueLock);

        //
        // Process the file outside the lock.
        //
        if (revision != NULL && workItem->FilePath != NULL) {
            (void)RevReviseFile(workItem->FilePath);
        }

        //
        // Free work item resources.
        //
        if (workItem->FilePath != NULL) {
            free(workItem->FilePath);
        }
        free(workItem);

        //
        // Update worker count and potentially signal that the queue
        // has been fully drained.
        //
        EnterCriticalSection(&context->QueueLock);

        context->ActiveWorkers -= 1;

        if (context->StopEnqueuing &&
            context->WorkHead == NULL &&
            context->ActiveWorkers == 0) {

            WakeAllConditionVariable(&context->QueueDrained);
        }

        LeaveCriticalSection(&context->QueueLock);
    }

    return 0;
}

/**
 * Initializes the thread pool backend for the given revision.
 *
 * @param Revision Supplies the revision instance.
 *
 * @return TRUE if the backend was initialized successfully, FALSE otherwise.
 */
_Must_inspect_result_
static
BOOL
RevThreadPoolBackendInitialize(
    _Inout_ PREVISION Revision
    )
{
    SYSTEM_INFO systemInfo;
    ULONG desiredThreads;
    ULONG index;
    PREVISION_THREAD_POOL_BACKEND_CONTEXT context = NULL;

    if (Revision == NULL) {
        return FALSE;
    }

    //
    // Decide how many worker threads to use. If no override was specified
    // in the revision config, fall back to the number of logical processors.
    //
    GetSystemInfo(&systemInfo);

    desiredThreads = Revision->Config.WorkerThreadCount;
    if (desiredThreads == 0) {
        desiredThreads = systemInfo.dwNumberOfProcessors;
        if (desiredThreads == 0) {
            desiredThreads = 1;
        }
    }

    //
    // Allocate and zero the backend context.
    //
    context = (PREVISION_THREAD_POOL_BACKEND_CONTEXT)malloc(
        sizeof(REVISION_THREAD_POOL_BACKEND_CONTEXT));

    if (context == NULL) {
        RevLogError("Failed to allocate thread pool backend context.");
        return FALSE;
    }

    ZeroMemory(context, sizeof(*context));

    context->WorkerThreadCount = desiredThreads;
    context->WorkerThreads = (PHANDLE)malloc(sizeof(HANDLE) * desiredThreads);

    if (context->WorkerThreads == NULL) {
        RevLogError("Failed to allocate thread handle array for %lu threads.",
                    desiredThreads);
        free(context);
        return FALSE;
    }

    ZeroMemory(context->WorkerThreads, sizeof(HANDLE) * desiredThreads);

    InitializeCriticalSection(&context->QueueLock);
    InitializeConditionVariable(&context->QueueNotEmpty);
    InitializeConditionVariable(&context->QueueDrained);

    context->WorkHead = NULL;
    context->WorkTail = NULL;
    context->StopEnqueuing = FALSE;
    context->ActiveWorkers = 0;

    //
    // Spawn worker threads, each of which will drain the shared queue.
    //
    for (index = 0; index < desiredThreads; index += 1) {

        HANDLE threadHandle = CreateThread(NULL,
                                           0,
                                           RevThreadPoolWorkerThread,
                                           context,
                                           0,
                                           NULL);

        if (threadHandle == NULL) {

            RevLogError("Failed to create worker thread %lu. Error: %ls.",
                        index,
                        RevGetLastKnownWin32Error());

            //
            // Stop creating new threads; we'll shut down below.
            //
            context->StopEnqueuing = TRUE;
            context->WorkerThreadCount = index;
            break;
        }

        context->WorkerThreads[index] = threadHandle;
    }

    //
    // If no worker threads were successfully created,
    // tear down the context.
    //
    if (context->WorkerThreadCount == 0) {

        DeleteCriticalSection(&context->QueueLock);
        free(context->WorkerThreads);
        free(context);

        return FALSE;
    }

    Revision->BackendContext = context;

    return TRUE;
}

/**
 * Submits a single file to the thread pool backend.
 */
_Must_inspect_result_
static
BOOL
RevThreadPoolBackendSubmitFile(
    _Inout_ PREVISION Revision,
    _In_z_ PWCHAR FullPath,
    _In_ const WIN32_FIND_DATAW *FindData
    )
{
    PREVISION_THREAD_POOL_BACKEND_CONTEXT context = NULL;
    PREVISION_THREAD_POOL_WORK_ITEM workItem = NULL;
    SIZE_T pathLengthInChars = 0;
    PWCHAR pathCopy = NULL;

    if (Revision == NULL || FullPath == NULL) {
        return FALSE;
    }

    context = (PREVISION_THREAD_POOL_BACKEND_CONTEXT)Revision->BackendContext;

    if (context == NULL) {
        return FALSE;
    }

    //
    // Copy the path so that the work item owns its own stable string.
    //
    pathLengthInChars = wcslen(FullPath) + 1;
    pathCopy = (PWCHAR)malloc(pathLengthInChars * sizeof(WCHAR));

    if (pathCopy == NULL) {
        RevLogError("Failed to allocate path copy for \"%ls\".", FullPath);
        return FALSE;
    }

    memcpy(pathCopy,
           FullPath,
           pathLengthInChars * sizeof(WCHAR));

    //
    // Allocate and initialize the work item container.
    //
    workItem = (PREVISION_THREAD_POOL_WORK_ITEM)malloc(
        sizeof(REVISION_THREAD_POOL_WORK_ITEM));

    if (workItem == NULL) {
        RevLogError("Failed to allocate thread pool work item.");
        free(pathCopy);
        return FALSE;
    }

    ZeroMemory(workItem, sizeof(*workItem));

    workItem->FilePath = pathCopy;

    if (FindData != NULL) {
        workItem->FindData = *FindData;
    }

    workItem->Next = NULL;

    //
    // Enqueue the work item at the tail of the queue.
    //
    EnterCriticalSection(&context->QueueLock);

    if (context->StopEnqueuing) {

        //
        // Backend is shutting down; reject new work.
        //
        LeaveCriticalSection(&context->QueueLock);
        free(pathCopy);
        free(workItem);

        return FALSE;
    }

    if (context->WorkTail == NULL) {
        context->WorkHead = workItem;
        context->WorkTail = workItem;

    } else {
        context->WorkTail->Next = workItem;
        context->WorkTail = workItem;
    }

    //
    // Signal one waiting worker that work is now available.
    //
    WakeConditionVariable(&context->QueueNotEmpty);

    LeaveCriticalSection(&context->QueueLock);

    return TRUE;
}

/**
 * @brief Drains and shuts down the thread pool backend.
 *
 * This function prevents further enqueues, waits for the work queue
 * to be fully drained, joins all worker threads, and cleans up the
 * backend context.
 *
 * @param Revision Supplies the revision instance.
 *
 * @return TRUE if the backend was shut down cleanly, FALSE otherwise.
 */
_Must_inspect_result_
static
BOOL
RevThreadPoolBackendDrainAndShutdown(
    _Inout_ PREVISION Revision
    )
{
    PREVISION_THREAD_POOL_BACKEND_CONTEXT Context = NULL;
    ULONG Index;

    if (Revision == NULL) {
        return FALSE;
    }

    Context = (PREVISION_THREAD_POOL_BACKEND_CONTEXT)Revision->BackendContext;

    if (Context == NULL) {
        //
        // Backend was never initialized; nothing to shut down.
        //
        return TRUE;
    }

    //
    // Stop accepting new work items and wake all workers so that they can
    // finish draining the queue.
    //
    EnterCriticalSection(&Context->QueueLock);

    Context->StopEnqueuing = TRUE;
    WakeAllConditionVariable(&Context->QueueNotEmpty);

    //
    // Wait until the queue is empty and no worker is actively processing
    // a work item.
    //
    while (Context->WorkHead != NULL || 
           Context->ActiveWorkers != 0) {

        SleepConditionVariableCS(&Context->QueueDrained,
                                 &Context->QueueLock,
                                 INFINITE);
    }

    LeaveCriticalSection(&Context->QueueLock);

    //
    // Join all worker threads and close their handles. The context
    // initializer must ensure that WorkerThreadCount and WorkerThreads
    // are consistent (either both valid or both zero/NULL).
    //
    if (Context->WorkerThreadCount > 0 && 
        Context->WorkerThreads != NULL) {

        WaitForMultipleObjects(Context->WorkerThreadCount,
                               Context->WorkerThreads,
                               TRUE,
                               INFINITE);

        for (Index = 0; Index < Context->WorkerThreadCount; Index += 1) {

            HANDLE ThreadHandle = Context->WorkerThreads[Index];

            //
            // ThreadHandle may legitimately be NULL if thread creation
            // failed after the array was zeroed during initialization.
            //
            if (ThreadHandle != NULL) {
                CloseHandle(ThreadHandle);
            }
        }
    }

    //
    // Free any remaining work items in the queue. In normal operation
    // there should be none at this point, because we waited for the
    // queue to be drained.
    //
    EnterCriticalSection(&Context->QueueLock);

    while (Context->WorkHead != NULL) {

        PREVISION_THREAD_POOL_WORK_ITEM Item = Context->WorkHead;

        //
        // Advance the head pointer before touching the current item to
        // avoid use-after-free bugs.
        //
        Context->WorkHead = Item->Next;

        //
        // Break the linkage to make debugging easier and avoid accidental
        // double-free chains if the Item pointer is misused after this.
        //
        Item->Next = NULL;

        //
        // FilePath is owned by the work item and is either NULL or a heap
        // allocation performed by RevThreadPoolBackendSubmitFile().
        //
        if (Item->FilePath != NULL) {
            free(Item->FilePath);
            Item->FilePath = NULL;
        }

        free(Item);
    }

    Context->WorkTail = NULL;

    LeaveCriticalSection(&Context->QueueLock);

    //
    // Destroy the queue lock and free the thread handle array and the
    // backend context itself.
    //
    DeleteCriticalSection(&Context->QueueLock);

    if (Context->WorkerThreads != NULL) {
        free(Context->WorkerThreads);
        Context->WorkerThreads = NULL;
    }

    free(Context);

    Revision->BackendContext = NULL;

    return TRUE;
}

/**
 * Static vtable instance for the thread pool backend.
 */
static const REVISION_FILE_BACKEND_VTABLE RevThreadPoolBackendVtable = {
    RevThreadPoolBackendInitialize,
    RevThreadPoolBackendSubmitFile,
    RevThreadPoolBackendDrainAndShutdown
};

/**
 * @brief Initializes the file processing backend for the current revision.
 *
 * This function selects an appropriate backend based on the revision
 * configuration and initializes it. Currently, the default selection
 * (RevFileBackendAuto) maps to the thread pool backend.
 *
 * @param Revision Supplies the revision instance.
 *
 * @return TRUE if the backend was initialized successfully, FALSE otherwise.
 */
_Must_inspect_result_
BOOL
RevInitializeFileBackend(
    _Inout_ PREVISION Revision
    )
{
    REV_FILE_BACKEND_KIND requested;
    REV_FILE_BACKEND_KIND effective;
    BOOL result = FALSE;

    if (Revision == NULL) {
        return FALSE;
    }

    requested = Revision->Config.BackendKind;

    if (requested == RevFileBackendAuto) {
        effective = RevFileBackendThreadPool;
    } else {
        effective = requested;
    }

    switch (effective) {

    case RevFileBackendSynchronous:
        Revision->BackendVtable = &RevSynchronousBackendVtable;
        result = Revision->BackendVtable->Initialize(Revision);
        break;

    case RevFileBackendThreadPool:
        Revision->BackendVtable = &RevThreadPoolBackendVtable;
        result = Revision->BackendVtable->Initialize(Revision);
        //
        // Fall back from thread pool to synchronous when thread pool
        // initialization fails.
        //
        if (!result) {
            RevLogWarning("Thread pool backend failed to initialize, "
                          "falling back to synchronous backend.");
            Revision->BackendVtable = &RevSynchronousBackendVtable;
            result = Revision->BackendVtable->Initialize(Revision);
            effective = RevFileBackendSynchronous;
        }
        break;

    // case RevFileBackendIocp:
    // case RevFileBackendIoRing:
    default:
        RevLogWarning("Requested backend kind (%d) is not implemented yet. "
                      "Falling back to synchronous backend.",
                      (LONG)effective);
        Revision->BackendVtable = &RevSynchronousBackendVtable;
        result = Revision->BackendVtable->Initialize(Revision);
        effective = RevFileBackendSynchronous;
        break;
    }

    if (!result) {
        Revision->BackendVtable = NULL;
        Revision->BackendKind = RevFileBackendAuto;
        return FALSE;
    }

    Revision->BackendKind = effective;

    return TRUE;
}

/**
 * @brief Drains and shuts down the file processing backend for the current
 *        revision.
 *
 * @param Revision Supplies the revision instance.
 *
 * @return TRUE if the backend was shut down cleanly, FALSE otherwise.
 */
_Must_inspect_result_
BOOL
RevDrainAndShutdownFileBackend(
    _Inout_ PREVISION Revision
    )
{
    if (Revision == NULL || Revision->BackendVtable == NULL) {
        return TRUE;
    }

    return Revision->BackendVtable->DrainAndShutdown(Revision);
}

/**
 * @brief This function is responsible for initializing the revision
 * system.
 *
 * @param InitParams Supplies the revision initialization parameters.
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevInitializeRevision(
    _In_ PREVISION_CONFIG InitParams
    )
{
    PREVISION revision = NULL;

    if (InitParams == NULL || InitParams->RootDirectory == NULL) {
        RevLogError("RevInitializeRevision received invalid Config.");
        return FALSE;
    }

    revision = (PREVISION)malloc(sizeof(REVISION));
    if (revision == NULL) {
        RevLogError("Failed to allocate memory for REVISION.");
        return FALSE;
    }

    ZeroMemory(revision, sizeof(REVISION));

    revision->Config = *InitParams;

    RevInitializeListHead(&revision->RevisionRecordListHead);

    revision->CountOfLinesTotal = 0;
    revision->CountOfLinesBlank = 0;
    revision->CountOfLinesComment = 0;
    revision->CountOfFiles = 0;
    revision->CountOfIgnoredFiles = 0;

    //
    // The effective backend will be selected in RevInitializeFileBackend().
    //
    revision->BackendKind = InitParams->BackendKind;
    revision->BackendVtable = NULL;
    revision->BackendContext = NULL;

    InitializeCriticalSection(&revision->StatsLock);

    //
    // Publish the global revision pointer.
    //
    RevisionState = revision;

    //
    // Initialize the extension hash table.
    //
    RevInitializeExtensionHashTable();

    return TRUE;
}

/**
 * @brief This function is responsible for starting the revision system and
 *        initializing the file processing backend.
 *
 * It ensures that the system has been initialized correctly before
 * proceeding with its operations.
 *
 * The root path in Revision->Config.RootDirectory may be either:
 *  - a directory path, in which case a directory enumeration is performed; or
 *  - a path to a single file, in which case only that file is revised.
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevStartRevision(
    VOID
    )
{
    BOOL status = TRUE;
    PREVISION revision = RevisionState;
    DWORD attributes;
    PWCHAR rootPath;

    if (revision == NULL) {
        RevLogError("RevStartRevision called before RevInitializeRevision.");
        return FALSE;
    }

    rootPath = revision->Config.RootDirectory;
    if (rootPath == NULL) {
        RevLogError("Revision config does not contain a RootDirectory.");
        return FALSE;
    }

    if (!RevInitializeFileBackend(revision)) {
        RevLogError("Failed to initialize the file processing backend.");
        return FALSE;
    }

    attributes = GetFileAttributesW(rootPath);
    if (attributes == INVALID_FILE_ATTRIBUTES) {

        RevLogError("Failed to retrieve attributes for \"%ls\". Error: %ls.",
                    rootPath,
                    RevGetLastKnownWin32Error());

        status = FALSE;
        goto Exit;
    }

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {

        WIN32_FIND_DATAW findData;

        const WCHAR *fileName = rootPath;
        const WCHAR *lastBackslash = wcsrchr(rootPath, L'\\');
        const WCHAR *lastForwardSlash = wcsrchr(rootPath, L'/');
        const WCHAR *separator = lastBackslash;

        ZeroMemory(&findData, sizeof(findData));
        findData.dwFileAttributes = attributes;

        if (lastForwardSlash != NULL &&
            (separator == NULL || lastForwardSlash > separator)) {

            separator = lastForwardSlash;
        }

        if (separator != NULL && separator[1] != L'\0') {
            fileName = separator + 1;
        }

        (void)wcsncpy_s(findData.cFileName,
                        ARRAYSIZE(findData.cFileName),
                        fileName,
                        _TRUNCATE);

        if (!RevShouldReviseFile(findData.cFileName)) {

            EnterCriticalSection(&revision->StatsLock);
            revision->CountOfIgnoredFiles += 1;
            LeaveCriticalSection(&revision->StatsLock);

        } else {

            if (revision->BackendVtable == NULL) {
                RevLogError("File backend is not available.");
                status = FALSE;
                goto Exit;
            }

            if (!revision->BackendVtable->SubmitFile(revision,
                                                     rootPath,
                                                     &findData)) {

                RevLogError("File backend failed to submit \"%ls\".",
                            rootPath);
                status = FALSE;

                goto Exit;
            }
        }

    } else {

        ENUMERATION_OPTIONS options = revision->Config.EnumerationOptions;

        if (!RevEnumerateDirectoryWithVisitor(rootPath,
                                              RevRevisionFileVisitor,
                                              revision,
                                              &options)) {

            RevLogError("Failed to start enumeration in directory \"%ls\".",
                        rootPath);
            status = FALSE;

            goto Exit;
        }
    }

Exit:

    if (!RevDrainAndShutdownFileBackend(revision)) {
        RevLogError("Failed to drain and shutdown file backend.");
        status = FALSE;
    }

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
 * @brief Resolve the canonical extension key and language for a bare file name.
 *
 * This function interprets the supplied file name according to the
 * ExtensionMappingTable semantics:
 *
 *   1. It first tries a "whole-name" key of the form ".<FileName>".
 *      This enables entries such as ".CMakeLists.txt", ".Dockerfile",
 *      ".Makefile", etc., to match file names directly.
 *
 *   2. If that fails, it scans the file name from the first '.' towards
 *      the end, and for each dot considers the suffix starting at that
 *      dot as a candidate extension (e.g., ".rst.txt", ".txt").
 *      The first candidate that maps to a known language/file type is
 *      selected. This naturally prefers more specific multi-dot
 *      extensions over shorter ones.
 *
 * @param FileName Supplies the file name (no path).
 *
 * @param ExtensionBuffer Receives the canonical extension key (e.g. ".c",
 *        ".CMakeLists.txt", ".rst.txt").
 *
 * @param ExtensionBufferCch Supplies the size of ExtensionBuffer in WCHARs.
 *
 * @param LanguageOrFileType Receives a pointer to the mapped language or
 *        file type string if successful; may be NULL if caller is not
 *        interested in the language.
 *
 * @return TRUE if a known extension/language was resolved, FALSE otherwise.
 */
_Must_inspect_result_
BOOL
RevResolveExtensionForFileName(
    _In_z_ const WCHAR *FileName,
    _Out_writes_(ExtensionBufferCch) PWCHAR ExtensionBuffer,
    _In_ SIZE_T ExtensionBufferCch,
    _Outptr_result_maybenull_ PWCHAR *LanguageOrFileType
    )
{
    SIZE_T length = 0;
    PWCHAR language = NULL;
    const WCHAR *scan = NULL;
    const WCHAR *dot = NULL;

    if (LanguageOrFileType != NULL) {
        *LanguageOrFileType = NULL;
    }

    if (FileName == NULL ||
        ExtensionBuffer == NULL ||
        ExtensionBufferCch < 4) {

        return FALSE;
    }

    ExtensionBuffer[0] = L'\0';

    length = wcslen(FileName);
    if (length == 0) {
        return FALSE;
    }

    //
    // Step 1: try a whole-name key ".<FileName>".
    // This allows entries like ".CMakeLists.txt", ".Dockerfile", ".Makefile"
    // to match directly.
    //
    if (length + 2 <= ExtensionBufferCch) {

        ExtensionBuffer[0] = L'.';

        memcpy(ExtensionBuffer + 1,
               FileName,
               length * sizeof(WCHAR));

        ExtensionBuffer[length + 1] = L'\0';

        language = RevMapExtensionToLanguage(ExtensionBuffer);

        if (language != NULL) {

            if (LanguageOrFileType != NULL) {
                *LanguageOrFileType = language;
            }

            return TRUE;
        }
    }

    //
    // Step 2: scan for multi-dot and single-dot suffixes.
    // We start at the first '.' and move forward. The first suffix that
    // maps to a known language wins (the longest match by construction).
    //
    scan = FileName;
    dot = wcschr(scan, L'.');

    while (dot != NULL) {

        SIZE_T suffixLength = length - (SIZE_T)(dot - FileName);

        if (suffixLength + 1 <= ExtensionBufferCch) {

            memcpy(ExtensionBuffer,
                   dot,
                   suffixLength * sizeof(WCHAR));

            ExtensionBuffer[suffixLength] = L'\0';

            language = RevMapExtensionToLanguage(ExtensionBuffer);

            if (language != NULL) {

                if (LanguageOrFileType != NULL) {
                    *LanguageOrFileType = language;
                }

                return TRUE;
            }
        }

        scan = dot + 1;
        dot = wcschr(scan, L'.');
    }

    //
    // No known extension mapping.
    //
    ExtensionBuffer[0] = L'\0';

    return FALSE;
}

/**
 * @brief Resolve the canonical extension key and language for a full file path.
 *
 * This helper extracts the last path component from FilePath and then calls
 * RevResolveExtensionForFileName(). It understands both '\\' and '/' as
 * directory separators.
 *
 * @param FilePath Supplies the full path to the file.
 *
 * @param ExtensionBuffer Receives the canonical extension key.
 *
 * @param ExtensionBufferCch Supplies the size of ExtensionBuffer in WCHARs.
 *
 * @param LanguageOrFileType Receives a pointer to the mapped language or
 *        file type string if successful; may be NULL if caller is not
 *        interested in the language.
 *
 * @return TRUE if a known extension/language was resolved, FALSE otherwise.
 */
_Must_inspect_result_
BOOL
RevResolveExtensionForPath(
    _In_z_ const WCHAR *FilePath,
    _Out_writes_(ExtensionBufferCch) PWCHAR ExtensionBuffer,
    _In_ SIZE_T ExtensionBufferCch,
    _Outptr_result_maybenull_ PWCHAR *LanguageOrFileType
    )
{
    const WCHAR *fileName = FilePath;
    const WCHAR *lastBackslash = NULL;
    const WCHAR *lastSlash = NULL;
    const WCHAR *separator = NULL;

    if (FilePath == NULL) {
        return FALSE;
    }

    lastBackslash = wcsrchr(FilePath, L'\\');
    lastSlash = wcsrchr(FilePath, L'/');

    separator = lastBackslash;

    if (lastSlash != NULL && (separator == NULL || lastSlash > separator)) {
        separator = lastSlash;
    }

    if (separator != NULL && separator[1] != L'\0') {
        fileName = separator + 1;
    }

    return RevResolveExtensionForFileName(fileName,
                                          ExtensionBuffer,
                                          ExtensionBufferCch,
                                          LanguageOrFileType);
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
    bucket = hash & (EXTENSION_HASH_BUCKET_COUNT - 1);
    probes = 0;

    while (probes < EXTENSION_HASH_BUCKET_COUNT) {

        const REVISION_RECORD_EXTENSION_MAPPING *entry =
            RevExtensionHashTable[bucket];

        if (entry == NULL) {
            break;
        }

        if (_wcsicmp(entry->Extension, Extension) == 0) {
            return entry->LanguageOrFileType;
        }

        bucket = (bucket + 1) & (EXTENSION_HASH_BUCKET_COUNT - 1);
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

    assert(RevisionState != NULL);

    entry = RevisionState->RevisionRecordListHead.Flink;

    while (entry != &RevisionState->RevisionRecordListHead) {
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
    RevInsertTailList(&RevisionState->RevisionRecordListHead,
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
COMMENT_STYLE_FAMILY
RevGetLanguageFamily(
    _In_z_ PWCHAR LanguageOrFileType
    )
{
    LONG index;

    if (LanguageOrFileType == NULL) {
        return LanguageFamilyCStyle;
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
    return LanguageFamilyCStyle;
}

/**
 * @brief Generic directory enumerator that traverses files and optional
 *        subdirectories and invokes a visitor callback for each entry.
 *
 * @param RootDirectoryPath Supplies the root directory path from which
 *        enumeration should begin (without wildcard characters).
 *
 * @param Visitor Supplies a callback invoked for each discovered file or
 *                directory.
 *
 * @param Context Supplies an optional user-defined context pointer.
 *
 * @param Options Supplies optional enumeration options. If NULL, default
 *                options are used (recursive traversal).
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevEnumerateDirectoryWithVisitor(
    _In_z_ PWCHAR RootDirectoryPath,
    _In_ PFILE_VISITOR Visitor,
    _Inout_opt_ PVOID Context,
    _In_opt_ PENUMERATION_OPTIONS Options
    )
{
    BOOL status = TRUE;
    HANDLE findFile = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW findFileData;
    PWCHAR searchPath = NULL;
    SIZE_T rootLength = 0;
    BOOL hasTrailingSeparator = FALSE;
    BOOL shouldRecurse = TRUE;

    //
    // Validate parameters.
    //
    if (RootDirectoryPath == NULL || Visitor == NULL) {
        RevLogError("Invalid parameter/-s.");
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
    // RootDirectoryPath is expected to be a plain directory path without
    // wildcard characters. This keeps path handling simple and predictable.
    //
    if (wcschr(RootDirectoryPath, L'*') != NULL) {
        RevLogError("The root directory path \"%ls\" must not contain "
                    "wildcard characters.",
                    RootDirectoryPath);
        status = FALSE;
        goto Exit;
    }

    if (Options != NULL) {
        shouldRecurse = Options->ShouldRecurseIntoSubdirectories;
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
    // Start enumeration.
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
        // Build the full path to the current entry:
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
            SIZE_T subPathLength =
                rootLength + separatorChars + nameLength + 1;

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
        // Process the entry with the visitor.
        //
        if (!Visitor(subPath, &findFileData, Context)) {
            free(subPath);
            status = FALSE;
            break;
        }

        //
        // If a subdirectory was found and recursion is enabled, traverse it.
        //
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
            shouldRecurse) {

            //
            // Skip reparse points to avoid infinite loops.
            //
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                RevLogWarning("Skipping reparse point: %ls", subPath);
                free(subPath);
                continue;
            }

            if (!RevEnumerateDirectoryWithVisitor(subPath,
                                                  Visitor,
                                                  Context,
                                                  Options)) {

                RevLogError("Recursive subdirectory traversal failed for "
                            "\"%ls\".",
                            subPath);
                free(subPath);
                status = FALSE;
                break;
            }
        }

        free(subPath);

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
 * @brief Default file visitor used by the revision engine.
 *
 * This visitor revises regular files whose extensions are recognized
 * by RevShouldReviseFile and increments the ignored file counter
 * otherwise. Directories are ignored by this visitor (recursion is
 * handled by the enumerator itself).
 *
 * @param FullPath Supplies the full path to the file or directory.
 *
 * @param FindData Supplies the corresponding WIN32_FIND_DATAW structure.
 *
 * @param Context Supplies an optional user-defined context pointer.
 *
 * @return TRUE to continue enumeration, FALSE to stop.
 */
_Must_inspect_result_
BOOL
RevRevisionFileVisitor(
    _In_z_ PWCHAR FullPath,
    _In_ const WIN32_FIND_DATAW *FindData,
    _Inout_opt_ PVOID Context
    )
{
    PREVISION revision = (PREVISION)Context;
    BOOL isDirectory;

    if (revision == NULL) {
        revision = RevisionState;
    }

    if (FullPath == NULL || FindData == NULL || revision == NULL) {
        return FALSE;
    }

    isDirectory = (FindData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (isDirectory) {

        //
        // Subdirectories are handled by the enumerator; there is no work
        // to submit for directories in this visitor.
        //
        return TRUE;

    }

    if (!RevShouldReviseFile(FindData->cFileName)) {

        EnterCriticalSection(&revision->StatsLock);
        revision->CountOfIgnoredFiles += 1;
        LeaveCriticalSection(&revision->StatsLock);

        return TRUE;
    }

    if (revision->BackendVtable == NULL) {

        //
        // Fallback: process immediately on the enumeration thread.
        //
        return RevReviseFile(FullPath);
    }

    if (!revision->BackendVtable->SubmitFile(revision,
                                             FullPath,
                                             FindData)) {

        RevLogError("File backend failed to submit \"%ls\".", FullPath);
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief This function checks whether the specified file should be revised.
 *
 * The file is considered for revision if its name/extension can be resolved
 * to a known language/file type using the ExtensionMappingTable. This
 * includes:
 *   - Conventional extensions (".c", ".cpp", ".js", ...)
 *   - Multi-dot extensions (".rst.txt", ".config.js", ".glide.lock", ...)
 *   - Special whole-name mappings (".CMakeLists.txt", ".Dockerfile",
 *     ".Makefile", etc.) via the ".<FileName>" key convention.
 *
 * @param FileName Supplies the file name (without path).
 *
 * @return TRUE if the file should be revised, FALSE otherwise.
 */
_Must_inspect_result_
BOOL
RevShouldReviseFile(
    _In_z_ const WCHAR *FileName
    )
{
    BOOL status = FALSE;
    WCHAR extensionBuffer[MAX_EXTENSION_CCH];

    if (FileName == NULL) {
        RevLogError("FileName is NULL.");
        return status;
    }

    //
    // If the extension (in the generalized sense described above) can be
    // resolved to a known language or file type, we should revise the file.
    //
    status = RevResolveExtensionForFileName(FileName,
                                            extensionBuffer,
                                            ARRAYSIZE(extensionBuffer),
                                            NULL);

    return status;
}

/**
 * @brief Classifies a fully completed logical line and updates statistics.
 *
 * A line is considered:
 *   - Blank:   only whitespace, and not inside a block comment.
 *   - Comment: only comment text or still inside a block comment, with no code.
 *   - Code:    any line that contains code; comment text on the same line
 *              does not change it from code to comment.
 *
 * @param SawNonWhitespace TRUE if the line contained any non-whitespace char.
 * @param SawCode TRUE if the line contained any code outside comments.
 * @param SawComment TRUE if the line contained any comment characters.
 * @param InBlockComment TRUE if we are currently inside a block comment.
 * @param FileLineStats Receives updated counts.
 */
FORCEINLINE
VOID
RevClassifyCompletedLine(
    _In_ BOOL SawNonWhitespace,
    _In_ BOOL SawCode,
    _In_ BOOL SawComment,
    _In_ BOOL InBlockComment,
    _Inout_ PFILE_LINE_STATS FileLineStats
    )
{
    FileLineStats->CountOfLinesTotal += 1;

    if (!SawNonWhitespace &&
        !SawCode &&
        !SawComment &&
        !InBlockComment) {

        FileLineStats->CountOfLinesBlank += 1;

    } else if (!SawCode &&
               (SawComment || InBlockComment)) {

        FileLineStats->CountOfLinesComment += 1;
    }
}

/**
 * @brief Optionally classifies the last line of a file when there is no
 *        terminating newline.
 *
 * @param SawNonWhitespace  TRUE if the line contained any non-whitespace char.
 * @param SawCode TRUE if the line contained any code outside comments.
 * @param SawComment TRUE if the line contained any comment characters.
 * @param InBlockComment TRUE if we are currently inside a block comment.
 * @param FileLineStats Receives updated counts.
 */
FORCEINLINE
VOID
RevMaybeClassifyLastLine(
    _In_ BOOL SawNonWhitespace,
    _In_ BOOL SawCode,
    _In_ BOOL SawComment,
    _In_ BOOL InBlockComment,
    _Inout_ PFILE_LINE_STATS FileLineStats
    )
{
    //
    // For languages without block comments, InBlockComment is always FALSE.
    //
    if (SawNonWhitespace || SawCode || SawComment || InBlockComment) {
        RevClassifyCompletedLine(SawNonWhitespace,
                                 SawCode,
                                 SawComment,
                                 InBlockComment,
                                 FileLineStats);
    }
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
 * @param FileLineStats Supplies a pointer to a FILE_LINE_STATS structure that
 *        receives the results.
 */
VOID
RevCountLinesCStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _Inout_ PFILE_LINE_STATS FileLineStats
    )
{
    SIZE_T index = {0};
    BOOL inBlockComment = FALSE;
    BOOL inLineComment = FALSE;
    BOOL inString = FALSE;
    CHAR stringDelim = 0;
    BOOL sawCode = FALSE;
    BOOL sawComment = FALSE;
    BOOL sawNonWhitespace = FALSE;
    BOOL previousWasCR = FALSE;

    if (Buffer == NULL || FileLineStats == NULL || Length == 0) {
        return;
    }

    FileLineStats->CountOfLinesTotal = 0;
    FileLineStats->CountOfLinesBlank = 0;
    FileLineStats->CountOfLinesComment = 0;

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

            RevClassifyCompletedLine(sawNonWhitespace,
                                     sawCode,
                                     sawComment,
                                     inBlockComment,
                                     FileLineStats);

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
        // Inside a block comment: everything until "*/" is comment.
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
        // Inside a line comment: everything until newline is comment.
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
        // Line comment: // ...
        //
        if (currentChar == '/' && nextChar == '/') {

            inLineComment = TRUE;
            sawComment = TRUE;

            //
            // Optionally consume the second '/' so it will not be
            // re-processed as part of the comment body.
            //
            index += 1;
            continue;
        }

        //
        // Block comment: /* ... *\/
        //
        if (currentChar == '/' && nextChar == '*') {

            inBlockComment = TRUE;
            sawComment = TRUE;

            //
            // Consume '*'
            //
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
    RevMaybeClassifyLastLine(sawNonWhitespace,
                             sawCode,
                             sawComment,
                             inBlockComment,
                             FileLineStats);
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
 * @param FileLineStats Supplies a pointer to a FILE_LINE_STATS structure that
 *                  receives the results.
 */
VOID
RevCountLinesLineCommentStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _In_ CHAR FirstCommentChar,
    _In_ CHAR SecondCommentChar,
    _Inout_ PFILE_LINE_STATS FileLineStats
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

    if (Buffer == NULL || FileLineStats == NULL || Length == 0) {
        return;
    }

    FileLineStats->CountOfLinesTotal = 0;
    FileLineStats->CountOfLinesBlank = 0;
    FileLineStats->CountOfLinesComment = 0;

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

            //
            // N.B. No block comment state.
            //
            RevClassifyCompletedLine(sawNonWhitespace,
                                     sawCode,
                                     sawComment,
                                     FALSE,
                                     FileLineStats);

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

            if (FirstCommentChar != 0 &&
                currentChar == FirstCommentChar) {

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
    RevMaybeClassifyLastLine(sawNonWhitespace,
                             sawCode,
                             sawComment,
                             FALSE,
                             FileLineStats);
}

/**
 * @brief This function counts lines using XML-style block comments.
 *
 * Supported syntax:
 *   - Block comments:  <!-- ... -->
 *
 * Everything outside comments (markup, text, attributes) is treated as code.
 * There is no string-literal state here because XML comment delimiters
 * cannot appear inside comments in a way that needs escaping like in C.
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
 * @param FileLineStats Supplies a pointer to a FILE_LINE_STATS structure that
 *                  receives the results.
 */
VOID
RevCountLinesXmlStyle(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _Inout_ PFILE_LINE_STATS FileLineStats
    )
{
    SIZE_T index;
    BOOL inBlockComment = FALSE;
    BOOL sawCode = FALSE;
    BOOL sawComment = FALSE;
    BOOL sawNonWhitespace = FALSE;
    BOOL previousWasCR = FALSE;

    if (Buffer == NULL || FileLineStats == NULL || Length == 0) {
        return;
    }

    FileLineStats->CountOfLinesTotal = 0;
    FileLineStats->CountOfLinesBlank = 0;
    FileLineStats->CountOfLinesComment = 0;

    for (index = 0; index < Length; index += 1) {

        CHAR currentChar = Buffer[index];
        CHAR nextChar;
        CHAR thirdChar;
        CHAR fourthChar;

        if (index + 1 < Length) {
            nextChar = Buffer[index + 1];
        } else {
            nextChar = 0;
        }

        if (index + 2 < Length) {
            thirdChar = Buffer[index + 2];
        } else {
            thirdChar = 0;
        }

        if (index + 3 < Length) {
            fourthChar = Buffer[index + 3];
        } else {
            fourthChar = 0;
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

            RevClassifyCompletedLine(sawNonWhitespace,
                                     sawCode,
                                     sawComment,
                                     inBlockComment,
                                     FileLineStats);

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
        // Inside an XML block comment: <!-- ... -->
        //
        if (inBlockComment) {
            sawComment = TRUE;

            if (currentChar == '-' &&
                nextChar == '-' &&
                thirdChar == '>') {

                inBlockComment = FALSE;

                //
                // Consume "-->"
                //
                index += 2;
            }

            continue;
        }

        //
        // Start of XML block comment: <!--
        //
        if (currentChar == '<' &&
            nextChar == '!' &&
            thirdChar == '-' &&
            fourthChar == '-') {

            inBlockComment = TRUE;
            sawComment = TRUE;

            //
            // Skip "!--"
            //
            index += 3;
            continue;
        }

        //
        // Any other non-whitespace character outside comments
        // is treated as code (markup or text).
        //
        if (!isspace((UCHAR)currentChar)) {
            sawCode = TRUE;
        }
    }

    //
    // Handle the final line when the file does not end with a newline.
    //
    RevMaybeClassifyLastLine(sawNonWhitespace,
                             sawCode,
                             sawComment,
                             inBlockComment,
                             FileLineStats);
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
 * @param FileLineStats Supplies a pointer to a FILE_LINE_STATS structure that
 *                  receives the results.
 */
VOID
RevCountLinesWithFamily(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length,
    _In_ COMMENT_STYLE_FAMILY LanguageFamily,
    _Inout_ PFILE_LINE_STATS FileLineStats
    )
{
    switch (LanguageFamily) {

    case LanguageFamilyHashStyle:
        //
        // Languages with "#" line comments.
        //
        RevCountLinesLineCommentStyle(Buffer,
                                      Length,
                                      '#',
                                      0,
                                      FileLineStats);
        break;

    case LanguageFamilyDoubleDash:
        //
        // Languages with "--" line comments.
        //
        RevCountLinesLineCommentStyle(Buffer,
                                      Length,
                                      '-',
                                      '-',
                                      FileLineStats);
        break;

    case LanguageFamilySemicolon:
        //
        // Languages with ";" line comments.
        //
        RevCountLinesLineCommentStyle(Buffer,
                                      Length,
                                      ';',
                                      0,
                                      FileLineStats);
        break;

    case LanguageFamilyPercent:
        //
        // Languages with "%" line comments.
        //
        RevCountLinesLineCommentStyle(Buffer,
                                      Length,
                                      '%',
                                      0,
                                      FileLineStats);
        break;

    case LanguageFamilyXmlStyle:
        //
        // Languages with XML-style block comments: <!-- ... -->
        //
        RevCountLinesXmlStyle(Buffer,
                              Length,
                              FileLineStats);
        break;

    case LanguageFamilyNoComments:
        //
        // Languages that should not treat any characters as comments.
        // We reuse the generic line-comment scanner with a NUL prefix,
        // which effectively never matches in text files.
        //
        RevCountLinesLineCommentStyle(Buffer,
                                      Length,
                                      0,
                                      0,
                                      FileLineStats);
        break;

    case LanguageFamilyCStyle:
    case LanguageFamilyUnknown:
    default:
        //
        // Languages with // and /* ... */ comments.
        //
        RevCountLinesCStyle(Buffer,
                            Length,
                            FileLineStats);
        break;
    }
}

/**
 * @brief This function reads and revises the specified file.
 *
 * The function reads the file contents into memory, determines the
 * appropriate comment syntax based on the file extension and language
 * mapping, counts total/blank/comment lines, and updates the global
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
    PWCHAR languageOrFileType = NULL;
    COMMENT_STYLE_FAMILY languageFamily = LanguageFamilyUnknown;
    FILE_LINE_STATS fileLineStats = {0};
    FILE_BUFFER_VIEW view;
    WCHAR extensionBuffer[MAX_EXTENSION_CCH];

    ZeroMemory(&view, sizeof(view));

    if (FilePath == NULL) {
        RevLogError("FilePath is NULL.");
        status = FALSE;
        goto Exit;
    }

    //
    // Resolve the canonical extension key and language before reading.
    // This ensures consistent handling of multi-dot and special whole-name
    // mappings.
    //
    if (!RevResolveExtensionForPath(FilePath,
                                    extensionBuffer,
                                    ARRAYSIZE(extensionBuffer),
                                    &languageOrFileType)) {

        //
        // This should not normally happen because RevShouldReviseFile()
        // filters by extension ahead of time, but we still handle it
        // gracefully for robustness.
        //
        RevLogWarning("No language mapping found for \"%ls\".", FilePath);
        status = FALSE;
        goto UpdateStats;
    }

    languageFamily = RevGetLanguageFamily(languageOrFileType);

    //
    // Read the file into a buffer view.
    //
    if (!RevReadFileIntoBufferView(FilePath, &view)) {
        //
        // Errors already logged by the helper. Do not propagate as fatal
        // for the entire revision; behave like a per-file failure.
        //
        status = FALSE;
        goto UpdateStats;
    }

    //
    // If the content is recognized as text and there is something to
    // revise, count lines.
    //
    if (view.IsText && view.ContentLength > 0) {

        RevCountLinesWithFamily(view.Buffer + view.ContentOffset,
                                view.ContentLength,
                                languageFamily,
                                &fileLineStats);
    }

UpdateStats:

    if (RevisionState == NULL) {
        goto Exit;
    }

    EnterCriticalSection(&RevisionState->StatsLock);

    //
    // Find or create a revision record for the given extension/language.
    //
    revisionRecord =
        RevFindRevisionRecordForLanguageByExtension(extensionBuffer);

    if (revisionRecord == NULL) {

        revisionRecord = RevInitializeRevisionRecord(extensionBuffer,
                                                     languageOrFileType);

        if (revisionRecord == NULL) {
            RevLogError("Failed to initialize a revision record for \"%ls\".",
                        extensionBuffer);

            LeaveCriticalSection(&RevisionState->StatsLock);

            status = FALSE;
            goto Exit;
        }
    }

    //
    // Update per-language statistics.
    //
    revisionRecord->CountOfFiles += 1;
    revisionRecord->CountOfLinesTotal += fileLineStats.CountOfLinesTotal;
    revisionRecord->CountOfLinesBlank += fileLineStats.CountOfLinesBlank;
    revisionRecord->CountOfLinesComment += fileLineStats.CountOfLinesComment;

    //
    // Update global statistics.
    //
    RevisionState->CountOfFiles += 1;
    RevisionState->CountOfLinesTotal += fileLineStats.CountOfLinesTotal;
    RevisionState->CountOfLinesBlank += fileLineStats.CountOfLinesBlank;
    RevisionState->CountOfLinesComment += fileLineStats.CountOfLinesComment;

    LeaveCriticalSection(&RevisionState->StatsLock);

Exit:

    if (view.Buffer != NULL) {
        free(view.Buffer);
    }

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

    entry = RevisionState->RevisionRecordListHead.Flink;

    while (entry != &RevisionState->RevisionRecordListHead) {

        ULONGLONG total = 0;
        ULONGLONG blank = 0;
        ULONGLONG comment = 0;
        ULONGLONG code = 0;

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
        ULONGLONG total = 0;
        ULONGLONG blank = 0;
        ULONGLONG comment = 0;
        ULONGLONG code = 0;

        total = RevisionState->CountOfLinesTotal;
        blank = RevisionState->CountOfLinesBlank;
        comment = RevisionState->CountOfLinesComment;

        if (total >= blank + comment) {
            code = total - blank - comment;
        } else {
            code = 0;
        }

        RevPrint(L"%-25s%10u%15llu%15llu%15llu%15llu\n",
                 L"Total:",
                 RevisionState->CountOfFiles,
                 blank,
                 comment,
                 code,
                 total);
    }

    RevPrint(L"----------------------------------------------------------------"
             "---------------------------------------------\n");
}

/**
 * @brief Parses a backend kind name from a command line argument.
 *
 * The comparison is case-sensitive; accepted values are:
 *   - L"auto"
 *   - L"sync" or L"synchronous"
 *   - L"threadpool"
 *
 * @param Value Supplies the backend name text.
 *
 * @param BackendKind Receives the parsed backend kind on success.
 *
 * @return TRUE if the backend name was recognized, FALSE otherwise.
 */
_Must_inspect_result_
BOOL
RevParseBackendKind(
    _In_z_ PWCHAR Value,
    _Out_ REV_FILE_BACKEND_KIND *BackendKind
    )
{
    if (Value == NULL || BackendKind == NULL) {
        return FALSE;
    }

    if (wcscmp(Value, L"auto") == 0) {

        *BackendKind = RevFileBackendAuto;

    } else if (wcscmp(Value, L"sync") == 0 ||
               wcscmp(Value, L"synchronous") == 0) {

        *BackendKind = RevFileBackendSynchronous;

    } else if (wcscmp(Value, L"threadpool") == 0) {

        *BackendKind = RevFileBackendThreadPool;

    } else {

        return FALSE;
    }

    return TRUE;
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
    REVISION_CONFIG revisionConfig = {0};

    SupportAnsi = SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
                                 ENABLE_PROCESSED_OUTPUT |
                                 ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    if (!SupportAnsi) {
        RevLogWarning("Failed to enable ANSI escape sequences.");
    }

    RevPrint(WelcomeString);

     //
     // Process the command line arguments, if any.
     //

     if (argc <= 1) {
         //
         // The command line arguments were not passed at all, so a folder
         // selection dialog should be opened where the user can select a
         // directory to perform the revision.
         //
         RevPrint(UsageString);
         goto Exit;
    }

    if (wcscmp(argv[1], L"-help") == 0 ||
        wcscmp(argv[1], L"-h") == 0 ||
        wcscmp(argv[1], L"-?") == 0) {
        //
        // The only command line argument passed was '-help', '-h', or '-?',
        // so show the instruction for use.
        //
        RevPrint(UsageString);
        goto Exit;
    }

    //
    // Default enumeration behavior: recurse into subdirectories.
    //
    revisionConfig.EnumerationOptions.ShouldRecurseIntoSubdirectories = TRUE;

    hr = PathAllocCanonicalize(argv[1],
                               PATHCCH_ALLOW_LONG_PATHS |
                               PATHCCH_FORCE_ENABLE_LONG_NAME_PROCESS,
                               &revisionConfig.RootDirectory);
    if (FAILED(hr)) {
        RevLogError("Path normalization failed: 0x%08X", hr);
        status = -1;
        goto Exit;
    }

    if (argc > 2) {

        LONG index = 0;

        //
        // It is expected that in the case of multiple command line arguments:
        //  1) The first argument is the path to the root revision directory.
        //  2) The remaining parameters are for optional revision configuration
        //     overrides.
        //
        // Process additional parameters:
        //

        for (index = 2; index < argc; index += 1) {

            PWCHAR argument = argv[index];

            //
            // -v: Enable verbose mode.
            //
            if (wcscmp(argument, L"-v") == 0) {

                revisionConfig.IsVerboseMode = TRUE;

            } else if (wcscmp(argument, L"-nr") == 0 ||
                       wcscmp(argument, L"-norecurse") == 0) {

                //
                // -nr / -norecurse: do NOT recurse into subdirectories;
                // only enumerate the top-level directory.
                //
                revisionConfig.EnumerationOptions.
                               ShouldRecurseIntoSubdirectories = FALSE;

            } else if (wcscmp(argument, L"-backend") == 0) {

                PWCHAR backendName = NULL;

                //
                // -backend must be followed by a value: auto|sync|threadpool.
                //
                if (index + 1 >= argc) {

                    RevLogError("Missing value for -backend option.");
                    status = -1;
                    goto Exit;
                }

                backendName = argv[index + 1];

                if (!RevParseBackendKind(backendName,
                                         &revisionConfig.BackendKind)) {

                    RevLogError("Unknown backend type: %ls", backendName);
                    status = -1;
                    goto Exit;
                }

                //
                // Consume the value as well.
                //
                index += 1;

            } else if (wcscmp(argument, L"-threads") == 0) {

                PWCHAR threadsValue = NULL;

                //
                // -threads must be followed by a positive integer value.
                //
                if (index + 1 >= argc) {

                    RevLogError("Missing value for -threads option.");
                    status = -1;
                    goto Exit;
                }

                threadsValue = argv[index + 1]; {
                    PWCHAR EndPointer = NULL;
                    unsigned long threads = wcstoul(threadsValue,
                                                    &EndPointer,
                                                    10);

                    //
                    // Validate that the entire string parsed as a number,
                    // and that the number is positive and fits in ULONG.
                    //
                    if (EndPointer == threadsValue ||
                        *EndPointer != L'\0' ||
                        threads == 0 ||
                        threads > MAXDWORD) {

                        RevLogError("Invalid value for -threads option: %ls",
                                    threadsValue);
                        status = -1;
                        goto Exit;
                    }

                    revisionConfig.WorkerThreadCount = threads;
                }

                //
                // Consume the value as well.
                //
                index += 1;

            } else {

                //
                // Unknown option.
                //
                RevLogWarning("Unknown command line option: %ls", argument);
            }
        }
    }

#ifndef NDEBUG
    //
    // Always use verbose mode in debug builds unless it was explicitly
    // enabled earlier (which results in the same value).
    //
    if (revisionConfig.IsVerboseMode == FALSE) {
        revisionConfig.IsVerboseMode = TRUE;
    }
#endif

    //
    // Initialize the revision engine.
    //
    status = RevInitializeRevision(&revisionConfig);
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

    if (RevisionState->CountOfIgnoredFiles > 0) {
        RevPrintEx(Cyan,
                   L"\tIgnored %d files\n",
                   RevisionState->CountOfIgnoredFiles);
    }

#ifndef NDEBUG
    system("pause");
#endif

Exit:

    if (revisionConfig.RootDirectory != NULL) {
        LocalFree(revisionConfig.RootDirectory);
    }

    return status;
}
