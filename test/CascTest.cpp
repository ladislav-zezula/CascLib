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
// Local variables

    const TCHAR * szListFile = _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile.txt");

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

static FILE * OpenOutputTextFile(HANDLE hStorage, const char * szFormat)
{
    CASC_STORAGE_PRODUCT ProductInfo;
    FILE * fp = NULL;
    char szOutFileName[MAX_PATH];

    if(CascGetStorageInfo(hStorage, CascStorageProduct, &ProductInfo, sizeof(CASC_STORAGE_PRODUCT), NULL))
    {
        sprintf(szOutFileName, szFormat, ProductInfo.szProductName, ProductInfo.dwBuildNumber);
        fp = fopen(szOutFileName, "wt");
    }

    return fp;
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

static int ExtractFile(
    TLogHelper & LogHelper,
    HANDLE hStorage,
    const char * szFileName,
    DWORD dwOpenFlags,
    FILE * fp = NULL)
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
        // Retrieve the file size
        dwFileSize = CascGetFileSize(hFile, NULL);
        if(dwFileSize == CASC_INVALID_SIZE)
        {
            if(fp != NULL)
                fprintf(fp, "GetFileSizeFail: %s\n", szFileName);
            LogHelper.PrintError("Warning: %s: Failed to get file size", szShortName);
            return GetLastError();
        }

        // Initialize the per-file hashing
        bWeHaveContentKey = CascGetFileInfo(hFile, CascFileContentKey, &CKey, sizeof(CONTENT_KEY), NULL);
        if(bHashFileContent && bWeHaveContentKey)
            md5_init(&md5_state);

        // Write the file name to a local file
        //if (fp != NULL)
        //   fprintf(fp, "%s\n", szFileName);

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
        LogHelper.PrintError("Warning: %s: Open error", szShortName);
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
    FILE * fp = NULL;
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
        fp = OpenOutputTextFile(hStorage, "\\list-%s-%u-002.txt");

        // Dump the storage
//      LogHelper.PrintProgress("Dumping storage ...");
//      CascDumpStorage(hStorage, "E:\\storage-dump.txt");
//      CascDumpStorage(hStorage, NULL);

        // Retrieve the total number of files
        CascGetStorageInfo(hStorage, CascStorageTotalFileCount, &dwTotalFileCount, sizeof(dwTotalFileCount), NULL);

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
                if(fp != NULL)
                    fprintf(fp, "%s\n", cf.szFileName);
                assert(cf.szFileName[0] != 0);

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

            // The file counts should match
            assert(dwFileCount == dwTotalFileCount);

            // Close the search handle
            CascFindClose(hFind);
        }

        if(fp != NULL)
            fclose(fp);
        CascCloseStorage(hStorage);

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
    FILE * fp = NULL;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwTotalFileCount = 0;
    DWORD dwFileCount = 0;
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
        CascGetStorageInfo(hStorage, CascStorageTotalFileCount, &dwTotalFileCount, sizeof(dwTotalFileCount), NULL);
        LogHelper.TotalFiles = dwTotalFileCount;

        // Init the hasher
        LogHelper.InitHashers();

        // Search the storage
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &cf, szListFile);
        if(hFind != NULL)
        {
            //fp = fopen("E:\\fail-get-file-size.txt", "wt");

            // Search the storage
            while(bFileFound)
            {
                // Add the file name to the name hash
                LogHelper.HashName(cf.szFileName);

                // Extract the file if available locally
                if(cf.bFileAvailable)
                {
                    ExtractFile(LogHelper, hStorage, cf.szFileName, 0, fp);
                }

                // Find the next file in CASC
                bFileFound = CascFindNextFile(hFind, &cf);
                dwFileCount++;
            }

            // The file counts should match
            assert(dwFileCount == dwTotalFileCount);

            // Close the search handle
            if(fp != NULL)
                fclose(fp);
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
    {"2014 - Heroes of the Storm/29049", "98396c1a521e5dee511d835b9e8086c7", "8febac8275e204800e5a4da0259e91c9", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
//  {"2014 - Heroes of the Storm/30027", "48d8027d1f1f52c22a6c2999de039663", "ee00dd0f415aaae7b517964af9227871", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
//  {"2014 - Heroes of the Storm/30414", "a38b7ada09d58a0ef44f01f5c9238eeb", "fc68e89a4492e9959c1b17dd24a89fce", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
//  {"2014 - Heroes of the Storm/31726", "62cedf27ba1cda971fe023169de91d6f", "ca66c0a0850f832434b2e7dd0773747c", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"2014 - Heroes of the Storm/39445", "c672b26f8f14ab2e68a9f9d7d6ca6062", "5ab7d596b5d6025072d7f331b3d7167a", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"2014 - Heroes of the Storm/50286", "d1d57e83cbd72cbecd76916c22f6c4b6", "572598a728ac46dd18278636394c4fbc", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
//  {"2014 - Heroes of the Storm/65943", "9dd64ac0f43d1e67bcbc3ade27e6faa6", "fa9ec8110527fe0855ad68951d35f4de", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    {"2015 - Diablo III/30013",          "468e6a5aa8d786ac038ed2cfe5119b54", "b642f0dd232c591f05e6bdd65e28da82", "ENCODING"},
//  {"2015 - Diablo III/50649",          "ff896a9f3c52f23326e9d88f133443d0", "d628554699d6b0764757ec421645caab", "ENCODING"},

//  {"2015 - Overwatch/24919/data/casc", "96cada34b1e98552ff16224774a8e20a", "348a299aaba1f3d77d7592c55ee5c22a", "ROOT"},
//  {"2015 - Overwatch/47161",           "fee1d8dc4e763eaa49dcabeebc3875fc", "e6f94b43188a791c05e3a9468e8f3417", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

//  {"2016 - Starcraft II/45364/\\/",    "9bd5b4588ad6b2976a63258b58c52ac3", "c07ffe613e73a4966ab2fc137130f86e", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    {"2016 - WoW/18125",                 "b31531af094f78f58592249c4d216a8e", "5606e21ce4b493ad1c6ce189818245ae", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/18379",                 "501f23575a042e96a640fc0b27e38f2e", "5a47a7d5a943493e2751560df2073379", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/18865",                 "3ca3739c5941dbe07d19d9eef6c4caf1", "798903b59dee4f50f0668b08d7ad3443", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/18888",                 "e9e74cdc739a2a3b7f75adc5889c3982", "d276fb39b7ece7a51bd38e0940c038db", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/19116",                 "df3233caa9f450ac8bd4896dedf458fe", "7f17f5b9334bea3f12f33782a23879a5", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/19342",                 "08435dd4aa28f3026c4dd353515e6131", "4174b8c855c0c466f2f270f5bd6b1318", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/21742",                 "a4e10c21d460426c8a0d0558fdc1cceb", "e031c7f8cd732f5aa048d88277a00d3f", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/22267",                 "7bce9384681232aa7a6a1075b560a22b", "b774baa36e794253c3975c51ff3aa778", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/23420",                 "3aef7cd675ab850005cc693386d09901", "8d9e79eb7913434f5629b193b1eb0fa4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/29981",                 "125ff65f0bffd8ff502cb7f1fb383247", "081d525b3235de6697897d5dc8cba835", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/30123",                 NULL, "1d84d8682149533d90fdbc46be2fcfc3", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
  
//  {"2017 - Starcraft1/2457",           NULL, "7a953a1e8713feeebf3a6d29e85731fb", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4037",           "bb2b76d657a841953fe093b75c2bdaf6", "5bf1dc985f0957d3ba92ed9c5431b31b", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4261",           "59ea96addacccb73938fdf688d7aa29b", "4bade608b78b186a90339aa557ad3332", "music\\radiofreezerg.ogg"},

    {"2018 - New CASC/00001",            "43d576ee81841a63f2211d43a50bb593", "2b7829b59c0b6e7ca6f6111bfb0dc426", "ROOT"},
    {"2018 - New CASC/00002",            "1c76139b51edd3ee114b5225d1b44c86", "4289e1e095dbfaec5dd926b5f9f22c6f", "ENCODING"},

    {"2018 - Warcraft III/09655",        "b1aeb7180848b83a7a3132cba608b254", "5d0e71a47f0b550de6884cfbbe3f50e5", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"2018 - Warcraft III/11889",        "f084ee1713153d8a15f1f75e94719aa8", "3541073dd77d370a01fbbcadd029477e", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },

    {NULL}
};

//-----------------------------------------------------------------------------
// Main

int main(int argc, char * argv[])
{
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

    //TestOpenStorage_EnumFiles("2014 - Heroes of the Storm\\29049", szListFile);
    //TestOpenStorage_EnumFiles("2014 - Heroes of the Storm\\39445", szListFile);
    //TestOpenStorage_EnumFiles("2014 - Heroes of the Storm\\50286", szListFile);
    //TestOpenStorage_EnumFiles("2015 - Diablo III\\30013", szListFile);
    //TestOpenStorage_EnumFiles("2016 - WoW\\18125", szListFile);
    //TestOpenStorage_EnumFiles("2018 - New CASC\\00001", szListFile);
    //TestOpenStorage_EnumFiles("2018 - New CASC\\00002", szListFile);
    //TestOpenStorage_EnumFiles("2018 - Warcraft III\\11889", NULL);

    //
    // Run the tests for every storage in my collection
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
