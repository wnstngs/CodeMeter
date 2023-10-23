/*++

Copyright (c) 2023  Glebs. All rights reserved.

Module Name:

    codemeter.h
    
Abstract:

    This header contains function declarations, internal data structures, globals,
    and constants.
    
Author:

    Glebs Oct-2023

--*/

#ifndef CODEMETER_CODEMETER_H
#define CODEMETER_CODEMETER_H

#include <stdio.h>
#include <malloc.h>

#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

//
// Internal data structures.
//

/**
 * @brief This structure stores the initialization parameters of the revision
 * provided by the user at launch.
 */
typedef struct REVISION_INIT_PARAMS {
    PWCHAR RootDirectory;               // Path to the revision root directory.
} REVISION_INIT_PARAMS, *PREVISION_INIT_PARAMS;

/**
 * @brief This structure stores statistics for some specific file extension.
 */
typedef struct REVISION_RECORD {
    PWCHAR Extension;                   // Extension of the revision record file.
    PWCHAR RecognizedLanguage;          // Recognized programming language/file type based on extension.
    ULONG CountOfLines;                 // Number of lines in the revision record.
} REVISION_RECORD, *PREVISION_RECORD;

/**
 * @brief This structure stores the statistics of the entire revision.
 */
typedef struct REVISION {
    REVISION_INIT_PARAMS InitParams;    // Revision initialization parameters provided by the user.
    ULONG TotalCountOfLines;            // Number of lines in the whole project.
    PREVISION_RECORD HeadEntry;         // Head of the list of revision records for each extension.
    PREVISION_RECORD LastEntry;         // Tail of the list of revision records for each extension.
} REVISION, *PREVISION;

//
// Functions.
//

/**
 * @brief This function appends one unicode string to another and returns the result.
 * @param String1 The first string (to which String2 will be appended).
 * @param String2 The second string (to be appended to String1).
 * @return A new string containing the concatenation of String1 and String2.
 *         NULL if the function failed.
 *         N.B. The caller is responsible for freeing the memory.
 */
PWCHAR
RevStringAppend(
    _In_ PWCHAR String1,
    _In_ PWCHAR String2
    );

/**
 * @brief This function prepends one unicode string to another and returns the result.
 * @param String1 Supplies the first string (to be appended to).
 * @param String2 Supplies the second string (to be prepended).
 * @return A new string containing the concatenation of String1 and String2.
 *         NULL if the function failed.
 *         N.B. The caller is responsible for freeing the memory.
 */
PWCHAR
RevStringPrepend(
    _In_ PWCHAR String1,
    _In_ PWCHAR String2
    );

/**
 * @brief
 * @param InitParams Supplies the revision initialization parameters.
 * @param Revision
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevInitialize(
    _In_ PREVISION_INIT_PARAMS InitParams,
    _Out_ PREVISION Revision
    );

/**
 * @brief
 * @param Revision
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevStart(
    _In_ PREVISION Revision
    );

/**
 * @brief
 * @param Path
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevpEnumerateRecursively(
    _In_ PWCHAR Path
    );

BOOL
RevisionReadFile();

/**
 * @brief This function outputs a red text error message to the standard error stream.
 * @param Message Supplies the error message.
 */
#define RevLogError(Message, ...)                                   \
    do {                                                            \
        fprintf(stderr,                                             \
                "\033[0;31m[ERROR]\n└───> (in %s@%d): " Message,    \
                __FUNCTION__,                                       \
                __LINE__,                                           \
                ##__VA_ARGS__);                                     \
        printf("\033[0m\n");                                        \
    } while (0)

#endif //CODEMETER_CODEMETER_H
