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
#include <io.h>
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
#define CASC_PATH_ROOT "/Multimedia/CASC"
#define CASC_WORK_ROOT "/Multimedia/CASC/Work"
#endif

#ifdef PLATFORM_LINUX
#define CASC_PATH_ROOT "/mnt/casc"
#define CASC_WORK_ROOT "/mnt/casc/Work"
#endif

static const char szCircleChar[] = "|/-\\";

#if defined(_MSC_VER) && defined(_DEBUG)
#define GET_TICK_COUNT()  GetTickCount()
#else
#define GET_TICK_COUNT()  0
#endif

#define SHORT_NAME_SIZE 60

//-----------------------------------------------------------------------------
// Local structures

// For local storages
typedef struct _STORAGE_INFO1
{
    LPCSTR szPath;                          // Path to the CASC storage
    LPCSTR szNameHash;                      // MD5 of all file names extracted sequentially
    LPCSTR szDataHash;                      // MD5 of all file data extracted sequentially
    LPCSTR szFile;                          // Example file in the storage
} STORAGE_INFO1, *PSTORAGE_INFO1;

// For online storages
typedef struct _STORAGE_INFO2
{
    LPCSTR szCodeName;                      // Codename of the product
    LPCSTR szRegion;                        // Region of the product. If NULL, CascLib will open the first one in the "versions"
    LPCSTR szFile;                          // Example file in the storage
} STORAGE_INFO2, *PSTORAGE_INFO2;

//-----------------------------------------------------------------------------
// Local variables

static LPCTSTR szListFile1 = _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile6x.txt");
static LPCTSTR szListFile2 = _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile8x.csv");

//-----------------------------------------------------------------------------
// Local functions

static bool IsFileKey(LPCSTR szFileName)
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
static LPCSTR GetHashResult(LPCSTR szExpectedHash, LPCSTR szFinalHash)
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

static void MakeShortName(LPSTR szShortName, size_t ccShortName, CASC_FIND_DATA & cf)
{
    LPSTR szShortNameEnd = szShortName + ccShortName - 1;
    size_t nLength;

    // Is the name too long?
    if((nLength = strlen(cf.szFileName)) >= ccShortName)
    {
        size_t nFirstPart = (ccShortName / 2) - 3;
        size_t nRemaining;

        // Copy the first part
        memcpy(szShortName, cf.szFileName, nFirstPart);
        szShortName += nFirstPart;

        // Copy "..."
        memcpy(szShortName, "...", 3);
        szShortName += 3;

        // Copy the rest
        nRemaining = szShortNameEnd - szShortName;
        memcpy(szShortName, cf.szFileName + nLength - nRemaining, nRemaining);
        szShortName[nRemaining] = 0;
    }
    else
    {
        CascStrCopy(szShortName, ccShortName, cf.szFileName);
    }
}

static LPTSTR CopyPath(LPTSTR szBuffer, LPTSTR szBufferEnd, LPCSTR szSource)
{
    while(szBuffer < szBufferEnd && szSource[0] != 0)
    {
        if(szSource[0] == '\\' || szSource[0] == '/')
            *szBuffer++ = PATH_SEP_CHAR;
        else
            *szBuffer++ = szSource[0];
        
        szSource++;
    }
    
    szBuffer[0] = 0;
    return szBuffer;
}        

static LPTSTR MakeFullPath(LPTSTR szBuffer, size_t ccBuffer, LPCSTR szStorage)
{
    LPTSTR szBufferEnd = szBuffer + ccBuffer - 1;
    LPCSTR szPathRoot = CASC_PATH_ROOT;

    // If we can not access the folder directly, we copy the path root
    if(_access(szStorage, 0) == -1)
    {
        szBuffer = CopyPath(szBuffer, szBufferEnd, szPathRoot);
        szBuffer = CopyPath(szBuffer, szBufferEnd, PATH_SEP_STRING);
    }
    
    // Copy the rest of the path
    return CopyPath(szBuffer, szBufferEnd, szStorage);
}

static LPCTSTR GetTheProperListfile(HANDLE hStorage, LPCTSTR szListFile)
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

