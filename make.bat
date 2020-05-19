@echo off
if not "x%WDKDIR%" == "x" goto SELECT_LIB
echo The WDKDIR environment variable is not set
echo Set this variable to your WDK directory (without ending backslash)
echo Example: set WDKDIR C:\WinDDK\6001
pause
goto:eof

:SELECT_LIB
set PROJECT_DIR=%~dp0
set LIBRARY_NAME=CascLibWDK

:BUILD_LIB_32
echo Building %LIBRARY_NAME%.lib (32-bit) ...
set DDKBUILDENV=
call %WDKDIR%\bin\setenv.bat %WDKDIR%\ fre w2k
cd %PROJECT_DIR%
build.exe -czgw
del buildfre_w2k_x86.log
echo.

:BUILD_LIB_64
echo Building %LIBRARY_NAME%.lib (64-bit) ...
set DDKBUILDENV=
call %WDKDIR%\bin\setenv.bat %WDKDIR%\ fre x64 WLH
cd %PROJECT_DIR%
build.exe -czgw
del buildfre_wlh_amd64.log
echo.

:COPY_LIBS
copy /Y .\objfre_wlh_amd64\amd64\%LIBRARY_NAME%.lib ..\aaa\lib64\%LIBRARY_NAME%.lib >nul
copy /Y .\objfre_w2k_x86\i386\%LIBRARY_NAME%.lib ..\aaa\lib32\%LIBRARY_NAME%.lib >nul
copy /Y .\src\CascPort.h ..\aaa\inc >nul
copy /Y .\src\CascLib.h ..\aaa\inc >nul

:CLEANUP
if exist build.bat del build.bat
