/*++

Copyright (c) 2023  Glebs. All rights reserved.

Module Name:

    revision.c
    
Abstract:

    This module contains the revision code. Revision means the whole process:
    from scanning files and counting them to counting lines of code.
                          ┌─────────────────┐
                e.g. path │                 │ returns
    Init params ─────────►│    Revision     ├─────────► Statistics
                          │                 │
                          └─────────────────┘
    
Author:

    Glebs Oct-2023

--*/

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
    _Field_z_ PWCHAR RootDirectory;         // Path to the revision root directory.
} REVISION_INIT_PARAMS, *PREVISION_INIT_PARAMS;

/**
 * @brief This structure stores statistics for some specific file extension.
 */
typedef struct REVISION_RECORD {
    _Field_z_ PWCHAR Extension;             // Extension of the revision record file.
    _Field_z_ PWCHAR RecognizedLanguage;    // Recognized programming language/file type based on extension.
    ULONG CountOfLines;                     // Number of lines in the revision record.
} REVISION_RECORD, *PREVISION_RECORD;

/**
 * @brief This structure stores the statistics of the entire revision.
 */
typedef struct REVISION {
    REVISION_INIT_PARAMS InitParams;        // Revision initialization parameters provided by the user.
    ULONG TotalCountOfLines;                // Number of lines in the whole project.
    PREVISION_RECORD HeadEntry;             // Head of the list of revision records for each extension.
    PREVISION_RECORD LastEntry;             // Tail of the list of revision records for each extension.
} REVISION, *PREVISION;

//
// Constants.
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
    L"\033[32mCodeMeter v0.0.1                 Copyright(c) 2023 Glebs\n"
    "--------------------------------------------------------\033[0m\n\n";

const WCHAR UsageString[] =
    L"\033[33mInstructions on how to use CodeMeter tools:\n\n"
    "In order to count the number of lines of CodeMeter code, you need "
    "the path to the root directory of the project you want to revise.\n"
    "The path should be passed as the first argument of the command line:\n\n\t"
    "CodeMeter.exe \"C:\\\\MyProject\\\"\033[0m\n\n";

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
 * @param RootDirectoryPath
 * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevEnumerateRecursively(
    _In_z_ PWCHAR RootDirectoryPath
);

_Must_inspect_result_
BOOL
RevReviseFile(
    _In_ PREVISION Revision,
    _In_z_ PWCHAR FilePath
);

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
        RevLogError("Failed to allocate buffer with malloc.");
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
        RevLogError("Invalid parameters.");
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
        RevLogError("Failed to allocate buffer with malloc.");
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
RevInitialize(
    _In_ PREVISION_INIT_PARAMS InitParams,
    _Out_ PREVISION Revision
    )
{
    BOOL status = TRUE;

    if (InitParams == NULL ||
        Revision == NULL ||
        InitParams->RootDirectory == NULL) {

        RevLogError("Invalid parameters.");
        status = FALSE;
        goto Exit;
    }

    Revision->InitParams = *InitParams;
    Revision->HeadEntry = NULL;
    Revision->LastEntry = Revision->HeadEntry;
    Revision->TotalCountOfLines = 0;

Exit:
    return status;
}

BOOL
RevStart(
    _In_ PREVISION Revision
    )
{
    BOOL status = TRUE;

    if (Revision == NULL || Revision->InitParams.RootDirectory == NULL) {
        RevLogError("Invalid parameters.");
        status = FALSE;
        goto Exit;
    }

    RevEnumerateRecursively(Revision->InitParams.RootDirectory);

Exit:
    return status;
}

