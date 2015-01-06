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

#include "TLogHelper.cpp"

#ifdef _MSC_VER
#pragma warning(disable: 4505)              // 'XXX' : unreferenced local function has been removed
#endif

#ifdef PLATFORM_LINUX
#include <dirent.h>
#endif

//------------------------------------------------------------------------------
// Defines

#ifdef PLATFORM_WINDOWS
#define WORK_PATH_ROOT "\\Multimedia\\MPQs"
#endif

#ifdef PLATFORM_LINUX
#define WORK_PATH_ROOT "/home/ladik/MPQs"
#endif

#ifdef PLATFORM_MAC
#define WORK_PATH_ROOT "/Users/sam/StormLib/test"
#endif

#define MAKE_PATH(subdir)  (_T(WORK_PATH_ROOT) _T("\\") _T(subdir))

static const char szCircleChar[] = "|/-\\";

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
//  TFileStream * pStream = NULL;
    HANDLE hFile = NULL;
    TCHAR szLocalFileName[MAX_PATH];
    TCHAR * szNamePtr = szLocalFileName;
    BYTE Buffer[0x1000];
    DWORD dwBytesRead = 0;
    bool bResult;
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
//  if(nError == ERROR_SUCCESS)
//  {
//      pStream = FileStream_CreateFile(szLocalFileName, 0);
//      if(pStream == NULL)
//      {
//          // Try to create all directories and retry
//          ForceCreatePath(szLocalFileName);
//          pStream = FileStream_CreateFile(szLocalFileName, 0);
//          if(pStream == NULL)
//              nError = GetLastError();
//      }
//  }

    // Read some data from the file
    if(nError == ERROR_SUCCESS)
    {
        while(dwBytesRead == 0)
        {
            // Read data from the file
            bResult = CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead);
            if(bResult == false)
            {
                nError = GetLastError();
                break;
            }

            // Write the local file
//          if(dwBytesRead != 0)
//              FileStream_Write(pStream, NULL, Buffer, dwBytesRead);
        }
    }

    // Log the file sizes
#ifdef CASCLIB_TEST
//  if(nError == ERROR_SUCCESS)
//  {
//      TCascFile * hf = IsValidFileHandle(hFile);
//
//      fprintf(fp, "%8u %8u %8u %8u %8u %s\n", hf->FileSize_RootEntry,
//                                              hf->FileSize_EncEntry,
//                                              hf->FileSize_IdxEntry,
//                                              hf->FileSize_HdrArea,
//                                              hf->FileSize_FrameSum,
//                                              szFileName);
//  }
#endif

    // Close handles
//  if(pStream != NULL)
//      FileStream_Close(pStream);
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
    if(!CascOpenStorage(szStorage, CASC_LOCALE_ENGB, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS && szFileName != NULL)
    {
        // Open a file
        if(CascOpenFile(hStorage, szFileName, 0, 0, &hFile))
        {
            CascGetFileSize(hFile, NULL);
            CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead);
            CascCloseFile(hFile);
        }
        else
        {
            assert(GetLastError() != ERROR_SUCCESS);
            nError = GetLastError();
        }
    }

    // Close storage and return
    if(hStorage != NULL)
        CascCloseStorage(hStorage);
    return nError;
}

