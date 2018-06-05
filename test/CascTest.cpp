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
#define CASC_PATH_ROOT "\\Multimedia\\MPQs"
#endif

#ifdef PLATFORM_LINUX
#define CASC_PATH_ROOT "/home/ladik/MPQs"
#endif

#ifdef PLATFORM_MAC
#define CASC_PATH_ROOT "/Users/sam/StormLib/test"
#endif

#define MAKE_PATH(subdir)  (_T(CASC_PATH_ROOT) _T(CASC_PATH_SEPR) _T(subdir))

static const char szCircleChar[] = "|/-\\";

#if defined(_MSC_VER) && defined(_DEBUG)
#define GET_TICK_COUNT()  GetTickCount()
#else
#define GET_TICK_COUNT()  0
#endif

//-----------------------------------------------------------------------------
// Local structures

#define SHORT_NAME_SIZE 60

typedef struct _STORAGE_INFO
{
    const char * szPath;
    const char * szFile;
} STORAGE_INFO, *PSTORAGE_INFO;

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

static void MakeShortName(const char * szFileName, char * szShortName, size_t ccShortName)
{
    char * szShortNameEnd = szShortName + ccShortName - 1;
    size_t nLength = strlen(szFileName);

    // Is the name too long?
    if(nLength >= ccShortName)
    {
        size_t nFirstPart = (ccShortName / 2) - 3;
        size_t nRemaining;

        // Copy the first part
        memcpy(szShortName, szFileName, nFirstPart);
        szShortName += nFirstPart;

        // Copy "..."
        memcpy(szShortName, "...", 3);
        szShortName += 3;

        // Copy the rest
        nRemaining = szShortNameEnd - szShortName;
        memcpy(szShortName, szFileName + nLength - nRemaining, nRemaining);
        szShortName[nRemaining] = 0;
    }
    else
    {
        strcpy(szShortName, szFileName);
    }
}

