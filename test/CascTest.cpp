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
#include <io.h>
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
    const char * szNameHash;                    // MD5 of all file names extracted sequentially
    const char * szDataHash;                    // MD5 of all file data extracted sequentially
    const char * szFile;                        // Example file in the storage
} STORAGE_INFO, *PSTORAGE_INFO;

//-----------------------------------------------------------------------------
// Local functions

static bool IsFileKey(const char * szFileName)
{
    BYTE KeyBuffer[MD5_HASH_SIZE];
    bool bIsKey = false;

    if(szFileName && szFileName[0])
    {
        // The length must be at least the length of the CKey
        if(strlen(szFileName) < MD5_STRING_SIZE)
            return false;

        // Convert the BLOB to binary.
        bIsKey = (ConvertStringToBinary(szFileName, MD5_STRING_SIZE, KeyBuffer) == ERROR_SUCCESS);
    }

    return bIsKey;
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

static void MakeShortName(const char * szFileName, DWORD dwOpenFlags, char * szShortName, size_t ccShortName)
{
    char * szShortNameEnd = szShortName + ccShortName - 1;
    size_t nLength;

    // Is that a file data id?
    switch(dwOpenFlags & CASC_OPEN_TYPE_MASK)
    {
        case CASC_OPEN_BY_NAME:

            // Is the name too long?
            if((nLength = strlen(szFileName)) >= ccShortName)
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
            break;

        case CASC_OPEN_BY_FILEID:
            sprintf(szShortName, "FILE%08X.dat", CASC_FILE_DATA_ID_FROM_STRING(szFileName));
            break;

        default:
            szShortName[0] = 0;
            assert(false);
            break;
    }
}

static TCHAR * MakeFullPath(const char * szStorage, TCHAR * szBuffer, size_t ccBuffer)
{
    TCHAR * szBufferEnd = szBuffer + ccBuffer - 1;
    const char * szPathRoot = CASC_PATH_ROOT;

    // If we can access the file directly, use the path as-is
    if(_access(szStorage, 0) != -1)
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

static void TestStorageGetTagInfo(HANDLE hStorage)
{
    PCASC_STORAGE_TAGS pTags = NULL;
    size_t cbTags = 0;

    CascGetStorageInfo(hStorage, CascStorageTags, pTags, cbTags, &cbTags);
    if(cbTags != 0)
    {
        pTags = (PCASC_STORAGE_TAGS)CASC_ALLOC(BYTE, cbTags);
        if(pTags != NULL)
        {
            CascGetStorageInfo(hStorage, CascStorageTags, pTags, cbTags, &cbTags);
            CASC_FREE(pTags);
        }
    }
}

static void TestFileGetFullInfo(HANDLE hFile)
{
    PCASC_FILE_FULL_INFO pFileInfo = NULL;
    size_t cbFileInfo = 0x800;

    // Pre-allocate the file info
    pFileInfo = (PCASC_FILE_FULL_INFO)CASC_ALLOC(BYTE, cbFileInfo);
    if(pFileInfo != NULL)
    {
        if(CascGetFileInfo(hFile, CascFileFullInfo, pFileInfo, cbFileInfo, &cbFileInfo) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            pFileInfo = (PCASC_FILE_FULL_INFO)CASC_REALLOC(BYTE, pFileInfo, cbFileInfo);
            CascGetFileInfo(hFile, CascFileFullInfo, pFileInfo, cbFileInfo, &cbFileInfo);
        }
        CASC_FREE(pFileInfo);
    }
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
    int nError = ERROR_SUCCESS;

    // Show the file name to the user if open succeeded
    MakeShortName(szFileName, dwOpenFlags, szShortName, sizeof(szShortName));

    // Open the CASC file
    if(CascOpenFile(hStorage, szFileName, 0, dwOpenFlags | CASC_STRICT_DATA_CHECK, &hFile))
    {
        // Test retrieving full file info
        TestFileGetFullInfo(hFile);

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

        // Show the progress, if open succeeded
        if((LogHelper.FileCount % 5) == 0)
            LogHelper.PrintProgress("Extracting (%u of %u) %s ...", LogHelper.FileCount, LogHelper.TotalFiles, szShortName);

        // Load the entire file, piece-by-piece, and calculate MD5
        while(dwBytesRead != 0)
        {
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
        LogHelper.PrintError("OpenFailed: %s", szShortName);
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    return nError;
}

//-----------------------------------------------------------------------------
// Testing functions

static int TestOpenStorage_OpenFile(const char * szStorage, const char * szFileName, DWORD dwOpenFlags = 0)
{
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    TCHAR szFullPath[MAX_PATH];
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

        // Check whether the name is the CKey
        if(dwOpenFlags == 0 && IsFileKey(szFileName))
            dwOpenFlags |= CASC_OPEN_BY_CKEY;

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
    TCHAR szFullPath[MAX_PATH];
    DWORD dwTotalFileCount = 0;
    DWORD dwFileCount = 0;
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    MakeFullPath(szStorage, szFullPath, MAX_PATH);
    LogHelper.PrintProgress("Opening storage ...");
    LogHelper.SetStartTime();
    if(CascOpenStorage(szFullPath, 0, &hStorage))
    {
        FILE * fp;
        char szOutFileName[MAX_PATH];

        // Create listfile for dumping
        sprintf(szOutFileName, "E:\\%s", szStorage);
        strcpy(strrchr(szOutFileName, '\\'), "-002.txt");
        fp = fopen(szOutFileName, "wt");

        // Dump the storage
//      LogHelper.PrintProgress("Dumping storage ...");
//      CascDumpStorage(hStorage, "E:\\storage-dump.txt");
//      CascDumpStorage(hStorage, NULL);

        // Retrieve the total number of files
        CascGetStorageInfo(hStorage, CascStorageLocalFileCount, &dwTotalFileCount, sizeof(dwTotalFileCount), NULL);

        // Retrieve the tags
        TestStorageGetTagInfo(hStorage);

        // Start finding
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &cf, szListFile);
        if(hFind != NULL)
        {
            while(bFileFound)
            {
                // There should always be a name
                assert(cf.szFileName[0] != 0);
                fprintf(fp, "%s\n", cf.szFileName);

                // Show the file name to the user
                //if((dwFileCount % 5) == 0)
                //{
                //    char szShortName[SHORT_NAME_SIZE];
                //    MakeShortName(cf.szFileName, 0, szShortName, sizeof(szShortName));
                //    LogHelper.PrintProgress("Enumerating (%u of %u) %s ...", dwFileCount, dwTotalFileCount, szShortName);
                //}

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &cf);
                dwFileCount++;
            }

            // Close the search handle
            CascFindClose(hFind);
        }

        CascCloseStorage(hStorage);
        fclose(fp);

        LogHelper.PrintTotalTime();
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

static int TestOpenStorage_ExtractFiles(const char * szStorage, const char * szExpectedNameHash, const char * szExpectedDataHash, const TCHAR * szListFile)
{
    CASC_FIND_DATA cf;
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    HANDLE hFind;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwTotalFileCount = 0;
    const char * szNameHash;
    const char * szDataHash;
    char szTotalBytes[0x20];
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    MakeFullPath(szStorage, szFullPath, MAX_PATH);
    LogHelper.PrintProgress("Opening storage ...");
    LogHelper.SetStartTime();
    if(CascOpenStorage(szFullPath, 0, &hStorage))
    {
        // Dump the storage
//      LogHelper.PrintProgress("Dumping storage ...");
//      CascDumpStorage(hStorage, "E:\\storage-dump.txt");

        // Retrieve the total file count
        CascGetStorageInfo(hStorage, CascStorageLocalFileCount, &dwTotalFileCount, sizeof(dwTotalFileCount), NULL);
        LogHelper.TotalFiles = dwTotalFileCount;

        // Init the hasher
        LogHelper.InitHashers();

        // Search the storage
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &cf, szListFile);
        if(hFind != NULL)
        {
            // Search the storage
            while(bFileFound)
            {
                // Add the file name to the name hash
                LogHelper.HashName(cf.szFileName);

                // Extract the file if available locally
                if(cf.bFileAvailable)
                {
                    ExtractFile(LogHelper, hStorage, cf.szFileName, 0 /*, fp */);
                }

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &cf);
            }

            // Close the search handle
            CascFindClose(hFind);
        }

        // Catch the hash and time
        szNameHash = LogHelper.GetNameHash();
        szDataHash = LogHelper.GetDataHash();

        // Close the storage
        CascCloseStorage(hStorage);

        // Show the summary
        LogHelper.FormatTotalBytes(szTotalBytes, _countof(szTotalBytes));
        LogHelper.PrintMessage("Extracted: %u of %u files (%s bytes total)", LogHelper.FileCount, LogHelper.TotalFiles, szTotalBytes);
        LogHelper.PrintMessage("Name Hash: %s%s", szNameHash, GetHashResult(szExpectedNameHash, szNameHash));
        LogHelper.PrintMessage("Data Hash: %s%s", szDataHash, GetHashResult(szExpectedDataHash, szDataHash));
        LogHelper.PrintTotalTime();
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

//-----------------------------------------------------------------------------
// Storage list

static STORAGE_INFO StorageInfo[] = 
{
    //- Name of the storage folder -------- Compound file name hash ----------- Compound file data hash ----------- Example file to extract -----------------------------------------------------------
    {"2014 - Heroes of the Storm/29049", "5af1b6b3c6b1444134f0ae43863f22cb", "754d713197f1ba27651483f01155027b", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    {"2014 - Heroes of the Storm/30027", "48d8027d1f1f52c22a6c2999de039663", "ee00dd0f415aaae7b517964af9227871", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    {"2014 - Heroes of the Storm/30414", "a38b7ada09d58a0ef44f01f5c9238eeb", "fc68e89a4492e9959c1b17dd24a89fce", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    {"2014 - Heroes of the Storm/31726", "62cedf27ba1cda971fe023169de91d6f", "ca66c0a0850f832434b2e7dd0773747c", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"2014 - Heroes of the Storm/39445", "e1b332fba8b10c1e267f4a85000a57e9", "95b19b57fde5e44a0d21f5b63a8fb46e", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"2014 - Heroes of the Storm/50286", "4a7df6bdc0e4ec4db53f58f495c44e40", "49d7b2a2efb4b08a43d65da01a931ab9", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"2014 - Heroes of the Storm/65943", "9dd64ac0f43d1e67bcbc3ade27e6faa6", "fa9ec8110527fe0855ad68951d35f4de", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    {"2015 - Diablo III/30013",          "2d1984dbdeeacea9ef43d07c2ee49522", "1d7dcff7d9198bf503a072e5fae41b2e", "ENCODING"},
    {"2015 - Diablo III/50649",          "ff896a9f3c52f23326e9d88f133443d0", "d628554699d6b0764757ec421645caab", "ENCODING"},

    {"2015 - Overwatch/24919/data/casc", "96cada34b1e98552ff16224774a8e20a", "348a299aaba1f3d77d7592c55ee5c22a", "ROOT"},
    {"2015 - Overwatch/47161",           "fee1d8dc4e763eaa49dcabeebc3875fc", "e6f94b43188a791c05e3a9468e8f3417", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

    {"2016 - Starcraft II/45364/\\/",    "9bd5b4588ad6b2976a63258b58c52ac3", "c07ffe613e73a4966ab2fc137130f86e", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    {"2016 - WoW/18125",                 "00631d425536a6b45cdc7cdee2749d6e", "9131b459d8eb34122ee70d6c84280b5f", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18379",                 "501f23575a042e96a640fc0b27e38f2e", "5a47a7d5a943493e2751560df2073379", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18865",                 "3ca3739c5941dbe07d19d9eef6c4caf1", "798903b59dee4f50f0668b08d7ad3443", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18888",                 "e9e74cdc739a2a3b7f75adc5889c3982", "d276fb39b7ece7a51bd38e0940c038db", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19116",                 "df3233caa9f450ac8bd4896dedf458fe", "7f17f5b9334bea3f12f33782a23879a5", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19342",                 "08435dd4aa28f3026c4dd353515e6131", "4174b8c855c0c466f2f270f5bd6b1318", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/21742",                 "a4e10c21d460426c8a0d0558fdc1cceb", "e031c7f8cd732f5aa048d88277a00d3f", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/22267",                 "7bce9384681232aa7a6a1075b560a22b", "b774baa36e794253c3975c51ff3aa778", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/23420",                 "3aef7cd675ab850005cc693386d09901", "8d9e79eb7913434f5629b193b1eb0fa4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/29981",                 "125ff65f0bffd8ff502cb7f1fb383247", "081d525b3235de6697897d5dc8cba835", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/30123",                 NULL, "1d84d8682149533d90fdbc46be2fcfc3", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},

    {"2017 - Starcraft1/2457",           NULL, "7a953a1e8713feeebf3a6d29e85731fb", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4037",           NULL, "e3b1fbab5301fb40c603cafac3d51cf8", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4261",           NULL, "2e35ddd46976326f9301ed04bf84e30c", "music\\radiofreezerg.ogg"},

    {"2018 - New CASC/00001",            NULL, "971803daed7ea8685a94d9c22c5cffe6", "ROOT"},
    {"2018 - New CASC/00002",            NULL, "82b381a8d79907c9fd4b19e36d69078c", "ENCODING"},

    {"2018 - Warcraft III/09655",        NULL, "629ed47991b2c4a29f6411d00cf72fa6", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"2018 - Warcraft III/11889",        NULL, "629ed47991b2c4a29f6411d00cf72fa6", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },

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
    printf("\n");

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif  // defined(_MSC_VER) && defined(_DEBUG)

    //
    // Single tests
    //

//  TestOpenStorage_EnumFiles("2015 - Diablo III\\30013", szListFile);
//  TestOpenStorage_EnumFiles("2014 - Heroes of the Storm\\29049", szListFile);
//  TestOpenStorage_EnumFiles("2015 - Overwatch\\24919", szListFile);
//  TestOpenStorage_EnumFiles("2017 - Starcraft1\\2457", szListFile);
//  TestOpenStorage_EnumFiles("2016 - WoW\\18125", szListFile);
//  TestOpenStorage_EnumFiles("2016 - WoW\\29981", szListFile);
//  TestOpenStorage_EnumFiles("2016 - WoW\\30123", szListFile);
//  TestOpenStorage_EnumFiles("2018 - Warcraft III\\11889", NULL);

    //
    // Tests for OpenStorage + EnumAllFiles + ExtractAllFiles
    //
    for(size_t i = 0; StorageInfo[i].szPath != NULL; i++)
    {
        // Attempt to open the storage and extract single file
        nError = TestOpenStorage_ExtractFiles(StorageInfo[i].szPath, StorageInfo[i].szNameHash, StorageInfo[i].szDataHash, szListFile);
//      nError = TestOpenStorage_EnumFiles(StorageInfo[i].szPath, szListFile);
        if(nError != ERROR_SUCCESS)
            break;
    }

#ifdef _MSC_VER                                                          
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return nError;
}
