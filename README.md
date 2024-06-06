# CodeMeter

## About
CodeMeter is a small program for counting the number of lines of code in a directory with language statistics (by file type) and taking into account blank lines. 
CodeMeter uses the C language and relies on the Windows API to meet the following objectives:
- to have a small and fast-running program (the popular alternative `cloc` is quite slow on large projects).
- to have access to specific system interfaces for optimizing I/O operations: Windows I/O completion ports, in Windows 10+ I/O Ring API.
- practice and study of C and Win32.

## Implementation Plan:
- [X] Basic foundation and data structures for future expansion
- [X] Logging and verbose mode with command line argument support
- [X] Basic file reading in a directory and counting lines in them
- [X] File extension determination and matching with file type/programming language
- [X] Counting empty lines
- [ ] RegEx support and counting lines of code by programming languages
- [ ] Use only native interfaces, avoiding Win32 libraries to minimize the final program file size
- [ ] Use IOCP
- [ ] Support I/O Ring in Windows 10+
