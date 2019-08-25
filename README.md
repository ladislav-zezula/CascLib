# CascLib

**CascLib** is an open-source implementation of library for reading CASC storages from Blizzard games since 2014.

For API documentation, refer to http://www.zezula.net/en/casc/casclib.html .

## Using CascLib as static library in Windows
1. Clone the CascLib repository into a local folder:

	`git clone https://github.com/ladislav-zezula/CascLib.git`

2. Open one of the solution files in Microsoft Visual Studio

	- `CascLib_vs17.sln` for Visual Studio 2017
	- `CascLib_vs15.sln` for Visual Studio 2015
	- `CascLib_vs08.sln` for Visual Studio 2008

3. Select `Build / Batch Build` and select all `CascLib` build configurations. Do a full build. The result LIB files for each platform are in `.\bin\CascLib\Win32` and `.\bin\CascLib\x64`. The following build configurations are available:

	- DebugAD\CascLibDAD.lib (Debug Ansi version with dynamic CRT library)
	- DebugAS\CascLibDAS.lib (Debug Ansi version with static CRT library)
	- DebugUD\CascLibDUD.lib (Debug Unicode version with dynamic CRT library)
	- DebugUS\CascLibDUS.lib (Debug Unicode version with static CRT library)
	- ReleaseAD\CascLibRAD.lib (Release Ansi version with dynamic CRT library)
	- ReleaseAS\CascLibRAS.lib (Release Ansi version with static CRT library)
	- ReleaseUD\CascLibRUD.lib (Release Unicode version with dynamic CRT library)
	- ReleaseUS\CascLibRUS.lib (Release Unicode version with static CRT library)

4. After the build is done, put all 32-bit LIBs to a library directory (e.g. `lib32`) and all 64-bit LIBs into another directory (e.g. `lib64`)

5. Include `CascLib.h` in your project. `CascLib.h` will automatically select the required LIB file, depending on your project settings.

6. Build your project.

## Using CascLib as DLL in Windows
1. Clone the CascLib repository into a local folder:

	`git clone https://github.com/ladislav-zezula/CascLib.git`

2. Open one of the solution files in Microsoft Visual Studio

	- `CascLib_vs17.sln` for Visual Studio 2017
	- `CascLib_vs15.sln` for Visual Studio 2015
	- `CascLib_vs08.sln` for Visual Studio 2008

3. Select `Build / Batch Build` and check all `CascLib_dll Release` configurations. Do a full build. The result DLL and LIB files for `Win32` and `x64` platforms are in:
	- `.\bin\CascLib_dll\Win32\Release` (32-bit build)
	- `.\bin\CascLib_dll\x64\Release` (64-bit build)

5. Include `CascLib.h` and add `CascLib.lib` to your project and build it.
