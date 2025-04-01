/*++

Copyright (c) 2025  wnstngs. All rights reserved.

Module Name:

    codemeter.h
    
Abstract:

    Public declarations for the CodeMeter revision engine.
    This header exposes types and function prototypes used for
    file revision (line counting, file extension mapping, etc.).

--*/

#ifndef CODEMETER_H
#define CODEMETER_H

#ifdef __cplusplus
extern "C" {
#endif

//
// -------------------------------------------------------------------- Includes
//
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

//
// ---------------------------------------------------------------------- Macros
//

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

//
// ------------------------------------------------------- Data Type Definitions
//


/**
 * @brief This structure stores the initialization parameters of the
 * revision provided by the user at launch.
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
 * @brief This enumeration defines the types of file extensions
 * (to be used in REVISION_RECORD_EXTENSION_MAPPING).
 */
typedef enum REVISION_RECORD_EXTENSION_TYPE {
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
 * @brief This structure stores the mapping of file extensions to
 * programming languages.
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
     * @brief Extension type which indicates whether the extension is a
     * single-dot or multi-dot extension.
     */
    /* TODO: Temporarily disabled. Re-evaluate the approach.
    REVISION_RECORD_EXTENSION_TYPE ExtensionType; */
} REVISION_RECORD_EXTENSION_MAPPING, *PREVISION_RECORD_EXTENSION_MAPPING;

/**
 * @brief This structure stores statistics for some specific file
 * extension.
 */
typedef struct REVISION_RECORD {
    /**
     * @brief Linked list entry.
     */
    LIST_ENTRY ListEntry;

    /**
     * @brief Extension of the revision record file and recognized
     * programming language/file type based on extension.
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

//
// --------------------------------------------------------- Function Prototypes
//

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
    );

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
    );

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
    );

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
    _In_ PREVISION_INIT_PARAMS InitParams
    );

/**
 * @brief This function is responsible for starting the revision system.
 * It ensures that the system has been initialized correctly before
 * proceeding with its operations.
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_

BOOL
RevStartRevision(
    VOID
    );

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
_Must_inspect_result_

PREVISION_RECORD
RevInitializeRevisionRecord(
    _In_z_ PWCHAR Extension,
    _In_z_ PWCHAR LanguageOrFileType
    );

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
    );

/**
 * This function searches a table of file extension-to-language mappings
 * to find the programming language associated with the provided file
 * extension.
 *
 * @param Extension Supplies the file extension.
 *
 * @return If a matching file extension is found in the mapping table,
 * the function returns the associated programming language as a string.
 * If no match is found, the function returns NULL.
 */
_Ret_maybenull_

PWCHAR
RevMapExtensionToLanguage(
    _In_z_ PWCHAR Extension
    );

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
    );

/**
 * @brief This function checks if a file extension is in the extension
 * table. File should be revised only if it has valid (is in the table)
 * extension.
 *
 * @param FileName Supplies the name of the file to be checked.
 *
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_

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

//
// ------------------------------------------------------------ Inline Functions
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

#ifdef __cplusplus
}
#endif

#endif //CODEMETER_H
