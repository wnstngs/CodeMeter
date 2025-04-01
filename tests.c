/*++

Copyright (c) 2025  wnstngs. All rights reserved.

Module Name:

    tests.c
    
Abstract:

    This module implements test harness.

--*/

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "codemeter.h"

#define _CRT_SECURE_NO_WARNINGS

extern PREVISION Revision;

typedef INT
(*TEST_PROC)(VOID);

typedef struct TEST_CASE {
    PWCHAR Name;
    TEST_PROC Procedure;
} TEST_CASE, *PTEST_CASE;

#define REGISTER_TEST(Test) { L#Test, Test }

/** Verifies that the two integer values are equal. */
#define ASSERT_EQUAL(actual, expected)                                      \
    do {                                                                    \
        if ((actual) != (expected)) {                                       \
            fwprintf(stderr,                                                \
                     L"[ERROR] %hs:%d: Assertion failed: expected %d, "     \
                     L"got %d\n", __FILE__, __LINE__,                       \
                     (int)(expected), (int)(actual));                       \
            return 1;                                                       \
        }                                                                   \
    } while (0)

/** Verifies that the expression is true. */
#define ASSERT_TRUE(expr)                                                   \
    do {                                                                    \
        if (!(expr)) {                                                      \
            fwprintf(stderr,                                                \
                     L"[ERROR] %hs:%d: Assertion failed: %hs is false\n",   \
                     __FILE__, __LINE__, #expr);                            \
            return 1;                                                       \
        }                                                                   \
    } while (0)

/** Verifies that the pointer is not NULL. */
#define ASSERT_NOT_NULL(ptr)                                                \
    do {                                                                    \
        if ((ptr) == NULL) {                                                \
            fwprintf(stderr,                                                \
                     L"[ERROR] %hs:%d: Assertion failed: %hs is NULL\n",    \
                     __FILE__, __LINE__, #ptr);                             \
            return 1;                                                       \
        }                                                                   \
    } while (0)

/** Runs a single test case and logs the result. */
static int
RunTest(const TEST_CASE TestCase)
{
    fwprintf(stdout, L"Running test: %ls... ", TestCase.Name);
    int result = TestCase.Procedure();
    if (result == 0) {
        fwprintf(stdout, L"PASSED\n");
    } else {
        fwprintf(stdout, L"FAILED\n");
    }
    return result;
}

//
// Test Cases
//

/** @brief Test case for file extension recognition.
 *
 *  Verifies that recognized file extensions return TRUE and
 *  unrecognized ones return FALSE.
 */
static int
test_file_extension_recognition(void)
{
    int res1 = RevShouldReviseFile(L"example.ahk");
    int res2 = RevShouldReviseFile(L"example.unknown");
    ASSERT_EQUAL(res1, 1);
    ASSERT_EQUAL(res2, 0);
    return 0;
}

/** @brief Test case for basic line counting.
 *
 *  Creates a temporary file with controlled content and verifies that
 *  RevReviseFile properly updates the revision statistics.
 */
static int
test_line_counting(void)
{
    const char *testContent = "line1\nline2\r\nline3";
    PWCHAR tempFileName = L"temp_test_file.txt";
    FILE *fp = _wfopen(tempFileName, L"wb");
    if (fp == NULL) {
        fwprintf(stderr,
                 L"[ERROR] Failed to create temporary file %ls\n",
                 tempFileName);
        return 1;
    }
    const size_t contentLength = strlen(testContent);
    const size_t written = fwrite(testContent,
                                  sizeof(char),
                                  contentLength,
                                  fp);
    fclose(fp);
    if (written != contentLength) {
        fwprintf(stderr,
                 L"[ERROR] Failed to write complete test content to %ls\n",
                 tempFileName);
        remove("temp_test_file.txt");
        return 1;
    }

    REVISION_INIT_PARAMS initParams = {0};
    initParams.RootDirectory = L".";
    initParams.IsVerboseMode = 0;
    if (!RevInitializeRevision(&initParams)) {
        fwprintf(stderr,
                 L"[ERROR] Failed to initialize revision engine.\n");
        remove("temp_test_file.txt");
        return 1;
    }

    if (!RevReviseFile(tempFileName)) {
        fwprintf(stderr,
                 L"[ERROR] RevReviseFile failed on %ls\n",
                 tempFileName);
        remove("temp_test_file.txt");
        return 1;
    }

    ASSERT_EQUAL(Revision->CountOfLinesTotal, 3);
    ASSERT_EQUAL(Revision->CountOfLinesBlank, 0);

    remove("temp_test_file.txt");
    return 0;
}

//
// Main Test Runner
//

int
main(void)
{
    const TEST_CASE tests[] = {
        REGISTER_TEST(test_file_extension_recognition),
        REGISTER_TEST(test_line_counting),
    };
    const int numTests = ARRAYSIZE(tests);
    int numFailed = 0;

    fwprintf(stdout, L"\nStarting CodeMeter Test Suite...\n\n");

    for (int i = 0; i < numTests; i++) {
        if (RunTest(tests[i]) != 0) {
            numFailed++;
        }
    }

    fwprintf(stdout, L"\nTest Summary:\n");
    fwprintf(stdout, L"  Total Tests: %d\n", numTests);
    fwprintf(stdout, L"  Passed:      %d\n", numTests - numFailed);
    fwprintf(stdout, L"  Failed:      %d\n", numFailed);

    if (numFailed > 0) {
        fwprintf(stdout,
                 L"\nSome tests failed. Please review the errors.\n");
        return 1;
    }

    fwprintf(stdout,
             L"\nAll tests passed successfully.\n");
    return 0;
}
