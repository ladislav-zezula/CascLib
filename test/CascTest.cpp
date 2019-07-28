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

#define SHORT_NAME_SIZE 59

//-----------------------------------------------------------------------------
// Local structures

// For local storages
typedef struct _STORAGE_INFO1
{
    LPCSTR szPath;                          // Path to the CASC storage
    LPCSTR szNameHash;                      // MD5 of all file names extracted sequentially
    LPCSTR szDataHash;                      // MD5 of all file data extracted sequentially
    LPCSTR szFileName;                      // Example file in the storage
} STORAGE_INFO1, *PSTORAGE_INFO1;

// For online storages
typedef struct _STORAGE_INFO2
{
    LPCSTR szCodeName;                      // Codename of the product
    LPCSTR szRegion;                        // Region of the product. If NULL, CascLib will open the first one in the "versions"
    LPCSTR szFile;                          // Example file in the storage
} STORAGE_INFO2, *PSTORAGE_INFO2;

// For running tests on an open storage
struct TEST_PARAMS
{
    TEST_PARAMS()
    {
        memset(this, 0, sizeof(TEST_PARAMS));
        dwFileDataId = CASC_INVALID_ID;
    }

    ~TEST_PARAMS()
    {
        if(hStorage != NULL)
            CascCloseStorage(hStorage);
        hStorage = NULL;

        if(fp1 != NULL)
            fclose(fp1);
        fp1 = NULL;

        if(fp2 != NULL)
            fclose(fp2);
        fp2 = NULL;
    }

    HANDLE hStorage;                // Opened storage handle
    FILE * fp1;                     // Opened stream for writing list of file names
    FILE * fp2;                     // Opened stream for writing a content of a file
    LPCTSTR szListFile;
    LPCSTR szExpectedNameHash;
    LPCSTR szExpectedDataHash;
    LPCSTR szFileName;
    DWORD dwFileDataId;
    DWORD dwOpenFlags;
    DWORD bOnlineStorage:1;
    DWORD bExtractFiles:1;
    DWORD bHashData:1;
};

typedef DWORD (*PFN_RUN_TEST)(TLogHelper & LogHelper, TEST_PARAMS & Params);

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
        LPCSTR szPlainName = GetPlainFileName(cf.szFileName);
        size_t nFirstPart = (ccShortName / 3);
        size_t nRemaining;

        // Try to place the short name before the plain name
        if((szPlainName > cf.szFileName) && (szPlainName - cf.szFileName) > 5)
        {
            if(nFirstPart > (size_t)((szPlainName - cf.szFileName) - 5))
                nFirstPart = (szPlainName - cf.szFileName) - 5;
        }

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

    // Does it look like a DOS path?
    if(isalpha(szStorage[0]) && szStorage[1] == ':' && szStorage[2] == '\\')
    {
        return CopyPath(szBuffer, szBufferEnd, szStorage);
    }

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
        CascStrPrintf(szOutFileName, _countof(szOutFileName), szFormat, ProductInfo.szCodeName, ProductInfo.BuildNumber);
        fp = fopen(szOutFileName, "wt");
    }

    return fp;
}

static FILE * OpenExtractedFile(HANDLE /* hStorage */, LPCSTR szFormat, CASC_FIND_DATA & cf)
{
    char szOutFileName[MAX_PATH];

    CascStrPrintf(szOutFileName, _countof(szOutFileName), szFormat, GetPlainFileName(cf.szFileName));
    return fopen(szOutFileName, "wb");
}

static void TestStorageGetTagInfo(HANDLE hStorage)
{
    PCASC_STORAGE_TAGS pTags = NULL;
    size_t cbTags = 0;

    CascGetStorageInfo(hStorage, CascStorageTags, pTags, cbTags, &cbTags);
    if(cbTags != 0)
    {
        pTags = (PCASC_STORAGE_TAGS)CASC_ALLOC<BYTE>(cbTags);
        if(pTags != NULL)
        {
            CascGetStorageInfo(hStorage, CascStorageTags, pTags, cbTags, &cbTags);
            CASC_FREE(pTags);
        }
    }
}

static void TestStorageGetName(HANDLE hStorage)
{
    TCHAR szStorageParams[MAX_PATH];
    size_t nLength = 0;

    CascGetStorageInfo(hStorage, CascStoragePathProduct, szStorageParams, sizeof(szStorageParams), &nLength);
}

