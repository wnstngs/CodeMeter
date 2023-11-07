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

#include <stdio.h>
#include <malloc.h>

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
    _Field_z_ PWCHAR RootDirectory;         // Path to the revision root directory.
} REVISION_INIT_PARAMS, *PREVISION_INIT_PARAMS;

/**
 * @brief This structure stores the mapping of file extensions to programming languages.
 */
typedef struct REVISION_RECORD_EXTENSION_MAPPING {
    _Field_z_ PWCHAR Extension;             // File extension.
    _Field_z_ PWCHAR LanguageOrType;        // Programming language or file type.
} REVISION_RECORD_EXTENSION_MAPPING, *PREVISION_RECORD_EXTENSION_MAPPING;

/**
 * @brief This structure stores statistics for some specific file extension.
 */
typedef struct REVISION_RECORD {
    /*
     * Extension of the revision record file and recognized programming language/file
     * type based on extension.
     */
    REVISION_RECORD_EXTENSION_MAPPING ExtensionMapping;
    ULONG CountOfLines;                     // Number of lines in the revision record.
} REVISION_RECORD, *PREVISION_RECORD;

/**
 * @brief This structure stores the statistics of the entire revision.
 */
typedef struct REVISION {
    REVISION_INIT_PARAMS InitParams;        // Revision initialization parameters provided by the user.
    ULONGLONG TotalCountOfLines;            // Number of lines in the whole project.
    PREVISION_RECORD HeadEntry;             // Head of the list of revision records for each extension.
    PREVISION_RECORD LastEntry;             // Tail of the list of revision records for each extension.
} REVISION, *PREVISION;

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
    L"\033[32mCodeMeter v0.0.1                 Copyright(c) 2023 Glebs\n"
    "--------------------------------------------------------\033[0m\n\n";

const WCHAR UsageString[] =
    L"\033[33mInstructions on how to use CodeMeter tools:\n\n"
    "In order to count the number of lines of CodeMeter code, you need "
    "the path to the root directory of the project you want to revise.\n"
    "The path should be passed as the first argument of the command line:\n\n\t"
    "CodeMeter.exe \"C:\\\\MyProject\\\"\033[0m\n\n";

/**
 * @brief Mapping of file extensions that can be recognized to human-readable descriptions of file types.
 */
REVISION_RECORD_EXTENSION_MAPPING ExtensionMappingTable[] = {
    {L".c", L"C"},
    {L".h", L"C/C++ Header"}
};

/*
 * The global revision state used throughout the entire program run-time.
 */
PREVISION Revision = NULL;

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
 * @brief This function checks if a file extension is in the extension table. File should be revised
 * only if it has valid (is in the table) extension.
 * @param FilePath Supplies the path to the file to be checked.
 * @return TRUE if succeeded, FALSE if failed.
 */
BOOL
RevShouldReviseFile(
    _In_ PWCHAR FilePath
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
RevInitialize(
    _In_ PREVISION_INIT_PARAMS InitParams
    );

/**
 * @brief This function is responsible for starting the revision system. It
 * ensures that the system has been initialized correctly before proceeding
 * with its operations.
= * @return TRUE if succeeded, FALSE if failed.
 */
_Must_inspect_result_
BOOL
RevStart(
    VOID
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
    DWORD lastKnownError = GetLastError(), formatResult;

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
        messageBuffer = (PWCHAR) malloc((5 + 1) * sizeof(WCHAR));
        if (messageBuffer == NULL) {
            RevLogError("Failed to allocate a message buffer.");
            goto Exit;
        }

        swprintf_s(messageBuffer,
                   (5 + 1),
                   L"%d",
                   lastKnownError);
    }

Exit:
    return messageBuffer;
}

BOOL
RevShouldReviseFile(
    _In_ PWCHAR FilePath
    )
{
    LONG i, tableSize;
    PWCHAR fileExtension;

    if (FilePath == NULL) {
        RevLogError("FilePath is NULL.");
        return FALSE;
    }

    /*
     * Find the file extension.
     */
    fileExtension = wcsrchr(FilePath, L'.');

    if (fileExtension == NULL) {
        RevLogError("Failed to determine the extension for the file \"%ls\".", FilePath);
        return FALSE;
    }

    /*
     * Define the size of the extension mapping table.
     */
    tableSize = sizeof(ExtensionMappingTable) / sizeof(ExtensionMappingTable[0]);

    for (i = 0; i < tableSize; ++i) {
        if (wcscmp(ExtensionMappingTable[i].Extension, fileExtension) == 0) {
            return TRUE;
        }
    }

    return FALSE;
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

    Revision = (PREVISION) malloc(sizeof(REVISION));

    RtlZeroMemory(Revision, sizeof(REVISION));

    Revision->InitParams = *InitParams;
    Revision->HeadEntry = NULL;
    Revision->LastEntry = Revision->HeadEntry;
    Revision->TotalCountOfLines = 0;

Exit:
    return status;
}