BOOL
RevEnumerateRecursively(
    _In_z_ PWCHAR RootDirectoryPath
    )
{
    BOOL status = TRUE;
    DWORD lastKnownWin32Error;
    HANDLE findFile = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW findFileData;
    PWCHAR revisionSubpath = NULL;
    PWCHAR searchPath = NULL;

    /*
     * Check validity of passed arguments.
     */
    if (RootDirectoryPath == NULL) {
        RevLogError("Invalid parameters.");
        status = FALSE;
        goto Exit;
    }

    /*
     * Create the search pattern:
     *   Each directory path should indicate that we are examining all files,
     *   Append a wildcard character (an asterisk) to the root path for this purpose.
     * TODO: Check if the passed RootDirectoryPath already includes the wildcard.
     */
    searchPath = RevStringAppend(RootDirectoryPath,
                                 ASTERISK);
    if (searchPath == NULL) {
        RevLogError("Failed to normalize the revision subdirectory path "
                    "(RevStringAppend failed).");
        status = FALSE;
        goto Exit;
    }

    /*
     * Try to find a file or subdirectory with a name that matches the pattern.
     */
    findFile = FindFirstFileW(searchPath,
                              &findFileData);
    if (findFile == INVALID_HANDLE_VALUE) {
        lastKnownWin32Error = GetLastError();
        RevLogError("Failed to find a file named \"%ls\" to start the enumeration. "
                    "The last known error: %lu",
                    RootDirectoryPath,
                    lastKnownWin32Error);
        status = FALSE;
        goto Exit;
    }

    free(searchPath);

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
         * Check if found a subdirectory.
         */
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /*
             * If the item is a directory then append the "\" to the RootDirectoryPath.
             */
            revisionSubpath = RevStringAppend(RootDirectoryPath,
                                              L"\\");
            if (revisionSubpath == NULL) {
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
            revisionSubpath = RevStringAppend(revisionSubpath,
                                              findFileData.cFileName);
            if (revisionSubpath == NULL) {
                RevLogError("Failed to normalize the revision subdirectory path "
                            "(RevStringAppend failed).");
                status = FALSE;
                goto Exit;
            }

            /*
             * Recursively traverse all subdirectories.
             */
            RevEnumerateRecursively(revisionSubpath);

            free(revisionSubpath);
        } else {

            /*
             * Found a file.
             */

            RevReviseFile();
        }
    } while (FindNextFileW(findFile, &findFileData) != 0);

    FindClose(findFile);

Exit:
    return status;
}

int wmain(int argc, PWCHAR *argv)
{
    int status = 0;
    HANDLE stdOut;
    PWCHAR revisionPath = NULL;
    REVISION_INIT_PARAMS revisionInitParams;
    REVISION revision;

    wprintf(WelcomeString);

    /*
     * Process the command line arguments if any.
     */
    if (argc <= -1) {
        /*
         * The command line arguments were not passed at all, so a folder selection dialog
         * should be opened where the user can select a directory to perform the revision.
         */
        wprintf(UsageString);
        status = -1;
        goto Exit;
    }
    else {

        /*
         * Prepend L"\\?\" to the `argv[1]` to avoid the obsolete MAX_PATH limitation.
         */
        PWCHAR testPath = L"C:\\Users\\Glebs\\Downloads";
        revisionPath = RevStringPrepend(testPath/*argv[1]*/, MAX_PATH_FIX);
        if (revisionPath == NULL) {
            RevLogError("Failed to normalize the revision path (RevStringPrepend failed).");
            status = -1;
            goto Exit;
        }

        /*
         * Now we are ready to set the root path of the revision directory.
         */
        revisionInitParams.RootDirectory = revisionPath;

        if (argc == 2) {
            /*
             * Only one command line argument was provided, it is expected that this
             * should be the path to the directory to perform the revision.
             */
        }
        else {
            /*
             * It is expected that in the case of multiple command line arguments:
             *  1) The first argument is the path to the root revision directory.
             *  2) The remaining parameters are for optional revision configuration overrides.
             *
             * Process additional parameters:
             */
        }
    }

    /*
     * Initialize the directory revision process.
     */
    if (!RevInitialize(&revisionInitParams, &revision)) {
        RevLogError("RevInitialize failed.");
        status = -1;
        goto Exit;
    }

    if (!RevStart(&revision)) {
        RevLogError("RevStart failed.");
        status = -1;
        goto Exit;
    }

Exit:
    free(revisionPath);
    return status;
}
