@echo off
rem Batch file for post-build processing
rem
rem Parameters:
rem PostBuild %1 %2
rem
rem   %1 - Platform name (Win32 or x64)
rem   %2 - Configuration name (Debug or Release)
rem
rem

del .\bin\Play.pdb.00? >nul
if exist .\bin\Play.pdb ren .\bin\Play.pdb Play.pdb.000
if exist .\bin\Play.pdb ren .\bin\Play.pdb Play.pdb.001
if exist .\bin\Play.pdb ren .\bin\Play.pdb Play.pdb.002
if exist .\bin\Play.pdb ren .\bin\Play.pdb Play.pdb.003
copy .\bin\CascLib_test\Win32\Debug\CascLib_test.pdb .\bin\Play.pdb