static TCHAR * MakeFullPath(const char * szStorage, TCHAR * szBuffer, size_t ccBuffer)
{
    TCHAR * szBufferEnd = szBuffer + ccBuffer - 1;
    const char * szPathRoot = CASC_PATH_ROOT;

    // Copy the path prefix
    while(szBuffer < szBufferEnd && szPathRoot[0] != 0)
        *szBuffer++ = *szPathRoot++;

    // Append the separator
    if(szBuffer < szBufferEnd)
        *szBuffer++ = PATH_SEP_CHAR;

    // Append the rest
    while(szBuffer < szBufferEnd && szStorage[0] != 0)
        *szBuffer++ = *szStorage++;

    // Append zero
    szBuffer[0] = 0;
    return szBuffer;
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

static int ExtractFile(
    TLogHelper & LogHelper,
    HANDLE hStorage,
    const char * szFileName,
    DWORD dwOpenFlags,
    DWORD dwLocaleFlags,
    DWORD dwFileCount,
    DWORD dwTotalFileCount)
{
    hash_state md5_state;
    CONTENT_KEY CKey;
    HANDLE hFile = NULL;
    FILE * fp = NULL;
    BYTE md5_digest[MD5_HASH_SIZE];
    BYTE Buffer[0x1000];
    char szShortName[SHORT_NAME_SIZE];
    DWORD dwTotalRead = 0;
    DWORD dwBytesRead = 1;
    DWORD dwFileSize;
    bool bWeHaveContentKey;
    int nError = ERROR_SUCCESS;

    // Show the file name to the user
    MakeShortName(szFileName, szShortName, sizeof(szShortName));

    // Show progress
    if((dwFileCount % 5) == 0)
    {
        LogHelper.PrintProgress("Extracting (%u of %u) %s ...", dwFileCount, dwTotalFileCount, szShortName);
    }

    // Open the CASC file
    if(CascOpenFile(hStorage, szFileName, dwLocaleFlags, dwOpenFlags, &hFile))
    {
        // Retrieve the file size
        dwFileSize = CascGetFileSize(hFile, NULL);
        if(dwFileSize == CASC_INVALID_SIZE)
        {
            LogHelper.PrintError("Warning: %s: Failed to retrieve the size", szShortName);
            return GetLastError();
        }

        // Retrieve the content key (aka MD5 of the file content)
        bWeHaveContentKey = CascGetFileInfo(hFile, CascFileContentKey, &CKey, sizeof(CONTENT_KEY), NULL);

        // Initialize the MD5 hashing
        md5_init(&md5_state);

        // Test: Open a local file
//      fp = fopen("e:\\Multimedia\\MPQs\\Work\\Character\\BloodElf\\Female\\BloodElfFemale-TEST.M2", "wb");

        // Load the entire file, piece-by-piece, and calculate MD5
        while(dwBytesRead != 0)
        {
            // Load the chunk of memory
            if(!CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead))
            {
                // Do not report some errors; for example, when the file is encrypted,
                // we can't do much about it
                switch(nError = GetLastError())
                {
                    case ERROR_FILE_ENCRYPTED:
                        if(dwTotalFileCount == 1)
                            LogHelper.PrintMessage("Warning: %s: File is encrypted", szShortName);
                        break;

                    default:
                        LogHelper.PrintMessage("Warning: %s: Read error", szShortName);
                        break;
                }
                break;
            }

            // Write the data to a local file
            if(fp != NULL)
                fwrite(Buffer, 1, dwBytesRead, fp);

            // Hash the file data
            if(bWeHaveContentKey)
                md5_process(&md5_state, (unsigned char *)Buffer, dwBytesRead);
            dwTotalRead += dwBytesRead;
        }

        // Close the local file
        if(fp != NULL)
            fclose(fp);
        fp = NULL;

        // Check whether the total size matches
        if(nError == ERROR_SUCCESS && dwTotalRead != dwFileSize)
        {
            LogHelper.PrintMessage("Warning: %s: dwTotalRead != dwFileSize", szShortName);
            nError = ERROR_FILE_CORRUPT;
        }

        // Check whether the MD5 matches
        if(nError == ERROR_SUCCESS && bWeHaveContentKey)
        {
            md5_done(&md5_state, md5_digest);
            if(memcmp(md5_digest, CKey.Value, MD5_HASH_SIZE))
            {
                LogHelper.PrintMessage("Warning: %s: MD5 mismatch", szShortName);
                nError = ERROR_FILE_CORRUPT;
            }
        }

        // Close the handle
        CascCloseFile(hFile);
    }
    else
    {
        LogHelper.PrintError("Warning: %s: Failed to open", szShortName);
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    return nError;
}

//-----------------------------------------------------------------------------
// Testing functions

static int TestOpenStorage_OpenFile(const char * szStorage, const char * szFileName)
{
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwOpenFlags = 0;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    MakeFullPath(szStorage, szFullPath, MAX_PATH);
    LogHelper.PrintProgress("Opening storage ...");
    if(CascOpenStorage(szFullPath, CASC_LOCALE_ENGB, &hStorage))
    {
        // Check whether the name is the CKey
        if(IsFileKey(szFileName))
            dwOpenFlags |= CASC_OPEN_BY_CKEY;

        // Extract the entire file
        ExtractFile(LogHelper, hStorage, szFileName, dwOpenFlags, 0, 0, 1);

        // Close the storage
        CascCloseStorage(hStorage);
        LogHelper.PrintMessage("Work complete.");
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", szStorage);
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    return nError;
}

static int TestOpenStorage_EnumFiles(const char * szStorage, const TCHAR * szListFile = NULL)
{
    CASC_FIND_DATA cf;
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    HANDLE hFind;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwTotalFileCount = 0;
    DWORD dwFileCount = 0;
    char szShortName[SHORT_NAME_SIZE];
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    MakeFullPath(szStorage, szFullPath, MAX_PATH);
    LogHelper.PrintProgress("Opening storage ...");
    if(CascOpenStorage(szFullPath, 0, &hStorage))
    {
        // Dump the storage
//      CascDumpStorage(hStorage, "E:\\storage-dump.txt");

        // Retrieve the total number of files
        CascGetStorageInfo(hStorage, CascStorageFileCount, &dwTotalFileCount, sizeof(dwTotalFileCount), NULL);

        // Start finding
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &cf, szListFile);
        if(hFind != NULL)
        {
            while(bFileFound)
            {
                // Show the file name to the user
                MakeShortName(cf.szFileName, szShortName, sizeof(szShortName));

                // Show progress
                if((dwFileCount % 5) == 0)
                {
                    LogHelper.PrintProgress("Enumerating (%u of %u) %s ...", dwFileCount, dwTotalFileCount, szShortName);
                }

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &cf);
                dwFileCount++;
            }

            // Close the search handle
            CascFindClose(hFind);
        }

        CascCloseStorage(hStorage);
        LogHelper.PrintMessage("Work complete.");
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", szStorage);
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    return nError;
}

static int TestOpenStorage_ExtractFiles(const char * szStorage, const TCHAR * szListFile)
{
    CASC_FIND_DATA cf;
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    HANDLE hFind;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwTotalFileCount = 0;
    DWORD dwFileCount;
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    MakeFullPath(szStorage, szFullPath, MAX_PATH);
    LogHelper.PrintProgress("Opening storage ...");
    if(CascOpenStorage(szFullPath, 0, &hStorage))
    {
        // Dump the storage
//      LogHelper.PrintProgress("Dumping storage ...");
//      CascDumpStorage(hStorage, "E:\\storage-dump.txt");

        // Retrieve the total file count
        CascGetStorageInfo(hStorage, CascStorageFileCount, &dwTotalFileCount, sizeof(dwTotalFileCount), NULL);
        dwFileCount = 0;

        // Search the storage
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &cf, szListFile);
        if(hFind != NULL)
        {
            // Search the storage
            while(bFileFound)
            {
                // Extract the file
                ExtractFile(LogHelper, hStorage, cf.szFileName, cf.dwOpenFlags, cf.dwLocaleFlags, dwFileCount, dwTotalFileCount);

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &cf);
                dwFileCount++;
            }

            // Close the search handle
            CascFindClose(hFind);
        }

        // Close the storage
        CascCloseStorage(hStorage);
        LogHelper.PrintMessage("Work complete.");
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", szStorage);
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }
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

