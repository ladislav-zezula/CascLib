@echo off
rem Build file for VS 2017 (expected in %ProgramFiles%\Microsoft Visual Studio\2017)

rem Save the values of INCLUDE, LIB and PATH
set SAVE_INCLUDE=%INCLUDE%
set SAVE_LIB=%LIB%
set SAVE_PATH=%PATH%

rem Determine where the program files are, both for 64-bit and 32-bit Windows
if exist "%ProgramFiles%" set PROGRAM_FILES_DIR=%ProgramFiles%
if exist "%ProgramFiles(x86)%" set PROGRAM_FILES_DIR=%ProgramFiles(x86)%

rem Determine the installed version of Visual Studio (Professional/Enterprise)
if exist "%PROGRAM_FILES_DIR%\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS_BAT=%PROGRAM_FILES_DIR%\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvarsall.bat
if exist "%PROGRAM_FILES_DIR%\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS_BAT=%PROGRAM_FILES_DIR%\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvarsall.bat

:BUILD_32BIT
call "%VCVARS_BAT%" x86
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "DebugAD|Win32"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "DebugAS|Win32"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "DebugUD|Win32"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "DebugUS|Win32"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "ReleaseAD|Win32"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "ReleaseAS|Win32"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "ReleaseUD|Win32"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "ReleaseUS|Win32"
call :RestoreEnvVars

:BUILD_64BIT
call "%VCVARS_BAT%" x64
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "DebugAD|x64"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "DebugAS|x64"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "DebugUD|x64"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "DebugUS|x64"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "ReleaseAD|x64"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "ReleaseAS|x64"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "ReleaseUD|x64"
devenv.com CascLib_vs17.sln /project "CascLib" /rebuild "ReleaseUS|x64"
call :RestoreEnvVars

:RestoreEnvVars
set INCLUDE=%SAVE_INCLUDE%
set LIB=%SAVE_LIB%
set PATH=%SAVE_PATH%
