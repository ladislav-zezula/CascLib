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
#include <time.h>

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
    const char * szPath;                        // Path to the CASC storage
    const char * szHash;                        // MD5 of all files extracted sequentially
    const char * szFile;                        // Example file in the storage
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
    return (ConvertStringToBinary(szFileName, MD5_STRING_SIZE, KeyBuffer) == ERROR_SUCCESS);
}

// Compares the expected hash with the real one. If they match, returns "match"
// If the expected hash is not available, returns empty string
static const char * GetHashResult(const char * szExpectedHash, const char * szFinalHash)
{
    if(szExpectedHash != NULL)
    {
        return (_stricmp(szExpectedHash, szFinalHash) == 0) ? " (match)" : " (HASH MISMATCH)";
    }
    else
    {
        return "";
    }
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

    // If the folder name is already a full path, do nothing
    if(isalpha(szStorage[0]) && szStorage[1] == ':' && szStorage[2] == '\\')
    {
        while(szStorage[0] != 0 && szBuffer < szBufferEnd)
            *szBuffer++ = *szStorage++;
    }
    else
    {
        // Copy the path prefix
        while(szBuffer < szBufferEnd && szPathRoot[0] != 0)
            *szBuffer++ = *szPathRoot++;

        // Append the separator
        if(szBuffer < szBufferEnd)
            *szBuffer++ = PATH_SEP_CHAR;

        // Append the rest
        while(szBuffer < szBufferEnd && szStorage[0] != 0)
            *szBuffer++ = *szStorage++;
    }

    // Append zero and exit
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
    FILE * fpListFile = NULL)
{
    hash_state md5_state;
    CONTENT_KEY CKey;
    HANDLE hFile = NULL;
    BYTE md5_digest[MD5_HASH_SIZE];
    BYTE Buffer[0x1000];
    char szShortName[SHORT_NAME_SIZE];
    DWORD dwTotalRead = 0;
    DWORD dwBytesRead = 1;
    DWORD dwFileSize;
    bool bWeHaveContentKey;
    bool bHashFileContent = true;
    bool bIsWatchedFile = false;
    int nError = ERROR_SUCCESS;

    // Show the file name to the user
    MakeShortName(szFileName, szShortName, sizeof(szShortName));

    // Show progress
    if((LogHelper.FileCount % 5) == 0)
    {
        LogHelper.PrintProgress("Extracting (%u of %u) %s ...", LogHelper.FileCount, LogHelper.TotalFiles, szShortName);
    }

//  bIsWatchedFile = (_stricmp(szFileName, "fd45b0f59067a8dda512b740c782cd70") == 0);

#ifdef CASC_STRICT_DATA_CHECK
    dwOpenFlags |= CASC_STRICT_DATA_CHECK;
#endif

    // Open the CASC file
    if(CascOpenFile(hStorage, szFileName, 0, dwOpenFlags, &hFile))
    {
        // Retrieve the file size
        dwFileSize = CascGetFileSize(hFile, NULL);
        if(dwFileSize == CASC_INVALID_SIZE)
        {
            LogHelper.PrintError("Warning: %s: Failed to retrieve the size", szShortName);
            return GetLastError();
        }

        // Initialize the per-file hashing
        bWeHaveContentKey = CascGetFileInfo(hFile, CascFileContentKey, &CKey, sizeof(CONTENT_KEY), NULL);
        if(bHashFileContent && bWeHaveContentKey)
            md5_init(&md5_state);

        // Write the file name to a local file
        if (fpListFile != NULL)
            fprintf(fpListFile, "%s\n", szFileName);

        // Test: Open a local file
        // fp = fopen("e:\\Multimedia\\MPQs\\Work\\Character\\BloodElf\\Female\\BloodElfFemale-TEST.M2", "wb");

        // Load the entire file, piece-by-piece, and calculate MD5
        while(dwBytesRead != 0)
        {
            // Show some extra info for watched file(s)
            if (bIsWatchedFile)
            {
                LogHelper.PrintMessage("Extracting \"%s\" (%08X extracted)", szFileName, dwTotalRead);
            }

            // Load the chunk of file
            if(!CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead))
            {
                // Do not report some errors; for example, when the file is encrypted,
                // we can't do much about it. Only report it if we are going to extract one file
                switch(nError = GetLastError())
                {
                    case ERROR_FILE_ENCRYPTED:
                        if(LogHelper.TotalFiles == 1)
                            LogHelper.PrintMessage("Warning: %s: File is encrypted", szShortName);
                        break;

                    default:
                        LogHelper.PrintMessage("Warning: %s: Read error (offset %08X)", szShortName, dwTotalRead);
                        break;
                }
                break;
            }

            // Write the data to a local file
//          if(fp != NULL)
//              fwrite(Buffer, 1, dwBytesRead, fp);

            // Per-file hashing
            if(bHashFileContent && bWeHaveContentKey)
                md5_process(&md5_state, Buffer, dwBytesRead);

            // Per-storage hashing
            if(LogHelper.HasherReady)
                LogHelper.HashData(Buffer, dwBytesRead);

            dwTotalRead += dwBytesRead;
        }

        // Close the local file
//      if(fp != NULL)
//          fclose(fp);
//      fp = NULL;

        // Check whether the total size matches
        if(nError == ERROR_SUCCESS && dwTotalRead != dwFileSize)
        {
            LogHelper.PrintMessage("Warning: %s: dwTotalRead != dwFileSize", szShortName);
            nError = ERROR_FILE_CORRUPT;
        }

        // Check whether the MD5 matches
        if(nError == ERROR_SUCCESS && bHashFileContent && bWeHaveContentKey)
        {
            md5_done(&md5_state, md5_digest);
            if(memcmp(md5_digest, CKey.Value, MD5_HASH_SIZE))
            {
                LogHelper.PrintMessage("Warning: %s: MD5 mismatch", szShortName);
                nError = ERROR_FILE_CORRUPT;
            }
        }

        // Increment the total number of files
        LogHelper.TotalBytes = LogHelper.TotalBytes + dwTotalRead;
        LogHelper.FileCount++;

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

    // Setup the counters
    LogHelper.TotalFiles = 1;

    // Open the storage directory
    MakeFullPath(szStorage, szFullPath, MAX_PATH);
    LogHelper.PrintProgress("Opening storage ...");
    if(CascOpenStorage(szFullPath, CASC_LOCALE_ENGB, &hStorage))
    {
        // Dump the storage
//      CascDumpStorage(hStorage, "E:\\storage-dump.txt");
//      CascGetFileId(hStorage, szFileName);

        // Check whether the name is the CKey
        dwOpenFlags |= IsFileKey(szFileName) ? CASC_OPEN_BY_CKEY : 0;

        // Extract the entire file
        ExtractFile(LogHelper, hStorage, szFileName, dwOpenFlags);

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
    FILE * fp;
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
//      fp = fopen("E:\\110-storage-dump.txt", "wt");

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

                if(fp != NULL)
                    fprintf(fp, "%s\n", cf.szFileName);

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

        if(fp != NULL)
            fclose(fp);

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

static int TestOpenStorage_ExtractFiles(const char * szStorage, const char * szExpectedHash, const TCHAR * szListFile)
{
    CASC_FIND_DATA cf;
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    HANDLE hFind;
//  FILE * fp = NULL;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwTotalFileCount = 0;
    const char * szFinalHash;
    time_t Duration;
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
        LogHelper.TotalFiles = dwTotalFileCount;

        // Init the hasher
        LogHelper.SetStartTime();
        LogHelper.InitHasher();

//      fp = fopen("E:\\filelist-new.txt", "wt");

        // Search the storage
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &cf, szListFile);
        if(hFind != NULL)
        {
            // Search the storage
            while(bFileFound)
            {
                // Extract the file
                ExtractFile(LogHelper, hStorage, cf.szFileName, cf.dwOpenFlags /*, fp */);

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &cf);
            }

            // Close the search handle
            CascFindClose(hFind);
        }

        // Catch the hash and time
        szFinalHash = LogHelper.GetHash();
        Duration = LogHelper.SetEndTime();

        // Close the file stream
//      if (fp != NULL)
//          fclose(fp);

        // Close the storage
        CascCloseStorage(hStorage);

        // Show the summary
        LogHelper.PrintMessage("Extracted: %u of %u files (%llu bytes total)", LogHelper.FileCount, LogHelper.TotalFiles, LogHelper.TotalBytes);
        LogHelper.PrintMessage("Data Hash: %s%s", szFinalHash, GetHashResult(szExpectedHash, szFinalHash));
        LogHelper.PrintMessage("TotalTime: %u second(s)", Duration);
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

    {"2014 - Heroes of the Storm/29049", "2d0209bb094127eb970f53ba29904b7d", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    {"2014 - Heroes of the Storm/30027", "b9cf425da4d836b0b1fabb6702c24111", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    {"2014 - Heroes of the Storm\\30414\\HeroesData\\config\\09\\32", "c07afadc372bffccf70b93533b4f1845", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    {"2014 - Heroes of the Storm/31726", "2f4c8af4d864f6ed33f0a19a55a1ee8c", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"2014 - Heroes of the Storm/39445/HeroesData", "ee5bd644554fe660a47205b3a37f4b20", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"2014 - Heroes of the Storm/50286/HeroesData", "8ee62ff0b959992854a7faa3a4c4efc3", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"2014 - Heroes of the Storm/65943", "044646b8cd27cfe9115bd55810955d40", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
/*
    {"2015 - Diablo III/30013", "a0b42485b79dae47069d86784564d884", "ENCODING"},
    {"2015 - Diablo III/50649", "68d14fb4c20edccd414ca8c935a5ebfe", "ENCODING" },

    {"2015 - Overwatch/24919/data/casc/data", "2c3b0eae9b059e64ad605270e5f3fb42", "ROOT"},
    {"2015 - Overwatch/47161", "d12b77b585ce708f1af3b1b7776a1fb0", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},
*/
    {"2016 - Starcraft II/45364/\\/\\/\\", "fc13de3bbca74f907f967afb9f8db830", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},
/*
    {"2016 - WoW/18125", "af477d5cb467c07fef5764473b0a1155", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18379", "e6879ab7cbd93d48718fd56633bcdcbc", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18865", "9859fbb72f24153532b33787ac875c8d", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18888", "91b074918ccac080355b2a76ba9a8d3b", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19116", "cf3a4ff622fc7176ce6721f6eaf7cd2c", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19342", "f9e7c440158c07f0fb8ec9a55a7679d2", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19342-with-patch", "678d497e556b80954e4b2ce0fd90cd78", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19678-after-patch", "a31378fbdc261e4470fc8be1175a6210", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/21742", "740a369bf6a1b81913bda719f4877840", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/22267", "f7da13a344e1d5d35a5cf85fd30831e2", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/23420", "86c513a7d6c0b3edb13e2839d96b5738", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},

    {"2017 - Starcraft1/2457", "ee7ec3feb3636a49604d76cd1330e6cc", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4037", "e3b1fbab5301fb40c603cafac3d51cf8", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4261", NULL, "music\\radiofreezerg.ogg"},

    {"2018 - New CASC/00001", "971803daed7ea8685a94d9c22c5cffe6", "ROOT"},
    {"2018 - New CASC/00002", "82b381a8d79907c9fd4b19e36d69078c", "ENCODING"},

    {"2018 - Warcraft III/9655", "ee0d4eea919860b99b8b7204020a5650", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
*/
    {NULL}
};

//-----------------------------------------------------------------------------
// Main

int main(int argc, char * argv[])
{
    const TCHAR * szListFile = _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile.txt");
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

//  TestOpenStorage_OpenFile("2014 - Heroes of the Storm/29049", "fd45b0f59067a8dda512b740c782cd70");
//  TestOpenStorage_OpenFile("z:\\47161", "ROOT");
//  TestOpenStorage_EnumFiles("2016 - WoW/23420", NULL);
//  TestOpenStorage_ExtractFiles("2014 - Heroes of the Storm/29049", "2d0209bb094127eb970f53ba29904b7d", szListFile);
/*
    //
    // Tests for OpenStorage + ExtractFile
    //

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
        nError = TestOpenStorage_ExtractFiles(StorageInfo[i].szPath, StorageInfo[i].szHash, szListFile);
        if(nError != ERROR_SUCCESS)
            break;
    }

#ifdef _MSC_VER                                                          
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return nError;
}
