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

static const TCHAR * szListFile1 = _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile6x.txt");
static const TCHAR * szListFile2 = _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile8x.csv");

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

static const TCHAR * GetTheProperListfile(HANDLE hStorage, const TCHAR * szListFile)
{
    DWORD dwFeatures = 0;

    // If the caller gave a concrete listfile, use that one
    if(szListFile != NULL)
        return szListFile;

    // Check the storage format. If WoW 8.2+, we need the CSV listfile
    CascGetStorageInfo(hStorage, CascStorageFeatures, &dwFeatures, sizeof(dwFeatures), NULL);
    if(dwFeatures & CASC_FEATURE_FNAME_HASHES_OPTIONAL)
        return szListFile2;

    if(dwFeatures & CASC_FEATURE_FNAME_HASHES)
        return szListFile1;

    return NULL;
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

        // Retrieve the listfile needed for enumerating files
        szListFile = GetTheProperListfile(hStorage, szListFile);

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

static int TestOpenStorage_ExtractFiles(const char * szStorage, const char * szExpectedNameHash, const char * szExpectedDataHash, const TCHAR * szListFile = NULL)
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

        // Retrieve the listfile needed for enumerating files
        szListFile = GetTheProperListfile(hStorage, szListFile);

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
    {"2014 - Heroes of the Storm/30027", "6bcbe7c889cc465e4993f92d6ae1ee75", "54ed1440368de80723eddd89931fe191", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    {"2014 - Heroes of the Storm/30414", "4b5d1f21de95c2a448684f98cc157f10", "ff32ed33bfcb40e01bf75c8df381eca5", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    {"2014 - Heroes of the Storm/31726", "8b7633e519b78c96c85a1faa1c9f151f", "a0fd31d04f1bd6c5b3532c72592abf19", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"2014 - Heroes of the Storm/39445", "c672b26f8f14ab2e68a9f9d7d6ca6062", "5ab7d596b5d6025072d7f331b3d7167a", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"2014 - Heroes of the Storm/50286", "d1d57e83cbd72cbecd76916c22f6c4b6", "572598a728ac46dd18278636394c4fbc", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"2014 - Heroes of the Storm/65943", "c5d75f4e12dbc05d4560fe61c4b88773", "981b882e090bdc027910ba70744c0e2c", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    {"2015 - Diablo III/30013",          "468e6a5aa8d786ac038ed2cfe5119b54", "b642f0dd232c591f05e6bdd65e28da82", "ENCODING"},
    {"2015 - Diablo III/50649",          "0889067a005b92186fc0df0553845106", "84f4d3c1815afd69fc7edd8fb403815d", "ENCODING"},

    {"2015 - Overwatch/24919/data/casc", "53afa15570c29bd40bba4707b607657e", "117073f6e207e8cdcf43b705b80bf120", "ROOT"},
    {"2015 - Overwatch/47161",           "53db1f3da005211204997a6b50aa71e1", "434d7ff16fe0d283a2dacfc1390cb16e", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

    {"2016 - Starcraft II/45364/\\/",    "28f8b15b5bbd87c16796246eac3f800c", "4f5d1cd5453557ef7e10d35975df2b12", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    {"2016 - WoW/18125",                 "b31531af094f78f58592249c4d216a8e", "5606e21ce4b493ad1c6ce189818245ae", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18379",                 "6beec00c8a16f873b4297a2262e60a82", "1a86f24bfb1076bcbec9a4c9a32b4aeb", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18865",                 "2f97a490a7c321dda7947f8c6cd7aa78", "d705c852dbbbc1a7e7ebfd43a1a041f1", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18888",                 "44586978f5ee214ccfef03971435e164", "b24db02e56fc65ee07646658de5698c4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19116",                 "a3be9cfd4a15ba184e21eed9ec90417b", "0b68a11d1eae6645f18a453d39aba23a", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19342",                 "66f0de0cff477e1d8e982683771f1ada", "8e5f45f6892fc6e7a6c90a8544a9383b", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/21742",                 "a357c3cbed98e83ac5cd394ceabc01e8", "b5917c7b388d8d9c0f65f97c1738cb84", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},

    {"2016 - WoW/22267",                 "7bce9384681232aa7a6a1075b560a22b", "b774baa36e794253c3975c51ff3aa778", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/23420",                 "3aef7cd675ab850005cc693386d09901", "8d9e79eb7913434f5629b193b1eb0fa4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/29981",                 "125ff65f0bffd8ff502cb7f1fb383247", "081d525b3235de6697897d5dc8cba835", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/30123",                 NULL, "1d84d8682149533d90fdbc46be2fcfc3", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
  
    {"2017 - Starcraft1/2457",           NULL, "7a953a1e8713feeebf3a6d29e85731fb", "music\\radiofreezerg.ogg"},
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

int main(void)
{
    int nError = ERROR_SUCCESS;

    printf("\n");

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif  // defined(_MSC_VER) && defined(_DEBUG)

    //
    // Single tests
    //

    //TestOpenStorage_EnumFiles("2014 - Heroes of the Storm\\29049");
    //TestOpenStorage_EnumFiles("2014 - Heroes of the Storm\\39445");
    //TestOpenStorage_EnumFiles("2014 - Heroes of the Storm\\50286");
    //TestOpenStorage_EnumFiles("2015 - Diablo III\\30013");
    //TestOpenStorage_EnumFiles("2016 - WoW\\18125");
    //TestOpenStorage_OpenFile("2016 - WoW\\30123", "world/expansion07/doodads/gnome/8gn_gnome_antigravitybackpack_fx.m2");
    //TestOpenStorage_EnumFiles("2018 - New CASC\\00001");
    //TestOpenStorage_EnumFiles("2018 - New CASC\\00002");
    //TestOpenStorage_EnumFiles("2018 - Warcraft III\\11889");

    //
    // Run the tests for every storage in my collection
    //
    for(size_t i = 0; StorageInfo[i].szPath != NULL; i++)
    {
        // Attempt to open the storage and extract single file
        nError = TestOpenStorage_ExtractFiles(StorageInfo[i].szPath, StorageInfo[i].szNameHash, StorageInfo[i].szDataHash, NULL);
//      nError = TestOpenStorage_EnumFiles(StorageInfo[i].szPath);
        if(nError != ERROR_SUCCESS)
            break;
    }

#ifdef _MSC_VER
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return nError;
}