static PCASC_FILE_SPAN_INFO GetFileInfo(HANDLE hFile, CASC_FILE_FULL_INFO & FileInfo)
{
    PCASC_FILE_SPAN_INFO pSpans = NULL;

    // Retrieve the full file info
    if(CascGetFileInfo(hFile, CascFileFullInfo, &FileInfo, sizeof(CASC_FILE_FULL_INFO), NULL))
    {
        if((pSpans = CASC_ALLOC<CASC_FILE_SPAN_INFO>(FileInfo.SpanCount)) != NULL)
        {
            if(!CascGetFileInfo(hFile, CascFileSpanInfo, pSpans, FileInfo.SpanCount * sizeof(CASC_FILE_SPAN_INFO), NULL))
            {
                CASC_FREE(pSpans);
                pSpans = NULL;
            }
        }
    }

    return pSpans;
}

static DWORD CheckFileDataHash(MD5_CTX & md5_ctx, PCASC_FILE_SPAN_INFO pSpan, LPBYTE Buffer, ULONGLONG FileOffset, DWORD dwBytesRead, DWORD & dwSpanIndex)
{
    ULONGLONG StartOffset = FileOffset;
    ULONGLONG EndOffset = FileOffset + dwBytesRead;
    DWORD dwBytesToHash;
    BYTE ContentKey[MD5_HASH_SIZE];
    DWORD dwErrCode = ERROR_SUCCESS;

    // Is there an end of span in this block?
    if(StartOffset < pSpan->EndOffset && pSpan->EndOffset <= EndOffset)
    {
        // Finalize the hash
        dwBytesToHash = (DWORD)(pSpan->EndOffset - StartOffset);
        MD5_Update(&md5_ctx, Buffer, dwBytesRead);
        MD5_Final(ContentKey, &md5_ctx);
        Buffer += dwBytesToHash;

        // Compare the hash
        if(memcmp(ContentKey, pSpan->CKey, MD5_HASH_SIZE))
            dwErrCode = ERROR_FILE_CORRUPT;

        // Reset the hash generator
        dwBytesToHash = (DWORD)(EndOffset - pSpan->EndOffset);
        MD5_Init(&md5_ctx);
        MD5_Update(&md5_ctx, Buffer, dwBytesToHash);
        dwSpanIndex++;
    }
    else
    {
        // Update the hash
        MD5_Update(&md5_ctx, Buffer, dwBytesRead);
    }

    return dwErrCode;
}


