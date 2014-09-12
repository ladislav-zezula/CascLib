/*****************************************************************************/
/* CascTest.cpp                           Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Test module for CascLib                                                   */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascTest.cpp                    */
/*****************************************************************************/

#define _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_SECURE_NO_DEPRECATE
#define __INCLUDE_CRYPTOGRAPHY__
#define __CASCLIB_SELF__                    // Don't use CascLib.lib
#include <stdio.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "../src/CascLib.h"
#include "../src/CascCommon.h"

#ifdef _MSC_VER
#pragma warning(disable: 4505)              // 'XXX' : unreferenced local function has been removed
#endif

#ifdef PLATFORM_LINUX
#include <dirent.h>
#endif

//------------------------------------------------------------------------------
// Defines

#ifdef PLATFORM_WINDOWS
#define WORK_PATH_ROOT "E:\\Multimedia\\MPQs"
#endif

#ifdef PLATFORM_LINUX
#define WORK_PATH_ROOT "/home/ladik/MPQs"
#endif

#ifdef PLATFORM_MAC
#define WORK_PATH_ROOT "/Users/sam/StormLib/test"
#endif

#define MAKE_PATH(subdir)  (_T(WORK_PATH_ROOT) _T("\\") _T(subdir))

//-----------------------------------------------------------------------------
// Local functions

static int ForceCreatePath(TCHAR * szFullPath)
{
    TCHAR * szPlainName = (TCHAR *)GetPlainFileName(szFullPath) - 1;
    TCHAR * szPathPart = szFullPath;
    TCHAR chSaveChar;

    // Skip disk drive and root directory
    if(szPathPart[0] != 0 && szPathPart[1] == _T(':'))
        szPathPart += 3;

    while(szPathPart <= szPlainName)
    {
        // If there is a delimiter, create the path fragment
        if(szPathPart[0] == _T('\\') || szPathPart[0] == _T('/'))
        {
            chSaveChar = szPathPart[0];
            szPathPart[0] = 0;

            CREATE_DIRECTORY(szFullPath);
            szPathPart[0] = chSaveChar;
        }

        // Move to the next character
        szPathPart++;
    }

    return ERROR_SUCCESS;
}

static int ExtractFile(HANDLE hStorage, const char * szFileName, const TCHAR * szLocalPath, DWORD dwLocaleFlags)
{
    TFileStream * pStream = NULL;
    HANDLE hFile = NULL;
    TCHAR szLocalFileName[MAX_PATH];
    TCHAR * szNamePtr = szLocalFileName;
    BYTE Buffer[0x1000];
    DWORD dwBytesRead;
    int nError = ERROR_SUCCESS;

    // Create the file path
    _tcscpy(szNamePtr, szLocalPath);
    szNamePtr += _tcslen(szLocalPath);

    *szNamePtr++ = _T('\\');
    
    // Copy the plain file name
    CopyString(szNamePtr, szFileName, strlen(szFileName));

    // Open the CASC file
    if(nError == ERROR_SUCCESS)
    {
        // Open a file
        if(!CascOpenFile(hStorage, szFileName, dwLocaleFlags, 0, &hFile))
        {
            assert(GetLastError() != ERROR_SUCCESS);
            nError = GetLastError();
        }
    }

    // Create the local file
    if(nError == ERROR_SUCCESS)
    {
        pStream = FileStream_CreateFile(szLocalFileName, 0);
        if(pStream == NULL)
        {
            // Try to create all directories and retry
            ForceCreatePath(szLocalFileName);
            pStream = FileStream_CreateFile(szLocalFileName, 0);
            if(pStream == NULL)
                nError = GetLastError();
        }
    }

    // Read some data from the file
    if(nError == ERROR_SUCCESS)
    {
        for(;;)
        {
            // Read data from the file
            CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead);
            if(dwBytesRead == 0)
                break;

            // Write the local file
            FileStream_Write(pStream, NULL, Buffer, dwBytesRead);
        }
    }

    // Close handles
    if(pStream != NULL)
        FileStream_Close(pStream);
    if(hFile != NULL)
        CascCloseFile(hFile);
    return nError;
}

//-----------------------------------------------------------------------------
// Testing functions

