/*++

Copyright (c) 2023  Glebs. All rights reserved.

Module Name:

    main.c

Abstract:

    This module contains the entry point, its main responsibility is to
    process command line arguments.

Author:

    Glebs Oct-2023

--*/

#include "codemeter.h"

const WCHAR WelcomeString[] =
    L"\033[32mCodeMeter v0.0.1                 Copyright(c) 2023 Glebs\n"
    "--------------------------------------------------------\033[0m\n\n";

const WCHAR UsageString[] =
    L"\033[33mInstructions on how to use CodeMeter tools:\n\n"
    "In order to count the number of lines of CodeMeter code, you need "
    "the path to the root directory of the project you want to revise.\n"
    "The path should be passed as the first argument of the command line:\n\n\t"
    "CodeMeter.exe \"C:\\\\MyProject\\\"\033[0m\n\n";

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
