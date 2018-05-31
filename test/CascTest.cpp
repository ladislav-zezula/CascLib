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

#if defined(_MSC_VER) && defined(_DEBUG)
#define GET_TICK_COUNT()  GetTickCount()
#else
#define GET_TICK_COUNT()  0
#endif

//-----------------------------------------------------------------------------
// Local functions

static bool IsFileKey(const char * szFileName)
{
    BYTE KeyBuffer[MD5_HASH_SIZE];

    // The length must be at least the length of the CKey
    if(strlen(szFileName) < MD5_STRING_SIZE)
        return false;

    // Convert the BLOB to binary.
    if(ConvertStringToBinary(szFileName, MD5_STRING_SIZE, KeyBuffer) != ERROR_SUCCESS)
        return false;

    return true;
}

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

static int ExtractFile(HANDLE hStorage, CASC_FIND_DATA & cf, const TCHAR * szLocalPath)
{
//  TFileStream * pStream = NULL;
    HANDLE hFile = NULL;
    BYTE Buffer[0x1000];
    DWORD dwBytesRead = 1;
    bool bResult;
    int nError = ERROR_SUCCESS;

    // Keep compiler happy
    CASCLIB_UNUSED(szLocalPath);

    // Open the CASC file
    if(!CascOpenFile(hStorage, cf.szFileName, cf.dwLocaleFlags, cf.dwOpenFlags, &hFile))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    // Create the local file
//  if(nError == ERROR_SUCCESS)
//  {
//      TCHAR szLocalFileName[MAX_PATH];
//      TCHAR * szNamePtr = szLocalFileName;
//          
//      // Create the file path
//      _tcscpy(szNamePtr, szLocalPath);
//      szNamePtr += _tcslen(szLocalPath);
//      *szNamePtr++ = _T('\\');
//      CopyString(szNamePtr, cf.szFileName, strlen(szFileName));
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
    while(nError == ERROR_SUCCESS)
    {
        // Read data from the file
        bResult = CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead);
        if(bResult == false)
        {
            nError = GetLastError();
            break;
        }

        if(dwBytesRead == 0)
            break;

        // Write the local file
//      if(dwBytesRead != 0)
//          FileStream_Write(pStream, NULL, Buffer, dwBytesRead);
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

static int CompareFile(TLogHelper & LogHelper, HANDLE hStorage, CASC_FIND_DATA & cf, const TCHAR * szLocalPath)
{
    ULONGLONG FileSize = (ULONGLONG)-1;
    TFileStream * pStream = NULL;
    HANDLE hCascFile = NULL;
    LPBYTE pbFileData1 = NULL;
    LPBYTE pbFileData2 = NULL;
    TCHAR szFileName[MAX_PATH+1];
    DWORD dwFileSize1;
    DWORD dwFileSize2;
    DWORD dwBytesRead;
    int nError = ERROR_SUCCESS;

    // If we don't know the name, use the CKey as name
    if(cf.dwOpenFlags & CASC_OPEN_TYPE_MASK)
        _stprintf(szFileName, _T("%s\\unknown\\%02X\\%s"), szLocalPath, cf.FileKey[0], cf.szFileName);
    else
        _stprintf(szFileName, _T("%s\\%s"), szLocalPath, cf.szFileName);

/*
    if(cf.szFileName[0] == 0)
    {
        StringFromBinary(cf.FileKey, MD5_HASH_SIZE, cf.szFileName);
        dwFlags |= CASC_OPEN_BY_CKEY;

        CopyString(szTempBuff, cf.szFileName, MAX_PATH);
        _stprintf(szFileName, _T("%s\\unknown\\%02X\\%s"), szLocalPath, cf.FileKey[0], szTempBuff);
    }
    else
    {
        CopyString(szTempBuff, cf.szFileName, MAX_PATH);
    }
*/
    LogHelper.PrintProgress("Comparing %s ...", cf.szFileName);

    // Open the CASC file
    if(nError == ERROR_SUCCESS)
    {
        if(!CascOpenFile(hStorage, cf.szFileName, cf.dwLocaleFlags, cf.dwOpenFlags, &hCascFile))
            nError = LogHelper.PrintError("CASC file not found: %s", cf.szFileName);
    }

    // Open the local file
    if(nError == ERROR_SUCCESS)
    {
        pStream = FileStream_OpenFile(szFileName, STREAM_FLAG_READ_ONLY);
        if(pStream == NULL)
            nError = LogHelper.PrintError("Local file not found: %s", cf.szFileName);
    }

    // Retrieve the size of the file
    if(nError == ERROR_SUCCESS)
    {
        dwFileSize1 = CascGetFileSize(hCascFile, NULL);
        if(FileStream_GetSize(pStream, &FileSize))
            dwFileSize2 = (DWORD)FileSize;

        if(dwFileSize1 == CASC_INVALID_SIZE || dwFileSize2 == CASC_INVALID_SIZE)
        {
            nError = LogHelper.PrintError("Failed to get file size: %s", cf.szFileName);
        }
    }

    // The file sizes must match
    if(nError == ERROR_SUCCESS)
    {
        if(dwFileSize1 != dwFileSize2)
        {
            SetLastError(ERROR_FILE_CORRUPT);
            nError = LogHelper.PrintError("Size mismatch on %s", cf.szFileName);
        }
    }

    // Read the entire content to memory
    if(nError == ERROR_SUCCESS)
    {
        pbFileData1 = CASC_ALLOC(BYTE, dwFileSize1);
        pbFileData2 = CASC_ALLOC(BYTE, dwFileSize2);
        if(pbFileData1 == NULL || pbFileData2 == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            nError = LogHelper.PrintError("Failed allocate memory");
        }
    }

    // Read the entire CASC file to memory
    if(nError == ERROR_SUCCESS)
    {
        if(!CascReadFile(hCascFile, pbFileData1, dwFileSize1, &dwBytesRead))
        {
            nError = LogHelper.PrintError("Failed to read casc file: %s", cf.szFileName);
        }
    }

    // Read the entire local file to memory
    if(nError == ERROR_SUCCESS)
    {
        if(!FileStream_Read(pStream, NULL, pbFileData2, dwFileSize2))
        {
            nError = LogHelper.PrintError("Failed to read local file: %s", cf.szFileName);
        }
    }

    // Compare both
    if(nError == ERROR_SUCCESS)
    {
        if(memcmp(pbFileData1, pbFileData2, dwFileSize1))
        {
            SetLastError(ERROR_FILE_CORRUPT);
            nError = LogHelper.PrintError("File data mismatch: %s", cf.szFileName);
        }
    }

    // Free both buffers
    if(pbFileData2 != NULL)
        CASC_FREE(pbFileData2);
    if(pbFileData1 != NULL)
        CASC_FREE(pbFileData1);
    if(pStream != NULL)
        FileStream_Close(pStream);
    if(hCascFile != NULL)
        CascCloseFile(hCascFile);
    return nError;
}

//-----------------------------------------------------------------------------
// Testing functions

static int TestOpenStorage_OpenFile(const TCHAR * szStorage, const char * szFileName)
{
    TLogHelper LogHelper("OpenStorage");
    HANDLE hStorage;
    HANDLE hFile;
    DWORD dwFileSize2 = 0;
    DWORD dwFileSize1;
    DWORD dwFlags = 0;
    BYTE Buffer[0x1000];
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    LogHelper.PrintProgress(_T("Opening storage \"%s\"..."), szStorage);
    if(!CascOpenStorage(szStorage, CASC_LOCALE_ENGB, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS && szFileName != NULL)
    {
        // Check whether the name is the CKey
        if(IsFileKey(szFileName))
            dwFlags |= CASC_OPEN_BY_CKEY;

        // Open a file
        LogHelper.PrintProgress("Opening file %s...", szFileName);
        if(CascOpenFile(hStorage, szFileName, 0, dwFlags, &hFile))
        {
            dwFileSize1 = CascGetFileSize(hFile, NULL);

            for(;;)
            {
                DWORD dwBytesRead = 0;

                CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead);
                if(dwBytesRead == 0)
                    break;

                dwFileSize2 += dwBytesRead;
            }
            
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
    TLogHelper LogHelper("OpenForEnum");
    HANDLE hStorage;
    HANDLE hFind;
    DWORD dwTotalFiles = 0;
    DWORD dwFoundFiles = 0;
    DWORD dwCircleCount = 0;
    DWORD dwTickCount = 0;
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    LogHelper.PrintProgress(_T("Opening storage %s ..."), szStorage);
    if(!CascOpenStorage(szStorage, 0, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS)
    {
        // Dump the storage
//      CascDumpStorage(hStorage, "E:\\storage-dump.txt");

        // Retrieve the total number of files
        CascGetStorageInfo(hStorage, CascStorageFileCount, &dwTotalFiles, sizeof(dwTotalFiles), NULL);

        // Start finding
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &FindData, szListFile);
        if(hFind != NULL)
        {
            dwTickCount = GET_TICK_COUNT();
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

            dwTickCount = GET_TICK_COUNT() - dwTickCount;

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
    TLogHelper LogHelper("OpenForExtract");
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
        // Dump the storage
//      CascDumpStorage(hStorage, "E:\\storage-dump.txt");

        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &FindData, szListFile);
        if(hFind != INVALID_HANDLE_VALUE)
        {
            // Search the storage
            while(bFileFound)
            {
                // Extract the file
                LogHelper.PrintProgress("Extracting %s ...", FindData.szPlainName);
                nError = ExtractFile(hStorage, FindData, szTargetDir);
                if(nError != ERROR_SUCCESS)
                    LogHelper.PrintError("Extracting %s .. Failed", FindData.szPlainName);

                // Compare the file with the local copy
//              CompareFile(LogHelper, hStorage, FindData, szTargetDir);

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

static int TestOpenStorage_GetFileDataId(const TCHAR * szStorage, const char * szFileName, DWORD expectedId)
{
    TLogHelper LogHelper("GetFileDataId");
    HANDLE hStorage;
    int nError = ERROR_FILE_NOT_FOUND;

    // Open the storage directory
    LogHelper.PrintProgress("Opening storage ...");
    if(!CascOpenStorage(szStorage, 0, &hStorage))
    {
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }
    else
    {
        nError = ERROR_SUCCESS;
    }

    if(nError == ERROR_SUCCESS)
    {
        if(CascGetFileId(hStorage, szFileName) != expectedId)
            nError = ERROR_BAD_FORMAT;
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
    const TCHAR * szListFile = _T("\\Ladik\\Appdir\\CascLib\\listfile\\World of Warcraft 6x.txt");
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

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - WoW/18888/Data"), "SPELLS\\T_VFX_BLOOD06B.BLP");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - WoW/19342-root-file-cut/Data"), "SPELLS\\T_VFX_BLOOD06B.BLP");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - WoW/19678-after-patch/Data"), "File000CD780.###");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - Heroes of the Storm/29049/BNTData/config/48/ae"), "World\\Maps\\Azeroth\\Azeroth_29_28.adt");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - Heroes of the Storm/30414/HeroesData"), "World\\Maps\\Azeroth\\Azeroth_29_28.adt");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2014 - Heroes of the Storm/39445/HeroesData"), "World\\Maps\\Azeroth\\Azeroth_29_28.adt");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2015 - Diablo III/30013"), "ENCODING");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_OpenFile(MAKE_PATH("2015 - Overwatch/24919/casc/data"), "ROOT");

//    if(nError == ERROR_SUCCESS)
//        nError = TestOpenStorage_OpenFile(MAKE_PATH("2016 - Starcraft II/45364/SC2Data\\/\\"), "mods/novastoryassets/base.sc2maps/maps/campaign/nova/nova01.sc2map");

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - Heroes of the Storm/29049"), NULL);

//  if(nError == ERROR_SUCCESS)                                                                  
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - Heroes of the Storm/30027/BNTData"), NULL);

//  if(nError == ERROR_SUCCESS)                                                                  
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - Heroes of the Storm/30414/HeroesData/config/09/32/"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - Heroes of the Storm/31726/HeroesData"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - Heroes of the Storm/39445/HeroesData"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - WoW/18865/Data"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - WoW/18888/Data"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - WoW/19116/Data"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - WoW/19678-after-patch/Data"), szListFile);

    // With a non-existant listfile
//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2014 - WoW/19678-after-patch/Data"), _T("huhu.txt"));

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2016 - WoW/21742/Data"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2016 - WoW/23420/Data"), _T("huhu.txt"));

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2017 - Starcraft1/4037"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2015 - Diablo III/30013/Data"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2015 - Overwatch/24919/casc/data"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_EnumFiles(MAKE_PATH("2015 - Overwatch/47161/casc/data"), NULL);

    if(nError == ERROR_SUCCESS)
        nError = TestOpenStorage_EnumFiles(MAKE_PATH("2018 - New CASC/00001"), NULL);

    // Test extracting the complete storage
//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - Heroes of the Storm/30414/HeroesData"), _T("Work"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - WoW/18865/Data"), _T("Work"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - WoW/18888/Data"), _T("Work"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2014 - WoW/19678-after-patch/Data"), _T("Work"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2016 - WoW/21742/Data"), _T("Work"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2016 - WoW/22267/Data"), _T("Work"), szListFile);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2015 - Diablo III/Data"), _T("Work"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2015 - Overwatch/24919/casc/data"), MAKE_PATH("Work"), NULL);

//  if(nError == ERROR_SUCCESS)
//      nError = TestOpenStorage_ExtractFiles(MAKE_PATH("2018 - New CASC/00001"), _T("Work"), NULL);


#ifdef _MSC_VER                                                          
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return nError;
}