static int TestOpenStorage_OpenFile(const TCHAR * szStorage, const char * szFileName)
{
    HANDLE hStorage;
    HANDLE hFile;
    DWORD dwBytesRead;
    BYTE Buffer[0x1000];
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    if(!CascOpenStorage(szStorage, 0, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS)
    {
        // Open a file
        if(!CascOpenFile(hStorage, szFileName, 0, 0, &hFile))
        {
            assert(GetLastError() != ERROR_SUCCESS);
            nError = GetLastError();
        }
    }

    // Read some data from the file
    if(nError == ERROR_SUCCESS)
    {
        // Read data from the file
        CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead);
        CascCloseFile(hFile);
    }

    // Close storage and return
    if(hStorage != NULL)
        CascCloseStorage(hStorage);
    return nError;
}

static int TestOpenStorage_EnumFiles(const TCHAR * szStorage, const TCHAR * szListFile = NULL)
{
    CASC_FIND_DATA FindData;
    HANDLE hStorage;
    HANDLE hFind;
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    if(!CascOpenStorage(szStorage, 0, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS)
    {
        hFind = CascFindFirstFile(hStorage, "*", &FindData, szListFile);
        if(hFind != NULL)
        {
            while(bFileFound)
            {
                // Extract the file
//              printf("%s\n", FindData.szFileName);

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &FindData);
            }

            // Just a testing call - must fail
            CascFindNextFile(hFind, &FindData);

            // Close the search handle
            CascFindClose(hFind);
        }
    }

    // Close storage and return
    if(hStorage != NULL)
        CascCloseStorage(hStorage);
    return nError;
}

static int TestOpenStorage_ExtractFiles(const TCHAR * szStorage, const TCHAR * szTargetDir, const TCHAR * szListFile)
{
    CASC_FIND_DATA FindData;
    HANDLE hStorage;
    HANDLE hFind;
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    if(!CascOpenStorage(szStorage, 0, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS)
    {
        hFind = CascFindFirstFile(hStorage, "*", &FindData, szListFile);
        if(hFind != INVALID_HANDLE_VALUE)
        {
            while(bFileFound)
            {
                // Extract the file
                printf("Extracting: %s ...", FindData.szFileName);
                nError = ExtractFile(hStorage, FindData.szFileName, szTargetDir, FindData.dwLocaleFlags);
                printf((nError == ERROR_SUCCESS) ? "OK\n" : "Failed\n");

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &FindData);
            }

            // Close the search handle
            CascFindClose(hFind);
        }
    }

    // Close storage and return
    if(hStorage != NULL)
        CascCloseStorage(hStorage);
    return nError;
}

static int TestOpenStorage_ExtractFilesArgs(const char * szStorage, const char * szTargetDir)
{
    TCHAR szStorageT[MAX_PATH];
    TCHAR szTargetT[MAX_PATH];

    CopyString(szStorageT, szStorage, strlen(szStorage));
    CopyString(szTargetT, szTargetDir, strlen(szTargetDir));
    return TestOpenStorage_ExtractFiles(szStorageT, szTargetT, NULL);
}

//-----------------------------------------------------------------------------
// Main

int main(int argc, char * argv[])
{
    int nError = ERROR_SUCCESS;

    // Keep compiler happy
    argc = argc;
    argv = argv;

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif  // defined(_MSC_VER) && defined(_DEBUG)

    // Test open CASC storage for HOTS
//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - Heroes of the Storm/29049/BNTData"), "mods/heroes.stormmod/base.stormmaps/maps/heroes/builtin/startingexperience/practicemode01.stormmap/dede.stormdata/localizeddata/gamestrings.txt");

    if(nError == ERROR_SUCCESS)
        nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - WoW/18848/Data"), "Cameras\\Abyssal_Maw_CameraFly_01.M2");

    // List all files in CASC storage with MNDX info
//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - Heroes of the Storm/30509/HeroesData"));

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - Heroes of the Storm/30414/HeroesData"));

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(_T("Data"), _T("e:\\Ladik\\AppDir\\CascLib\\listfile\\listfile-wow6.txt"));

    // Extracting the complete storage
//  if(nError == ERROR_SUCCESS && argc == 3)
//      nError = TestOpenStorage_ExtractFilesArgs(argv[1], argv[2]);

    // Test extracting the complete storage
//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - Heroes of the Storm/31726/HeroesData"), MAKE_PATH("Work"), NULL);

#ifdef _MSC_VER
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return nError;
}