BOOL
RevStart(
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

BOOL
RevEnumerateRecursively(
    _In_z_ PWCHAR RootDirectoryPath
    )
{
    BOOL status = TRUE;
    HANDLE findFile = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW findFileData;
    PWCHAR subdirectoryPath = NULL;
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
     * Create the search pattern:
     *
     * Each directory path should indicate that we are examining all files,
     * Append a wildcard character (an asterisk) to the root path for this purpose.
     *
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
        subdirectoryPath = RevStringAppend(RootDirectoryPath,
                                           L"\\");
        if (subdirectoryPath == NULL) {
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
        subdirectoryPath = RevStringAppend(subdirectoryPath,
                                           findFileData.cFileName);
        if (subdirectoryPath == NULL) {
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
            RevEnumerateRecursively(subdirectoryPath);
        } else {

            /*
             * If found a file, check if the file should be revised, and if so, revise it.
             *
             * The revision should be performed only if the file extension has been recognized.
             * For this purpose it is enough to pass only the file name (findFileData.cFileName),
             * for file revision the full path (subdirectoryPath) is required.
             */
            if (RevShouldReviseFile(findFileData.cFileName)) {
                RevReviseFile(subdirectoryPath);
            }
        }

        /*
         * Free after RevStringAppend.
         */
        free(subdirectoryPath);
    } while (FindNextFileW(findFile, &findFileData) != 0);

    FindClose(findFile);

Exit:
    return status;
}

BOOL
RevReviseFile(
    PWCHAR FilePath
    )
{
    BOOL status = TRUE, eof = FALSE;
    HANDLE file;
    ULONGLONG lineCount = 0;
    PWCHAR readBuffer = NULL;
    DWORD bytesRead, i;

    if (FilePath == NULL) {
        RevLogError("Invalid parameter/-s.");
        status = FALSE;
        goto Exit;
    }

    /*
     * Attempt to open the file for reading.
     */
    file = CreateFileW(FilePath,
                       GENERIC_READ,
                       FILE_SHARE_DELETE | FILE_SHARE_READ,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (file == INVALID_HANDLE_VALUE) {
        RevLogError("Failed to read the file \"%ls\". The last known error: %ls",
                    FilePath,
                    RevGetLastKnownWin32Error());
        status = FALSE;
        goto Exit;
    }

    /*
     * Allocate the buffer for reading.
     */
    readBuffer = (PWCHAR) malloc(524288);
    if (readBuffer == NULL) {
        RevLogError("Failed to allocate a buffer.");
        status = FALSE;
        goto Exit;
    }

    /*
     * Read the file content and count lines.
     */
    while (!eof) {
        ReadFile(file,
                 readBuffer,
                 524288,
                 &bytesRead,
                 NULL);

        for (i = 0; i < bytesRead; ++i) {
            if (readBuffer[i] == '\n') {
                ++lineCount;
            }
        }

        /*
         * Check if we've reached the end of the file.
         */
        eof = bytesRead < sizeof(readBuffer);
    }

    /*
     * Update the total line count.
     */
    Revision->TotalCountOfLines += lineCount;

    CloseHandle(file);

Exit:
    free(readBuffer);

    return status;
}

int
wmain(
    int argc,
    wchar_t *argv[]
    )
{
    int status = 0;
    ULONGLONG start, end;
    PWCHAR revisionPath = NULL;
    REVISION_INIT_PARAMS revisionInitParams;

    wprintf(WelcomeString);

    /*
     * Process the command line arguments if any.
     * TODO: "<= 1" instead of "<= -1" in Release. "-1" is used only for debugging.
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
         * TODO: Use argv[1] instead of testPath. testPath is used only for debugging.
         */
        PWCHAR testPath = L"C:\\Dev\\CodeMeter\\tests";
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
    if (!RevInitialize(&revisionInitParams)) {
        RevLogError("RevInitialize failed.");
        status = -1;
        goto Exit;
    }

    start = __rdtsc();

    if (!RevStart()) {
        RevLogError("RevStart failed.");
        status = -1;
        goto Exit;
    }

    end = __rdtsc();

    printf("TotalCountOfLines: %llu", Revision->TotalCountOfLines);
    printf(" (in %lld ticks)\n", end - start);

Exit:
    free(revisionPath);

    return status;
}
