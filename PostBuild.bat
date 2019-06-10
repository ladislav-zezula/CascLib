@echo off
rem Post-build batch for CascLib project
rem Called as PostBuild.bat $(ProjectName) $(PlatformName) $(ConfigurationName)
rem Example: CascLib.bat x64 Debug

if "%1" == "CascLib" goto PostBuild_LIB
if "%1" == "CascLib_dll" goto PostBuild_DLL

:PostBuild_LIB
rem Copy the public headers target include directory
if not exist ..\aaa goto:eof
copy /Y .\src\CascPort.h ..\aaa\inc
copy /Y .\src\CascLib.h  ..\aaa\inc
if "x%2" == "xWin32" goto PostBuild_LIB_Win32
if "x%2" == "xx64" goto PostBuild_LIB_Win64
goto:eof

:PostBuild_LIB_Win32
rem Copy the compiled LIB to the target library directory
copy  /Y .\bin\%1\%2\%3\*.lib    ..\aaa\lib32
goto:eof

:PostBuild_LIB_Win64
rem Copy the compiled LIB to the target library directory
copy  /Y .\bin\%1\%2\%3\*.lib    ..\aaa\lib64
goto:eof

:PostBuild_DLL
rem On 32-bit Release version, increment the build number
if not "x%2" == "xWin32" goto PostBuild_DLL_NoRC
if not "x%3" == "xRelease" goto PostBuild_DLL_NoRC
PostBuild.exe .\bin\%1\%2\%3\CascLib.dll .\src\DllMain.rc /pdb
goto:eof

:PostBuild_DLL_NoRC
PostBuild.exe .\bin\%1\%2\%3\CascLib.dll /pdb
goto:eof