static DWORD ExtractFile(
    TLogHelper & LogHelper,
    TEST_PARAMS & Params,
    CASC_FIND_DATA & cf)
{
    PCASC_FILE_SPAN_INFO pSpans;
    CASC_FILE_FULL_INFO FileInfo;
    LPCSTR szOpenName = cf.szFileName;
    HANDLE hFile = NULL;
    DWORD dwErrCode = ERROR_SUCCESS;
    char szShortName[SHORT_NAME_SIZE];
    bool bHashFileContent = true;
    bool bReadOk = true;

    // Show the file name to the user if open succeeded
    MakeShortName(szShortName, sizeof(szShortName), cf);

    //if(!_stricmp(cf.szPlainName, "84fd9825f313363fd2528cd999bcc852"))
    //    __debugbreak();

    // Open the CASC file
    if(CascOpenFile(Params.hStorage, szOpenName, 0, Params.dwOpenFlags | CASC_STRICT_DATA_CHECK, &hFile))
    {
        // Retrieve the information about file spans
        if((pSpans = GetFileInfo(hFile, FileInfo)) != NULL)
        {
            ULONGLONG FileSize = FileInfo.ContentSize;
            ULONGLONG TotalRead = 0;
            DWORD dwBytesRead = 0;

            // Show the progress, if open succeeded
            if(!LogHelper.ProgressCooldown())
                LogHelper.PrintProgress("Extracting: (%u of %u) %s ...", LogHelper.FileCount, LogHelper.TotalFiles, szShortName);

            // Load the entire file, one read request per span.
            // Using span-aligned reads will cause CascReadFile not to do any caching,
            // and the amount of memcpys will be almost zero
            for(DWORD i = 0; i < FileInfo.SpanCount && dwErrCode == ERROR_SUCCESS; i++)
            {
                PCASC_FILE_SPAN_INFO pFileSpan = pSpans + i;
                LPBYTE pbFileSpan;
                DWORD cbFileSpan = (DWORD)(pFileSpan->EndOffset - pFileSpan->StartOffset);

                // Do not read empty spans.
                // Storage: "2017 - Starcraft1/2457"
                // Example: "locales/itIT/Assets/SD/campaign/Starcraft/SWAR/staredit/scenario.chk"
                if(cbFileSpan == 0)
                    continue;

                // Allocate span buffer
                pbFileSpan = CASC_ALLOC<BYTE>(cbFileSpan);
                if(pbFileSpan == NULL)
                {
                    LogHelper.PrintProgress("Error: Not enough memory to allocate %u bytes", cbFileSpan);
                    break;
                }

                // Show the progress, if open succeeded
                if(!LogHelper.ProgressCooldown())
                    LogHelper.PrintProgress("Extracting: (%u of %u) %s (%u%%) ...", LogHelper.FileCount, LogHelper.TotalFiles, szShortName, (DWORD)((TotalRead * 100) / FileInfo.ContentSize));

                // CascReadFile will read as much as possible. If there is a frame error
                // (e.g. MD5 mismatch, missing encryption key or disc error),
                // CascReadFile only returns frames that are loaded entirely.
                bReadOk = CascReadFile(hFile, pbFileSpan, cbFileSpan, &dwBytesRead);
                if(bReadOk)
                {
                    // If required, write the file data to the output file
                    if(Params.fp2 != NULL)
                        fwrite(pbFileSpan, 1, dwBytesRead, Params.fp2);

                    // Per-file hashing. Don't do it if there is no CKey
                    // Skip it if we loaded less data than required (MD5 will be always mismatch)
                    if(bHashFileContent && dwBytesRead == cbFileSpan && CascIsValidMD5(pSpans->CKey))
                    {
                        MD5_CTX md5_ctx;
                        BYTE ContentKey[MD5_HASH_SIZE];

                        MD5_Init(&md5_ctx);
                        MD5_Update(&md5_ctx, pbFileSpan, dwBytesRead);
                        MD5_Final(ContentKey, &md5_ctx);

                        if(memcmp(pFileSpan->CKey, ContentKey, MD5_HASH_SIZE))
                        {
                            LogHelper.PrintMessage("Warning: %s: MD5 mismatch", szShortName);
                        }
                    }

                    // Per-storage hashing
                    if(LogHelper.HasherReady)
                        LogHelper.HashData(pbFileSpan, dwBytesRead);
                    TotalRead += dwBytesRead;

                    // If we read less than expected, we report read error
                    bReadOk = (dwBytesRead == cbFileSpan);
                }

                // Was there an error reading data?
                if(bReadOk == false)
                {
                    // Do not report some errors; for example, when the file is encrypted,
                    // we can't do much about it. Only report it if we are going to extract one file
                    switch(dwErrCode = GetLastError())
                    {
                        case ERROR_SUCCESS:
                            break;

                        case ERROR_FILE_ENCRYPTED:
                            if(LogHelper.TotalFiles == 1)
                                LogHelper.PrintMessage("Warning: %s: File is encrypted", szShortName);
                            break;

                        default:
                            LogHelper.PrintMessage("Warning: %s: Read error (offset %08X:%08X)", szShortName, (DWORD)(TotalRead >> 32), (DWORD)(TotalRead));
                            break;
                    }
                }

                // Free the memory occupied by the file span
                CASC_FREE(pbFileSpan);
            }

            // Check whether the total size matches
            if(dwErrCode == ERROR_SUCCESS && TotalRead != FileSize)
            {
                LogHelper.PrintMessage("Warning: %s: TotalRead != FileSize", szShortName);
                dwErrCode = ERROR_FILE_CORRUPT;
            }

            // Increment the total number of files
            LogHelper.TotalBytes = LogHelper.TotalBytes + TotalRead;

            // Free the span array
            CASC_FREE(pSpans);
        }

        LogHelper.FileCount++;

        // Close the handle
        CascCloseFile(hFile);
    }
    else
    {
        LogHelper.PrintError("Warning: %s: Open error", szShortName);
        assert(GetLastError() != ERROR_SUCCESS);
        dwErrCode = GetLastError();
    }

    return dwErrCode;
}

