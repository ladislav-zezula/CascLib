@echo off
rem Post-build batch for CascLib project
rem Called as CascLib.bat $(PlatformName) $(ConfigurationName)
rem Example: CascLib.bat x64 Debug

if not exist ..\aaa goto exit

copy src\CascPort.h ..\aaa\inc
copy src\CascLib.h  ..\aaa\inc

if x%1 == xWin32 goto PlatformWin32
if x%1 == xx64 goto PlatformWin64
goto exit

:PlatformWin32
copy .\bin\CascLib\%1\%2\*.lib    ..\aaa\lib32
goto exit

:PlatformWin64
copy .\bin\CascLib\%1\%2\*.lib    ..\aaa\lib64
goto exit

:exit

