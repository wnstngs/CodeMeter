#include <stdio.h>

#define WIN32_LEAN_AND_MEAN

#include <shlwapi.h>
#include <Windows.h>


//
// Get StrCmpW from Shlwapi.
//
#pragma comment(lib, "Shlwapi")


//
// This global variable contains the path of the root directory where
// the user requested the revision.
//
static PCWSTR WorkingDirectory = NULL;


void
SelectDirectoryToRevise (void)
/*

Not implemented.
TODO.

*/
{
}

int
wmain (int      argc,
       wchar_t *argv[])
/*++

Routine Description:

    This function is the entry point of the program.
    
Arguments:

    argc - Supplies a number of command line arguments.
    
    argv - Supplies command line arguments.
        
Return Value:

    0 - Success.
    
    1 - Failure.
 
--*/
{
    int status = 0;

    if (argc <= 1) {
        //
        // The command line argument is not passed, so we open a dialog box to select
        // the folder in which user wants to perform the revision.
        //
        SelectDirectoryToRevise ();
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
            // Print help.
            //
            printf ("Usage:\n\n"
                    "CodeMeter.exe <Provide here the path to a directory>\n");
            status = 0;
            goto Exit;
        }
        else {
            if (!PathIsDirectoryW (argv[1])) {
                //
                // Invalid path provided.
                //
                printf ("\033[31mThe provided folder path \"%ls\" is invalid.\033[0m", argv[1]);
                status = 1;
                goto Exit;
            }
            else {
                
            }
        }
    }


Exit:
    return status;
}