//-----------------------------------------------------------------------------
// Testing functions

static DWORD Storage_OpenFiles(TLogHelper & LogHelper, TEST_PARAMS & Params)
{
    CASC_FIND_DATA cf = {0};
    DWORD dwErrCode = ERROR_SUCCESS;

    // Setup the name structure
    CascStrCopy(cf.szFileName, _countof(cf.szFileName), Params.szFileName);
    cf.szPlainName = (char *)GetPlainFileName(cf.szFileName);

    // Setup the file to extract
    //Params.fp2 = OpenExtractedFile(Params.hStorage, "\\%s", cf);

    // Perform the extraction
    ExtractFile(LogHelper, Params, cf);
    LogHelper.PrintTotalTime();

    // Close the output file
    if(Params.fp2 != NULL)
        fclose(Params.fp2);
    Params.fp2 = NULL;

    return dwErrCode;
}

static DWORD Storage_SeekFiles(TLogHelper & LogHelper, TEST_PARAMS & Params)
{
    TFileStream * pStream;
    ULONGLONG TotalRead = 0;
    ULONGLONG FileSize = 0;
    HANDLE hFile;
    TCHAR szPlainName[MAX_PATH];
    DWORD dwBytesRead;
    DWORD dwErrCode = ERROR_SUCCESS;
    BYTE Buffer[0x1000];

    // Check whether the file name was given
    if(Params.szFileName == NULL)
        return ERROR_INVALID_PARAMETER;

    // Setup the name structure
    CascStrCopy(szPlainName, _countof(szPlainName), GetPlainFileName(Params.szFileName));

    // Extract the file to a local file
    if((pStream = FileStream_OpenFile(szPlainName, 0)) == NULL)
        pStream = FileStream_CreateFile(szPlainName, 0);
    if(pStream  != NULL)
    {
        if(CascOpenFile(Params.hStorage, Params.szFileName, 0, Params.dwOpenFlags, &hFile))
        {
            //
            // Phase 1: Create local copy of the file
            //

            LogHelper.PrintProgress("Extracting file ...");
            CascGetFileSize64(hFile, &FileSize);

            while(TotalRead < FileSize)
            {
                if(!LogHelper.ProgressCooldown())
                    LogHelper.PrintProgress("Extracting file (%u %%) ...", (DWORD)((TotalRead * 100) / FileSize));

                // Get the amount of bytes to read
                DWORD dwBytesToRead = sizeof(Buffer);
                if((TotalRead + dwBytesToRead) > FileSize)
                    dwBytesToRead = (DWORD)(FileSize - TotalRead);

                // Read the chunk
                CascReadFile(hFile, Buffer, dwBytesToRead, &dwBytesRead);
                if(dwBytesRead != dwBytesToRead)
                {
                    LogHelper.PrintMessage("Error: Failed to read %u bytes at offset %llX.", dwBytesToRead, TotalRead);
                    dwErrCode = GetLastError();
                    break;
                }

                // Write to the target file
                if(!FileStream_Write(pStream, &TotalRead, Buffer, dwBytesRead))
                {
                    LogHelper.PrintMessage("Error: Failed to write %u bytes at offset %llX.", dwBytesToRead, TotalRead);
                    dwErrCode = GetLastError();
                    break;
                }

                TotalRead += dwBytesRead;
            }

            //
            // Phase 2: Compare the loaded data from the random positions in the file
            //

            if(dwErrCode == ERROR_SUCCESS)
            {
                // Always set random number generator to the same value
                srand(0x12345678);

                // Perform several random offset reads and compare data
                for(DWORD i = 0; i < 0x1000; i++)
                {
                    ULONGLONG ByteOffset;
                    ULONGLONG RandomHi = rand();
                    DWORD RandomLo = rand();
                    DWORD Length = rand() % sizeof(Buffer);
                    BYTE Buffer2[0x1000];

                    if(!LogHelper.ProgressCooldown())
                        LogHelper.PrintProgress("Testing seek operations (%u of %u) ...", i, 0x1000);

                    // Determine offset and length
                    ByteOffset = ((RandomHi << 0x20) | RandomLo) % FileSize;
                    if((ByteOffset + Length) > FileSize)
                        ByteOffset = FileSize - Length;

                    // Load the data from CASC file
                    CascSetFilePointer64(hFile, ByteOffset, NULL, FILE_BEGIN);
                    CascReadFile(hFile, Buffer, Length, &dwBytesRead);
                    if(dwBytesRead != Length)
                    {
                        LogHelper.PrintMessage("Error: Failed to read %u bytes from CASC file (offset %llX).", Length, ByteOffset);
                        dwErrCode = GetLastError();
                        break;
                    }

                    // Load data from the local file
                    if(!FileStream_Read(pStream, &ByteOffset, Buffer2, Length))
                    {
                        LogHelper.PrintMessage("Error: Failed to read %u bytes from LOCAL file (offset %llX).", Length, ByteOffset);
                        dwErrCode = GetLastError();
                        break;
                    }

                    // Compare the loaded data blocks
                    if(memcmp(Buffer, Buffer2, Length))
                    {
                        LogHelper.PrintMessage("Error: Data mismatchat offset %llX, length %u.", ByteOffset, Length);
                        dwErrCode = GetLastError();
                        break;
                    }
                }
            }

            // Close the file handle
            CascCloseFile(hFile);
        }
        FileStream_Close(pStream);
        //_tunlink(szPlainName);
    }

    // Perform the extraction
    LogHelper.PrintTotalTime();
    return dwErrCode;
}