static FILE * OpenOutputTextFile(HANDLE hStorage, LPCSTR szFormat)
{
    CASC_STORAGE_PRODUCT ProductInfo;
    FILE * fp = NULL;
    char szOutFileName[MAX_PATH];

    if(CascGetStorageInfo(hStorage, CascStorageProduct, &ProductInfo, sizeof(CASC_STORAGE_PRODUCT), NULL))
    {
        CascStrPrintf(szOutFileName, _countof(szOutFileName), szFormat, ProductInfo.szProductName, ProductInfo.dwBuildNumber);
        fp = fopen(szOutFileName, "wt");
    }

    return fp;
}

static FILE * OpenExtractedFile(HANDLE /* hStorage */, LPCSTR szFormat, LPCSTR szFileName, bool bIsFileDataId)
{
    char szOutFileName[MAX_PATH];
    char szPlainName[MAX_PATH];

    if(bIsFileDataId)
    {
        CascStrPrintf(szPlainName, _countof(szPlainName), "FILE_%08u.dat", CASC_FILE_DATA_ID_FROM_STRING(szFileName));
        szFileName = szPlainName;
    }

    CascStrPrintf(szOutFileName, _countof(szOutFileName), szFormat, GetPlainFileName(szFileName));
    return fopen(szOutFileName, "wt");
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

static bool CascOpenFile(HANDLE hStorage, CASC_FIND_DATA & cf, DWORD dwOpenFlags, HANDLE * PtrHandle)
{
    if(cf.bCanOpenByName)
    {
        return CascOpenFile(hStorage, cf.szFileName, 0, dwOpenFlags, PtrHandle);
    }

    if(cf.bCanOpenByDataId)
    {
        return CascOpenFile(hStorage, CASC_FILE_DATA_ID(cf.dwFileDataId), 0, dwOpenFlags | CASC_OPEN_BY_FILEID, PtrHandle);
    }

    if(cf.bCanOpenByCKey)
    {
        return CascOpenFile(hStorage, cf.CKey, 0, dwOpenFlags | CASC_OPEN_BY_CKEY, PtrHandle);
    }

    assert(false);
    return false;
}

static int ExtractFile(
    TLogHelper & LogHelper,
    HANDLE hStorage,
    CASC_FIND_DATA & cf,
    DWORD dwOpenFlags = 0,
    FILE * fp = NULL)
{
    MD5_CTX md5_ctx;
    HANDLE hFile = NULL;
    BYTE md5_digest[MD5_HASH_SIZE];
    BYTE Buffer[0x1000];
    char szShortName[SHORT_NAME_SIZE];
    DWORD dwTotalRead = 0;
    DWORD dwBytesRead = 1;
    DWORD dwFileSize;
    bool bHashFileContent = true;
    int nError = ERROR_SUCCESS;

    // Show the file name to the user if open succeeded
    MakeShortName(szShortName, sizeof(szShortName), cf);

    // Open the CASC file
    if(CascOpenFile(hStorage, cf, dwOpenFlags | CASC_STRICT_DATA_CHECK, &hFile))
    {
        // Retrieve the file size
        dwFileSize = CascGetFileSize(hFile, NULL);
        if(dwFileSize == CASC_INVALID_SIZE)
        {
            if(fp != NULL)
                fprintf(fp, "GetFileSizeFail: %s\n", cf.szFileName);
            LogHelper.PrintError("Warning: %s: Failed to get file size", szShortName);
            return GetLastError();
        }

        // Initialize the per-file hashing
        if(bHashFileContent && cf.bCanOpenByCKey)
            MD5_Init(&md5_ctx);

        // Write the file name to a local file
//      if (fp != NULL)
//          fprintf(fp, "%s\n", cf.szFileName);

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

            // write the file data
            if(fp != NULL)
                fwrite(Buffer, 1, dwBytesRead, fp);

            // Per-file hashing
            if(bHashFileContent && cf.bCanOpenByCKey)
                MD5_Update(&md5_ctx, Buffer, dwBytesRead);

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
        if(nError == ERROR_SUCCESS && bHashFileContent && cf.bCanOpenByCKey)
        {
            MD5_Final(md5_digest, &md5_ctx);
            if(memcmp(md5_digest, cf.CKey, MD5_HASH_SIZE))
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

static int TestStorage_EnumFiles(TLogHelper & LogHelper, HANDLE hStorage, LPCTSTR szListFile)
{
    CASC_FIND_DATA cf;
    HANDLE hFind;
    FILE * fp = NULL;
    DWORD dwTotalFileCount = 0;
    DWORD dwFileCount = 0;
    bool bFileFound = true;

    if(hStorage != NULL)
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
        if (hFind != NULL)
        {
            while (bFileFound)
            {
                // There should always be a name
                if (fp != NULL)
                    fprintf(fp, "%s\n", cf.szFileName);
                assert(cf.szFileName[0] != 0);

                // Show the file name to the user
                //if((dwFileCount % 5) == 0)
                //{
                //    char szShortName[SHORT_NAME_SIZE];
                //    MakeShortName(szShortName, sizeof(szShortName), cf);
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

        if (fp != NULL)
            fclose(fp);
        CascCloseStorage(hStorage);

        LogHelper.PrintTotalTime();
        LogHelper.PrintMessage("Work complete.");
    }

    return ERROR_SUCCESS;
}

static int TestOpenStorage_OpenFile(LPCSTR szStorage, LPCSTR szFileName, DWORD dwOpenFlags = 0, bool bIsFileDataId = false)
{
    CASC_FIND_DATA cf = {0};
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    FILE * fp = NULL;
    TCHAR szFullPath[MAX_PATH];
    int nError = ERROR_SUCCESS;

    // Setup the counters
    LogHelper.TotalFiles = 1;

    // Open the storage directory
    MakeFullPath(szFullPath, _countof(szFullPath), szStorage);
    LogHelper.PrintProgress("Opening storage ...");
    if(CascOpenStorage(szFullPath, CASC_LOCALE_ENGB, &hStorage))
    {
        // Create the local file to be extracted to
        fp = OpenExtractedFile(hStorage, "\\%s", szFileName, bIsFileDataId);

        if(bIsFileDataId)
        {
            cf.dwFileDataId = CASC_FILE_DATA_ID_FROM_STRING(szFileName);
            cf.bCanOpenByDataId = true;
        }
        else
        {
            CascStrCopy(cf.szFileName, _countof(cf.szFileName), szFileName);
            cf.bCanOpenByName = true;
        }

        // Extract the entire file
        ExtractFile(LogHelper, hStorage, cf, dwOpenFlags, fp);

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

    if(fp != NULL)
        fclose(fp);
    return nError;
}

static int TestOpenStorage_EnumFiles(LPCSTR szStorage, LPCTSTR szListFile = NULL)
{
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    TCHAR szFullPath[MAX_PATH];
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    MakeFullPath(szFullPath, _countof(szFullPath), szStorage);
    LogHelper.PrintProgress("Opening storage ...");
    LogHelper.SetStartTime();
    if(CascOpenStorage(szFullPath, 0, &hStorage))
    {
        nError = TestStorage_EnumFiles(LogHelper, hStorage, szListFile);
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", szStorage);
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    return nError;
}

static int TestOpenStorage_ExtractFiles(LPCSTR szStorage, LPCSTR szExpectedNameHash, LPCSTR szExpectedDataHash, LPCTSTR szListFile = NULL)
{
    CASC_FIND_DATA cf;
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    HANDLE hFind;
    FILE * fp = NULL;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwTotalFileCount = 0;
    DWORD dwFileCount = 0;
    LPCSTR szNameHash;
    LPCSTR szDataHash;
    char szTotalBytes[0x20];
    bool bFileFound = true;
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    MakeFullPath(szFullPath, _countof(szFullPath), szStorage);
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
//          fp = fopen("E:\\extract-diablo3-002.txt", "wt");

            // Search the storage
            while(bFileFound)
            {
                // Add the file name to the name hash
                LogHelper.HashName(cf.szFileName);

                // Extract the file if available locally
                if(cf.bFileAvailable)
                {
                    ExtractFile(LogHelper, hStorage, cf, 0, fp);
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

static int TestOnlineStorage_EnumFiles(LPCSTR szCodeName, LPCSTR szRegion, LPCSTR /* szFileName */)
{
    TLogHelper LogHelper(szCodeName);
    HANDLE hStorage;
    TCHAR szLocalCache[MAX_PATH];
    TCHAR szPathRootT[0x20];
    int nError = ERROR_SUCCESS;

    // Open the storage directory
    LogHelper.PrintProgress("Opening storage ...");
    LogHelper.SetStartTime();
    
    // We need the code name to be LPTSTR
    CascStrCopy(szPathRootT, _countof(szPathRootT), CASC_WORK_ROOT);
    CombineFilePath(szLocalCache, _countof(szLocalCache), szPathRootT, NULL);

    // Open te online storage
    if (CascOpenOnlineStorage(szLocalCache, szCodeName, szRegion, 0, &hStorage))
    {
        nError = TestStorage_EnumFiles(LogHelper, hStorage, NULL);
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", szCodeName);
        assert(GetLastError() != ERROR_SUCCESS);
        nError = GetLastError();
    }

    return nError;
}

//-----------------------------------------------------------------------------
// Storage list

static STORAGE_INFO1 StorageInfo1[] =
{
    //- Name of the storage folder -------- Compound file name hash ----------- Compound file data hash ----------- Example file to extract -----------------------------------------------------------
    //{"2014 - Heroes of the Storm/29049", "98396c1a521e5dee511d835b9e8086c7", "8febac8275e204800e5a4da0259e91c9", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    //{"2014 - Heroes of the Storm/30027", "6bcbe7c889cc465e4993f92d6ae1ee75", "54ed1440368de80723eddd89931fe191", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    //{"2014 - Heroes of the Storm/30414", "4b5d1f21de95c2a448684f98cc157f10", "ff32ed33bfcb40e01bf75c8df381eca5", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    //{"2014 - Heroes of the Storm/31726", "8b7633e519b78c96c85a1faa1c9f151f", "a0fd31d04f1bd6c5b3532c72592abf19", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    //{"2014 - Heroes of the Storm/39445", "c672b26f8f14ab2e68a9f9d7d6ca6062", "5ab7d596b5d6025072d7f331b3d7167a", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    //{"2014 - Heroes of the Storm/50286", "d1d57e83cbd72cbecd76916c22f6c4b6", "572598a728ac46dd18278636394c4fbc", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    //{"2014 - Heroes of the Storm/65943", "c5d75f4e12dbc05d4560fe61c4b88773", "981b882e090bdc027910ba70744c0e2c", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    //{"2015 - Diablo III/30013",          "86ba76b46c88eb7c6188d28a27d00f49", "b642f0dd232c591f05e6bdd65e28da82", "ENCODING"},
    //{"2015 - Diablo III/50649",          "0889067a005b92186fc0df0553845106", "84f4d3c1815afd69fc7edd8fb403815d", "ENCODING"},
  
    //{"2015 - Overwatch/24919/data/casc", "53afa15570c29bd40bba4707b607657e", "117073f6e207e8cdcf43b705b80bf120", "ROOT"},
    //{"2015 - Overwatch/47161",           "53db1f3da005211204997a6b50aa71e1", "434d7ff16fe0d283a2dacfc1390cb16e", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

    //{"2016 - Starcraft II/45364/\\/",    "28f8b15b5bbd87c16796246eac3f800c", "4f5d1cd5453557ef7e10d35975df2b12", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    //{"2016 - WoW/18125",                 "b31531af094f78f58592249c4d216a8e", "5606e21ce4b493ad1c6ce189818245ae", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/18379",                 "6beec00c8a16f873b4297a2262e60a82", "1a86f24bfb1076bcbec9a4c9a32b4aeb", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/18865",                 "2f97a490a7c321dda7947f8c6cd7aa78", "d705c852dbbbc1a7e7ebfd43a1a041f1", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/18888",                 "44586978f5ee214ccfef03971435e164", "b24db02e56fc65ee07646658de5698c4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/19116",                 "a3be9cfd4a15ba184e21eed9ec90417b", "0b68a11d1eae6645f18a453d39aba23a", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/19342",                 "66f0de0cff477e1d8e982683771f1ada", "8e5f45f6892fc6e7a6c90a8544a9383b", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/21742",                 "a357c3cbed98e83ac5cd394ceabc01e8", "b5917c7b388d8d9c0f65f97c1738cb84", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/22267",                 "4bb1102729ddbca52cd5acb360515432", "6d629be84016d0900d05162b2e63b538", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/23420",                 "e62a798989e6db00044b079e74faa1eb", "a8a23b23fdbfe8cfe33a0905bf8bfdb4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/29981",                 "a35f7de61584644d4877aac1380ef090", "b765258b7a6f38d66ab5fc350a495be1", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/30123",                 "75fbd6340ae8caf70273880614f68c66", "39796f1c8f28923bbb8f4b4ef56cd8fc", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},

    //{"2017 - Starcraft1/2457",           "3eabb81825735cf66c0fc10990f423fa", "2ed3292de2285f7cf1f9c889a318b240", "music\\radiofreezerg.ogg"},
    //{"2017 - Starcraft1/4037",           "bb2b76d657a841953fe093b75c2bdaf6", "5bf1dc985f0957d3ba92ed9c5431b31b", "music\\radiofreezerg.ogg"},
    //{"2017 - Starcraft1/4261",           "59ea96addacccb73938fdf688d7aa29b", "4bade608b78b186a90339aa557ad3332", "music\\radiofreezerg.ogg"},

    //{"2018 - New CASC/00001",            "43d576ee81841a63f2211d43a50bb593", "2b7829b59c0b6e7ca6f6111bfb0dc426", "ROOT"},
    //{"2018 - New CASC/00002",            "1c76139b51edd3ee114b5225d1b44c86", "4289e1e095dbfaec5dd926b5f9f22c6f", "ENCODING"},

    //{"2018 - Warcraft III/09655",        "b1aeb7180848b83a7a3132cba608b254", "5d0e71a47f0b550de6884cfbbe3f50e5", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    //{"2018 - Warcraft III/11889",        "f084ee1713153d8a15f1f75e94719aa8", "3541073dd77d370a01fbbcadd029477e", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },

    {NULL}
};

static STORAGE_INFO2 StorageInfo2[] =
{
//  {"agent",    "us"},
//  {"bna",      "us"},
//  {"catalogs", NULL},
//  {"clnt",     "us"},
    {"hsb",      "us"},
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

//  TestOpenStorage_EnumFiles("2014 - Heroes of the Storm\\29049");
//  TestOpenStorage_EnumFiles("2015 - Diablo III\\30013");
//  TestOpenStorage_EnumFiles("2016 - WoW\\18125");
//  TestOpenStorage_EnumFiles("2018 - New CASC\\00001");
//  TestOpenStorage_EnumFiles("2018 - New CASC\\00002");
//  TestOpenStorage_EnumFiles("2018 - Warcraft III\\11889");
//  TestOpenStorage_EnumFiles("2019 - WoW Classic/30406");

    // "dbfilesclient\\battlepetspeciesstate.db2"
    TestOpenStorage_OpenFile("d:\\Hry\\World of Warcraft Public Test\\Data", CASC_FILE_DATA_ID(801581), CASC_OVERCOME_ENCRYPTED, true);
//  TestOnlineStorage_EnumFiles("agent", NULL, NULL);                                   



    //
    // Run the tests for every local storage in my collection
    //
    for(size_t i = 0; StorageInfo1[i].szPath != NULL; i++)
    {
        // Attempt to open the storage and extract single file
        nError = TestOpenStorage_ExtractFiles(StorageInfo1[i].szPath, StorageInfo1[i].szNameHash, StorageInfo1[i].szDataHash, NULL);
//      nError = TestOpenStorage_EnumFiles(StorageInfo1[i].szPath);
        if(nError != ERROR_SUCCESS)
            break;
    }

    //
    // Run the tests for every available online storage in my collection
    //
    for (size_t i = 0; StorageInfo2[i].szCodeName != NULL; i++)
    {
        // Attempt to open the storage and extract single file
//      nError = TestOnlineStorage_EnumFiles(StorageInfo2[i].szCodeName, StorageInfo2[i].szRegion, StorageInfo2[i].szFile);
        if (nError != ERROR_SUCCESS)
            break;
    }

#ifdef _MSC_VER
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return nError;
}
