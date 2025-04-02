/*++

Copyright (c) 2025  wnstngs. All rights reserved.

Module Name:

    codemeter.h
    
Abstract:

    CodeMeter revision engine declarations for the CodeMeter test suite.

--*/

#ifndef CODEMETER_H
#define CODEMETER_H

#ifdef __cplusplus
extern "C" {
#endif

//
// -------------------------------------------------------------------- Includes
//

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

//
// ------------------------------------------------------- Data Type Definitions
//

typedef struct REVISION_INIT_PARAMS {
    _Field_z_ PWCHAR RootDirectory;
    BOOL IsVerboseMode;
} REVISION_INIT_PARAMS, *PREVISION_INIT_PARAMS;

typedef struct REVISION {
    REVISION_INIT_PARAMS InitParams;
    LIST_ENTRY RevisionRecordListHead;
    ULONGLONG CountOfLinesTotal;
    ULONGLONG CountOfLinesBlank;
    ULONG CountOfFiles;
    ULONG CountOfIgnoredFiles;
} REVISION, *PREVISION;

//
// --------------------------------------------------------- Function Prototypes
//

_Must_inspect_result_
BOOL
RevInitializeRevision(
    _In_ PREVISION_INIT_PARAMS InitParams
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

#ifdef __cplusplus
}
#endif

#endif //CODEMETER_H