static DWORD Storage_EnumFiles(TLogHelper & LogHelper, TEST_PARAMS & Params)
{
    CASC_FIND_DATA cf;
    LPCTSTR szListFile = GetTheProperListfile(Params.hStorage, Params.szListFile);
    LPCSTR szActivity = (Params.bExtractFiles) ? "Extracting" : "Enumerating";
    LPCSTR szNameHash;
    LPCSTR szDataHash;
    HANDLE hStorage = Params.hStorage;
    HANDLE hFind;
    DWORD dwTotalFileCount = 0;
    DWORD dwFileCount = 0;
    DWORD dwErrCode = ERROR_SUCCESS;
    char szShortName[SHORT_NAME_SIZE];
    char szTotalBytes[0x20];
    bool bFileFound = true;

    // Create the output file for dumping all file names
//  Params.fp1 = OpenOutputTextFile(hStorage, "\\list-%s-%u-002.txt");

        // Dump the storage
//  LogHelper.PrintProgress("Dumping storage ...");
//  CascDumpStorage(hStorage, "E:\\storage-dump.txt");

    // Retrieve the total number of files
    CascGetStorageInfo(hStorage, CascStorageTotalFileCount, &dwTotalFileCount, sizeof(dwTotalFileCount), NULL);
    LogHelper.TotalFiles = dwTotalFileCount;

    // Retrieve the tags
    TestStorageGetTagInfo(hStorage);
    TestStorageGetName(hStorage);

    // Init the hashers
    LogHelper.InitHashers();

    // Start finding
    LogHelper.PrintProgress("Searching storage ...");
    hFind = CascFindFirstFile(hStorage, "*", &cf, szListFile);
    if (hFind != NULL)
    {
        while (bFileFound)
        {
            // Add the file name to the name hash
            if(Params.szExpectedNameHash != NULL)
                LogHelper.HashName(cf.szFileName);
            LogHelper.FileCount = dwFileCount;

            // There should always be a name
            if (Params.fp1 != NULL)
                fprintf(Params.fp1, "%s\n", cf.szFileName);
            assert(cf.szFileName[0] != 0);

            // Show the file name to the user
            if(!LogHelper.ProgressCooldown())
            {
                MakeShortName(szShortName, sizeof(szShortName), cf);
                LogHelper.PrintProgress("%s: (%u of %u) %s ...", szActivity, dwFileCount, dwTotalFileCount, szShortName);
            }

            // Extract the file if available locally
            if(Params.bExtractFiles && (Params.bOnlineStorage || cf.bFileAvailable))
            {
                ExtractFile(LogHelper, Params, cf);
            }

            // Find the next file in CASC
            bFileFound = CascFindNextFile(hFind, &cf);
            dwFileCount++;
        }

        // The file counts should match
        LogHelper.FormatTotalBytes(szTotalBytes, _countof(szTotalBytes));
        assert(dwFileCount == dwTotalFileCount);

        // Show the summary
        if(Params.bExtractFiles)
            LogHelper.PrintMessage("Extracted: %u of %u files (%s bytes total)", LogHelper.FileCount, LogHelper.TotalFiles, szTotalBytes);
        if((szNameHash = LogHelper.GetNameHash()) != NULL)
            LogHelper.PrintMessage("Name Hash: %s%s", szNameHash, GetHashResult(Params.szExpectedNameHash, szNameHash));
        if((szDataHash = LogHelper.GetDataHash()) != NULL)
            LogHelper.PrintMessage("Data Hash: %s%s", szDataHash, GetHashResult(Params.szExpectedDataHash, szDataHash));
        LogHelper.PrintTotalTime();

        // Close the search handle
        CascFindClose(hFind);
    }
    else
    {
        LogHelper.PrintMessage("Error: Failed to enumerate the storage.");
        dwErrCode = GetLastError();
    }

    if(Params.fp1)
        fclose(Params.fp1);
    return dwErrCode;
}

