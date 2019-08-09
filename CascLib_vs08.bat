@echo off
rem Build file for VS 2008. Expects MS Visual Studio to be installed
rem in %ProgramFiles%\Microsoft Visual Studio 9.0

rem Determine where the program files are, both for 64-bit and 32-bit Windows
if exist "%ProgramFiles%" set PROGRAM_FILES_DIR=%ProgramFiles%
if exist "%ProgramFiles(x86)%" set PROGRAM_FILES_DIR=%ProgramFiles(x86)%
if exist "C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\vcvarsall.bat" goto BUILD_32BIT
echo Visual Studio 2008 is not installed. Exiting.
goto:eof

:BUILD_32BIT
call "%PROGRAM_FILES_DIR%\Microsoft Visual Studio 9.0\VC\vcvarsall.bat" x86
devenv.com CascLib_vs08.sln /project "CascLib" /build "DebugAD|Win32"
devenv.com CascLib_vs08.sln /project "CascLib" /build "DebugAS|Win32"
devenv.com CascLib_vs08.sln /project "CascLib" /build "DebugUD|Win32"
devenv.com CascLib_vs08.sln /project "CascLib" /build "DebugUS|Win32"
devenv.com CascLib_vs08.sln /project "CascLib" /build "ReleaseAD|Win32"
devenv.com CascLib_vs08.sln /project "CascLib" /build "ReleaseAS|Win32"
devenv.com CascLib_vs08.sln /project "CascLib" /build "ReleaseUD|Win32"
devenv.com CascLib_vs08.sln /project "CascLib" /build "ReleaseUS|Win32"
devenv.com CascLib_vs08.sln /project "CascLib_dll" /build "Release|Win32"


:BUILD_64BIT
call "%PROGRAM_FILES_DIR%\Microsoft Visual Studio 9.0\VC\vcvarsall.bat" x64
devenv.com CascLib_vs08.sln /project "CascLib" /build "DebugAD|x64"
devenv.com CascLib_vs08.sln /project "CascLib" /build "DebugAS|x64"
devenv.com CascLib_vs08.sln /project "CascLib" /build "DebugUD|x64"
devenv.com CascLib_vs08.sln /project "CascLib" /build "DebugUS|x64"
devenv.com CascLib_vs08.sln /project "CascLib" /build "ReleaseAD|x64"
devenv.com CascLib_vs08.sln /project "CascLib" /build "ReleaseAS|x64"
devenv.com CascLib_vs08.sln /project "CascLib" /build "ReleaseUD|x64"
devenv.com CascLib_vs08.sln /project "CascLib" /build "ReleaseUS|x64"
devenv.com CascLib_vs08.sln /project "CascLib_dll" /build "Release|x64"
