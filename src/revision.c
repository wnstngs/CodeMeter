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

#include "codemeter.h"

PWCHAR RevStringAppend(
    _In_ PWCHAR String1,
    _In_ PWCHAR String2
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

PWCHAR RevStringPrepend(
    _In_ PWCHAR String1,
    _In_ PWCHAR String2
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

BOOL RevInitialize(
    _In_
    PREVISION_INIT_PARAMS InitParams,
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

BOOL RevStart(
    _In_ PREVISION Revision
)
{
    BOOL status = TRUE;

    if (Revision == NULL || Revision->InitParams.RootDirectory == NULL) {
        RevLogError("Invalid parameters.");
        status = FALSE;
        goto Exit;
    }

    RevpEnumerateRecursively(Revision->InitParams.RootDirectory);

Exit:
    return status;
}

BOOL RevpEnumerateRecursively(
    _In_ PWCHAR Path
    )
{
    BOOL status = TRUE;
    DWORD lastKnownWin32Error;
    HANDLE findFile = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW findFileData;
    PWCHAR revisionSubpath = NULL;

    if (Path == NULL) {
        RevLogError("Invalid parameters.");
        status = FALSE;
        goto Exit;
    }

    findFile = FindFirstFileW(Path,
                              &findFileData);

    if (findFile == INVALID_HANDLE_VALUE) {
        lastKnownWin32Error = GetLastError();
        RevLogError("Failed to find a file named \"%ls\" to start the enumeration. The last known error: %lu",
                    Path,
                    lastKnownWin32Error);
        status = FALSE;
        goto Exit;
    }

    do {
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /*
             * Found a subdirectory, run the enumeration recursively.
             */

            /*
             * Each directory path should indicate that we are examining all files,
             * Windows asks you to add an asterisk for this purpose.
             */
            revisionSubpath = RevStringAppend(findFileData.cFileName, L"\\\\*");
            if (revisionSubpath == NULL) {
                RevLogError("Failed to normalize the revision subdirectory path (RevStringAppend failed).");
                status = FALSE;
                goto Exit;
            }
            RevpEnumerateRecursively(revisionSubpath);
        }
        else {
            /*
             * Found a file, read it.
             */
            printf(" ");
            wprintf(findFileData.cFileName);
            printf("\n");
        }
    } while (FindNextFileW(findFile, &findFileData) != 0);

    FindClose(findFile);

Exit:
    free(revisionSubpath);
    return status;
}