static DWORD Storage_ReadFiles(TLogHelper & LogHelper, TEST_PARAMS & Params)
{
    Params.bHashData = true;
    Params.bExtractFiles = true;
    return Storage_EnumFiles(LogHelper, Params);
}

static DWORD LocalStorage_Test(PFN_RUN_TEST PfnRunTest, LPCSTR szStorage, LPCSTR szExpectedNameHash = NULL, LPCSTR szExpectedDataHash = NULL, LPCSTR szFileName = NULL)
{
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwErrCode = ERROR_SUCCESS;

    // Prepare the full path of the storage
    MakeFullPath(szFullPath, _countof(szFullPath), szStorage);

    // Open the CASC storage
    LogHelper.PrintProgress("Opening storage ...");
    if(CascOpenStorage(szFullPath, 0, &hStorage))
    {
        TEST_PARAMS Params;

        // Configure the test parameters
        Params.hStorage = hStorage;
        Params.szExpectedNameHash = szExpectedNameHash;
        Params.szExpectedDataHash = szExpectedDataHash;
        Params.szFileName = szFileName;
        dwErrCode = PfnRunTest(LogHelper, Params);
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", szStorage);
        assert(GetLastError() != ERROR_SUCCESS);
        dwErrCode = GetLastError();
    }

    return dwErrCode;
}

static DWORD OnlineStorage_Test(PFN_RUN_TEST PfnRunTest, LPCSTR szCodeName, LPCSTR szRegion = NULL, LPCSTR szFileName = NULL)
{
    TLogHelper LogHelper(szCodeName);
    HANDLE hStorage;
    TCHAR szParamsT[MAX_PATH];
    char szParamsA[MAX_PATH];
    size_t nLength;
    DWORD dwErrCode = ERROR_SUCCESS;

    // Prepare the path and region
    nLength = CascStrPrintf(szParamsA, _countof(szParamsA), "%s/%s:%s", CASC_WORK_ROOT, szCodeName, szCodeName);
    if(szRegion  && szRegion[0])
    {
        szParamsA[nLength++] = ':';
        CascStrCopy(&szParamsA[nLength], _countof(szParamsA) - nLength, szRegion);
    }

    CascStrCopy(szParamsT, _countof(szParamsT), szParamsA);

    // Open te online storage
    LogHelper.PrintProgress("Opening storage ...");
    if (CascOpenOnlineStorage(szParamsT, 0, &hStorage))
    {
        TEST_PARAMS Params;

        // Configure the test parameters
        Params.hStorage = hStorage;
        Params.szFileName = szFileName;
        Params.bOnlineStorage = true;
        dwErrCode = PfnRunTest(LogHelper, Params);
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", szCodeName);
        assert(GetLastError() != ERROR_SUCCESS);
        dwErrCode = GetLastError();
    }

    return dwErrCode;
}

//-----------------------------------------------------------------------------
// Storage list

