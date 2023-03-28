/*++

Module Name:

    codemeter.c

Abstract:

    A program for revising files (source code files by design). The revision includes
    counting the number of files, their extensions, the number of lines of code
    in the files (with and without comments).

Author:

    glebs 28-Mar-2023

Revision History:

--*/


#include <stdio.h>

#define WIN32_LEAN_AND_MEAN

#include <shlobj.h>   
#include <shlwapi.h>
#include <stdbool.h>
#include <Windows.h>


//
// Get StrCmpW from Shlwapi.
//
#pragma comment(lib, "Shlwapi")


//
// This global variable contains the path of the root directory where
// the user requested the revision.
//
static PCWSTR RootRevisionDirectory = NULL;

//
// This global variable contains the total number of files in RootRevisionDirectory.
//
static int TotalFilesCount = 0;


void
InitDirectoryRevision (
    _In_ PCWSTR path
    )
/*++

Routine Description:

    This function initiates a revision of the folder. The revision includes
    counting the number of files, their extensions, the number of lines of code
    in the files (with and without comments).

Arguments:

    path - Supplies a path of the root directory where the user requested the revision.

Return Value:



--*/
{
    
}

bool
SelectDirectoryToRevise (
    _Out_ PCWSTR path
    )
/*++

Routine Description:

    This function opens a folder selection dialog box.

Arguments:

    [Out] path - Selected directory path.
    
Return Value:

    true - Success.

    false - Failure.

--*/
{
    //
    // Not implemented.
    // TODO.
    //

    printf ("\033[33mSelectDirectoryToRevise is not implemented.\n\033[0m");
    return false;
}

int
wmain (
    int      argc,
    wchar_t *argv[]
    )
/*++

Routine Description:

    This function is the entry point of the program. This is where command line
    arguments are handled.
    
Arguments:

    argc - Supplies a number of command line arguments.
    
    argv - Supplies command line arguments.
        
Return Value:

    For now we always return 0.
 
--*/
{
    if (argc <= 1) {
        //
        // The command line argument is not passed, so we open a dialog box to select
        // the folder in which user wants to perform the revision.
        //
        if (SelectDirectoryToRevise (RootRevisionDirectory)) {
            //
            // Can revise...
            //
            InitDirectoryRevision (RootRevisionDirectory);
        }
        else {
            //
            // Folder selection failed. Report and return.
            //
            printf ("\033[31mERROR Provided folder path \"%ls\" is invalid.\033[0m", RootRevisionDirectory);
        }
    }
    else {
        //
        // The command line argument is passed. We only check the first argument,
        // which can be either "-help" to display help, or the path to the project
        // folder for code revision.
        //

        if (StrCmpW (argv[1], L"-help") == 0 ||
            StrCmpW (argv[1], L"-HELP") == 0 ||
            StrCmpW (argv[1], L"help") == 0 ||
            StrCmpW (argv[1], L"HELP") == 0 ||
            StrCmpW (argv[1], L"/help") == 0 ||
            StrCmpW (argv[1], L"/HELP") == 0 ||
            StrCmpW (argv[1], L"?") == 0) {
            //
            // Print help and return.
            //
            printf ("Usage:\n\n"
                    "CodeMeter.exe <Provide here the path to a directory>\n");
        }
        else {
            if (!PathIsDirectoryW (argv[1])) {
                //
                // Invalid path provided. Report and return.
                //
                printf ("\033[31mERROR Provided folder path \"%ls\" is invalid.\033[0m", argv[1]);
            }
            else {
                //
                // Can revise...
                //
                InitDirectoryRevision (argv[1]);
            }
        }
    }

    return 0;
}