//-----------------------------------------------------------------------------
// Storage list

static STORAGE_INFO StorageInfo[] = 
{
/*
    {"2014 - Heroes of the Storm/29049", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    {"2014 - Heroes of the Storm/30027", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    {"2014 - Heroes of the Storm\\30414\\HeroesData\\config\\09\\32", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    {"2014 - Heroes of the Storm/30414/", "mods\\heroesdata.stormmod\\base.stormdata\\cutscenes\\frameabathur.stormcutscene"},
    {"2014 - Heroes of the Storm/31726", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"2014 - Heroes of the Storm/39445/HeroesData", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"2014 - Heroes of the Storm/50286/HeroesData", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    {"2015 - Diablo III/30013", "ENCODING"},

    {"2015 - Overwatch/24919/casc/data", "ROOT"},
    {"2015 - Overwatch/47161", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

    {"2016 - Starcraft II/45364/\\/\\/\\", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    {"2016 - WoW/18125", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18379", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18865", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18888", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19116", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19342-root-file-cut", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19342-with-patch", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19678-after-patch", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/21742", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/22267", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/23420", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
*/
    {"2017 - Starcraft1/2457", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4037", "music\\radiofreezerg.ogg"},

    {"2018 - New CASC/00001", "ROOT"},
    {"2018 - New CASC/00002", "ENCODING"},

    {NULL}
};

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

    //
    // Single tests
    //                                   

    TestOpenStorage_OpenFile("2018 - New CASC/00001", "DOWNLOAD");
//  TestOpenStorage_EnumFiles("2016 - Starcraft II/45364", szListFile);
//  TestOpenStorage_ExtractFiles("2014 - Heroes of the Storm/39445"), NULL);

    //
    // Tests for OpenStorage + ExtractFile
    //
/*
    for(size_t i = 0; StorageInfo[i].szPath != NULL; i++)
    {
        // Attempt to open the storage and extract single file
        nError = TestOpenStorage_OpenFile(StorageInfo[i].szPath, StorageInfo[i].szFile);
        if(nError != ERROR_SUCCESS)
            break;
    }
*/
    //
    // Tests for OpenStorage + EnumAllFiles + ExtractAllFiles
    //

    for(size_t i = 0; StorageInfo[i].szPath != NULL; i++)
    {
        // Attempt to open the storage and extract single file
        nError = TestOpenStorage_ExtractFiles(StorageInfo[i].szPath, szListFile);
        if(nError != ERROR_SUCCESS)
            break;
    }

#ifdef _MSC_VER                                                          
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return nError;
}