static STORAGE_INFO1 StorageInfo1[] =
{
    //- Name of the storage folder -------- Compound file name hash ----------- Compound file data hash ----------- Example file to extract -----------------------------------------------------------
    {"2014 - Heroes of the Storm/29049", "12cda9bb481920355b115b94fbb15790", "12c19db7dc16b277e9de556876fa5d10", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    {"2014 - Heroes of the Storm/30027", "e8c6b0f329696fde2fb9a74c73e81646", "b094e10ef0cdbd574e21ffcd75cf68b5", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    {"2014 - Heroes of the Storm/30414", "f4278ae79295b0129e853b8f929199e8", "663ded34b197b8c8beac62ce55ecdb7a", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    {"2014 - Heroes of the Storm/31726", "38fd0452e82a8e56e13d7342b83aa63e", "1ab6b48be76ff23e0d6dc6ed88a53254", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"2014 - Heroes of the Storm/39445", "68f347adf40b8f11678829cf36aa7292", "5d4293f408075e83ba67aae8e2b40376", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"2014 - Heroes of the Storm/50286", "ac137cfc6bc956d0a7424f61547c5832", "4b355063c05eb7be603f2045c699d77d", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"2014 - Heroes of the Storm/65943", "6ca09ac7728eb1add2b97ccbb4c957ce", "4f25fb403217ba8c862f92c0209280a6", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    {"2015 - Diablo III/30013",          "949a2176b3b6a2efab72cb3410f01ba1", "59115a74a07373cf15e0b8ff2f83e941", "ENCODING"},
    {"2015 - Diablo III/50649",          "13c30e03063b3ba5b460e3b3a8a78724", "ae604da9908b8daf14ebd918e03c5f47", "ENCODING"},

    {"2015 - Overwatch/24919/data/casc", "224547726def07712076d2e19182749b", "cc985e483d5689411ba5381f6824dbe6", "ROOT"},
    {"2015 - Overwatch/47161",           "42f7d8a33c88a597b77cf4a1e6775dec", "5e14fa8aeecdf58324666ac454cb39c6", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

    {"2016 - Starcraft II/45364/\\/",    "3e1cb9ac46a1e07a05bc1acde7b9e7cc", "165aa18e98335ea23780c59c7450dbbb", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    {"2016 - WoW/18125",                 "e5541b24851b2b4c23f7ca8203fadeda", "e1db30550b2045ff60fee3e3e835a450", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18379",                 "b2956885147f5aef2243d95010ab257e", "99d896bd5dbb37e933666e69ff472cfa", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18865",                 "d1f9e440740e349d691abad752751c8e", "9e49b80fff417d4f2ab2083f7d3b1ca5", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18888",                 "e37f70b072733264d86bd3cf33d9fb39", "6b19c9ad015c0665b6a804ad565e1729", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19116",                 "bc3191c39c8d7a48ed2e24864f06a3cc", "250f1ede9ec490f76870e34e8251f68a", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19342",                 "8f40fa781b3b8e4f0d77477c6fe1ccf2", "9b03ebb59813aaee69760d39b28cc1b9", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/21742",                 "87a36476f68dc44c58bbee358b7b3b58", "fce853f6284a909a78bbb56796130a24", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/22267",                 "f2b8e0d5136c72c5067769a4525f5a1a", "8d80b5a66ea13e48b1507f7e2409604d", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/23420",                 "834ac651129b39bfd6fb3d65519bb2d2", "f4febb39913f59e1c0b359bd0201bf85", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/29981",                 "0de024afd819724712e1a9d6d4318467", "e3c5c6f51d1bd74b8f09f5469aaf1290", "dbfilesclient\\battlepetspeciesstate.db2"},
    {"2016 - WoW/30993:wow",             "bf63fb5ee13e9d323fdca3ac9a42c635", "187528bedd080ba049dd6bbeaa9444d0", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/30993:wowt",            "aab4e18cccb413a8ac10cc68daa1e938", "5165e632c69e1d68371346d63c719de8", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
//  {"2016 - WoW/30993:wow_classic",     "ff9377439f91605a17117bf8c89815b4", "3010a70f89e6fd96cac23fa7082b6d3d", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},

    {"2017 - Starcraft1/2457",           "e49f1880a14e3197d3bc05aea3befb12", "1ef032898743e6772108bf4a85974915", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4037",           "9536c1c74703c117496189c507c8758c", "ce1681e86a487e183101a1271558f687", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4261",           "64a95b66ab75c9d75bbbd1121324e2f7", "789329f08a12ea227fa6b3512b593820", "music\\radiofreezerg.ogg"},

    {"2018 - New CASC/00001",            "44833489ccf495e78d3a8f2ee9688ba6", "de800746f493e797372f9a666466e6a9", "ROOT"},
    {"2018 - New CASC/00002",            "0ada2ba6b0decfa4013e0465f577abf1", "140c72bcc49bfa2dd0c3d42fe2fc110b", "ENCODING"},

    {"2018 - Warcraft III/09655",        "f3f5470aa0ab4939fa234d3e29c3d347", "01590e3075b4fe4d89bf0dc26c3cc51c", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"2018 - Warcraft III/11889",        "ff36cd4f58aae23bd77d4a90c333bdb5", "8a95dba5dfd6109b9a76cde8c7c760a0", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },

    {"2018 - CoD4/3376209",              "e01180b36a8cfd82cb2daa862f5bbf3e", "8ec6cbaf555c84b7f877e388e674f5e0", "zone/base.xpak" },

    {NULL}
};

static STORAGE_INFO2 StorageInfo2[] =
{
//  {"agent",    "us"},
//  {"bna",      "us"},
//  {"catalogs", NULL},
//  {"clnt",     "us"},
//  {"hsb",      "us"},
    {NULL}
};

//-----------------------------------------------------------------------------
// Main

int main(void)
{
    DWORD dwErrCode = ERROR_SUCCESS;

    printf("\n");

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif  // defined(_MSC_VER) && defined(_DEBUG)

    //
    // Single tests
    //

//  LocalStorage_Test(Storage_OpenFiles, "2014 - Heroes of the Storm\\29049", NULL, NULL, "ENCODING");
//  LocalStorage_Test(Storage_ReadFiles, "2014 - Heroes of the Storm\\30414", NULL, NULL, "84fd9825f313363fd2528cd999bcc852");
//  LocalStorage_Test(Storage_EnumFiles, "2015 - Diablo III\\30013");
//  LocalStorage_Test(Storage_EnumFiles, "2016 - WoW/30993:wow", NULL, NULL, "PATCH");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - New CASC\\00001");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - New CASC\\00002");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - Warcraft III\\11889");
//  LocalStorage_Test(Storage_SeekFiles, "2018 - CoD4\\3376209", NULL, NULL, "zone/base.xpak");
    OnlineStorage_Test(Storage_OpenFiles, "agent", NULL, "PATCH");
    //OnlineStorage_Test(Storage_SeekFiles, "viper", "us", NULL, NULL, "zone/base.xpak");

    //HANDLE hFile = NULL;
    //LPBYTE Buffer;
    //if(CascOpenLocalFile(_T("e:\\Multimedia\\CASC\\Work\\viper\\data\\28\\ec\\28ec71e6c754dda3b9d3017382a886d3"), 0, &hFile))
    //{
    //    if((Buffer = CASC_ALLOC<BYTE>(0x100000)) != NULL)
    //    {
    //        CascReadFile(hFile, Buffer, 0x100000, NULL);
    //        CASC_FREE(Buffer);
    //    }
    //    CascCloseFile(hFile);
    //}

    //
    // Run the tests for every local storage in my collection
    //
    for(size_t i = 0; StorageInfo1[i].szPath != NULL; i++)
    {
        // Attempt to open the storage and extract single file
        dwErrCode = LocalStorage_Test(Storage_ReadFiles, StorageInfo1[i].szPath, StorageInfo1[i].szNameHash, StorageInfo1[i].szDataHash);
//      dwErrCode = LocalStorage_Test(Storage_EnumFiles, StorageInfo1[i].szPath, StorageInfo1[i].szNameHash);
        if(dwErrCode != ERROR_SUCCESS)
            break;
    }

    //
    // Run the tests for every available online storage in my collection
    //
    for (size_t i = 0; StorageInfo2[i].szCodeName != NULL; i++)
    {
        // Attempt to open the storage and extract single file
//      dwErrCode = OnlineStorage_Test(Storage_EnumFiles, StorageInfo2[i].szCodeName, StorageInfo2[i].szRegion, StorageInfo2[i].szFile);
        if (dwErrCode != ERROR_SUCCESS)
            break;
    }

#ifdef _MSC_VER
    _CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return (int)dwErrCode;
}