static int TestOpenStorage_EnumFiles(const TCHAR * szStorage, const TCHAR * szListFile = NULL)
{
    CASC_FIND_DATA FindData;
    TLogHelper LogHelper;
    HANDLE hStorage;
    HANDLE hFind;
    DWORD dwTotalFiles = 0;
    DWORD dwFoundFiles = 0;
    DWORD dwCircleCount = 0;
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    LogHelper.PrintProgress("Opening storage ...");
    if(!CascOpenStorage(szStorage, 0, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS)
    {
        // Retrieve the total number of files
        CascGetStorageInfo(hStorage, CascStorageFileCount, &dwTotalFiles, sizeof(dwTotalFiles), NULL);

        // Start finding
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &FindData, szListFile);
        if(hFind != NULL)
        {
            while(bFileFound)
            {
                // Extract the file
                if((dwFoundFiles % 400) == 0)
                {
                    LogHelper.PrintProgress("Enumerating files: %c", szCircleChar[dwCircleCount % 4]);
                    dwCircleCount++;
                }

                // Find the next file in CASC
                dwFoundFiles++;
                bFileFound = CascFindNextFile(hFind, &FindData);
            }

            // Just a testing call - must fail
            CascFindNextFile(hFind, &FindData);

            // Close the search handle
            CascFindClose(hFind);
            LogHelper.PrintProgress("");
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
    TLogHelper LogHelper;
    HANDLE hStorage;
    HANDLE hFind;
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    LogHelper.PrintProgress("Opening storage ...");
    if(!CascOpenStorage(szStorage, 0, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS)
    {
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &FindData, szListFile);
        if(hFind != INVALID_HANDLE_VALUE)
        {
            // Search the storage
            while(bFileFound)
            {
                // Extract the file
                LogHelper.PrintProgress("Extracting %s ...", FindData.szPlainName);
                nError = ExtractFile(hStorage, FindData.szFileName, szTargetDir, FindData.dwLocaleFlags);

                // Terminate the line
                if(nError != ERROR_SUCCESS)
                    LogHelper.PrintError("Extracting %s .. Failed", FindData.szPlainName);

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &FindData);
            }

            // Close the search handle
            CascFindClose(hFind);
            LogHelper.PrintProgress("");
        }
    }

    // Close storage and return
    if(hStorage != NULL)
        CascCloseStorage(hStorage);
    return nError;
}

static int Hack()
{
/*
    HANDLE hFile;
    BYTE Buffer[0x100];
    hash_state md5_state;
    LPBYTE pbStartHash = &Buffer[BLTE_HEADER_DELTA];
    BYTE md5_hash[MD5_HASH_SIZE];
    uint32_t dwHashHigh = 0;
    uint32_t dwHashLow = 0;
    DWORD dwBytesRead = 0;

    hFile = CreateFile(_T("e:\\Multimedia\\MPQs\\2014 - WoW\\18888\\Data\\data\\data.017"), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(hFile != INVALID_HANDLE_VALUE)
    {
        SetFilePointer(hFile, 0x34CBFE8C, NULL, FILE_BEGIN);
        ReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead, NULL);
        CloseHandle(hFile);

        for(int i = 1; i < 4+4+16; i++)
        {
            dwHashHigh = dwHashLow = 0;
            hashlittle2(pbStartHash, i, &dwHashHigh, &dwHashLow);

            md5_init(&md5_state);
            md5_process(&md5_state, pbStartHash, i);
            md5_done(&md5_state, md5_hash);

            if((dwHashHigh & 0xFF) == 0xAF)
                DebugBreak();
        }
    }
*/
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// Main

int main(int argc, char * argv[])
{
    const TCHAR * szListFile = _T("\\Ladik\\AppDir\\CascLib\\listfile\\listfile-wow6.txt");
    int nError = ERROR_SUCCESS;

    // Keep compiler happy
    szListFile = szListFile;
    argc = argc;
    argv = argv;

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif  // defined(_MSC_VER) && defined(_DEBUG)

//  if(nError == ERROR_SUCCESS)
//      nError = Hack();

    if(nError == ERROR_SUCCESS)
        nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - WoW/19116/Data"), "SPELLS\\T_VFX_BLOOD06B.BLP");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - WoW/18865/Data"), "SPELLS\\T_VFX_BLOOD06B.BLP");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - WoW/18888/Data"), "SPELLS\\T_VFX_BLOOD06B.BLP");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - WoW/19116/Data"), "DBFilesClient\\AnimationData.dbc");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - Heroes of the Storm/29049/BNTData"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(_T("c:\\Hry\\Heroes of the Storm\\HeroesData"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - WoW/18865/Data"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - WoW/18888/Data"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - WoW/19116/Data"), szListFile);

    // Test extracting the complete storage
//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - Heroes of the Storm/29049/BNTData"), _T("Work"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - WoW/18865/Data"), _T("Work"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - WoW/18888/Data"), _T("Work"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - WoW/19116/Data"), _T("Work"), szListFile);

#ifdef _MSC_VER                                                          
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return nError;
}
