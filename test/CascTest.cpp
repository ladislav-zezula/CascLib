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
    FILE * fp2;                     // Opened stream for writing a content of a file (ExtractFile only)
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
        CascStrPrintf(szOutFileName, _countof(szOutFileName), szFormat, ProductInfo.szProductName, ProductInfo.dwBuildNumber);
        fp = fopen(szOutFileName, "wt");
    }

    return fp;
}

static FILE * OpenExtractedFile(HANDLE /* hStorage */, LPCSTR szFormat, CASC_FIND_DATA & cf)
{
    char szOutFileName[MAX_PATH];
    char szPlainName[MAX_PATH];

    if(cf.bCanOpenByName)
    {
        CascStrPrintf(szOutFileName, _countof(szOutFileName), szFormat, GetPlainFileName(cf.szFileName));
    }
    else if(cf.bCanOpenByDataId)
    {
        CascStrPrintf(szPlainName, _countof(szPlainName), "FILE_%08u.dat", cf.dwFileDataId);
        CascStrPrintf(szOutFileName, _countof(szOutFileName), szFormat, szPlainName);
    }
    else
    {
        assert(false);
    }

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
    HANDLE hFile = NULL;
    char szShortName[SHORT_NAME_SIZE];
    DWORD dwErrCode = ERROR_SUCCESS;
    bool bHashFileContent = true;
    bool bReadOk = true;

    // Show the file name to the user if open succeeded
    MakeShortName(szShortName, sizeof(szShortName), cf);

    //if(!_stricmp(cf.szPlainName, "gl_forest1walkday01.mp3"))
    //    __debugbreak();

    // Open the CASC file
    if(CascOpenFile(Params.hStorage, cf, Params.dwOpenFlags | CASC_STRICT_DATA_CHECK, &hFile))
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

                // Do not read empty spans
                if(cbFileSpan == 0)
                {
                    assert(false);
                    continue;
                }

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
                            assert(false);
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
    cf.bCanOpenByName = true;

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

static DWORD Storage_SeekTest(TLogHelper & LogHelper, TEST_PARAMS & Params)
{
    CASC_FIND_DATA cf = {0};
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
    CascStrCopy(cf.szFileName, _countof(cf.szFileName), Params.szFileName);
    cf.bCanOpenByName = true;

    // Extract the file to a local file
    if((pStream = FileStream_OpenFile(szPlainName, 0)) == NULL)
        pStream = FileStream_CreateFile(szPlainName, 0);
    if(pStream  != NULL)
    {
        if(CascOpenFile(Params.hStorage, cf, Params.dwOpenFlags, &hFile))
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
    //Params.fp1 = OpenOutputTextFile(hStorage, "\\list-%s-%u-002.txt");

        // Dump the storage
//  LogHelper.PrintProgress("Dumping storage ...");
//  CascDumpStorage(hStorage, "E:\\storage-dump.txt");

    // Retrieve the total number of files
    CascGetStorageInfo(hStorage, CascStorageTotalFileCount, &dwTotalFileCount, sizeof(dwTotalFileCount), NULL);
    LogHelper.TotalFiles = dwTotalFileCount;

    // Retrieve the tags
    TestStorageGetTagInfo(hStorage);
    TestStorageGetName(hStorage);

    // Init the hasher
    if(Params.bHashData)
    {
        LogHelper.InitHashers();
    }

    // Start finding
    LogHelper.PrintProgress("Searching storage ...");
    hFind = CascFindFirstFile(hStorage, "*", &cf, szListFile);
    if (hFind != NULL)
    {
        while (bFileFound)
        {
            // Add the file name to the name hash
            LogHelper.HashName(cf.szFileName);
            LogHelper.FileCount = dwFileCount;

            //char szCKey[MD5_STRING_SIZE + 1];
            //char szEKey[MD5_STRING_SIZE + 1];
            //StringFromBinary(cf.CKey, MD5_HASH_SIZE, szCKey);
            //StringFromBinary(cf.EKey, MD5_HASH_SIZE, szEKey);
            //OutputDebugStringA(szCKey);
            //OutputDebugStringA(" ");
            //OutputDebugStringA(szEKey);
            //OutputDebugStringA("\n");

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

    return dwErrCode;
}

static DWORD Storage_ExtractFiles(TLogHelper & LogHelper, TEST_PARAMS & Params)
{
    Params.bHashData = true;
    Params.bExtractFiles = true;
    return Storage_EnumFiles(LogHelper, Params);
}

static DWORD LocalStorage_Test(PFN_RUN_TEST PfnRunTest, LPCSTR szStorage, LPCSTR szExpectedNameHash = NULL, LPCSTR szExpectedDataHash = NULL)
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
        Params.szExpectedNameHash = (PfnRunTest != Storage_OpenFiles) ? szExpectedNameHash : NULL;
        Params.szExpectedDataHash = szExpectedDataHash;
        Params.szFileName = (PfnRunTest != Storage_ExtractFiles) ? szExpectedNameHash : NULL;
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
    nLength = CascStrPrintf(szParamsA, _countof(szParamsA), "%s:%s", CASC_WORK_ROOT, szCodeName);
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
    //{"2014 - Heroes of the Storm/29049", "98396c1a521e5dee511d835b9e8086c7", "8febac8275e204800e5a4da0259e91c9", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    //{"2014 - Heroes of the Storm/30027", "6bcbe7c889cc465e4993f92d6ae1ee75", "54ed1440368de80723eddd89931fe191", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    //{"2014 - Heroes of the Storm/30414", "4b5d1f21de95c2a448684f98cc157f10", "ff32ed33bfcb40e01bf75c8df381eca5", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    //{"2014 - Heroes of the Storm/31726", "8b7633e519b78c96c85a1faa1c9f151f", "a0fd31d04f1bd6c5b3532c72592abf19", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    //{"2014 - Heroes of the Storm/39445", "c672b26f8f14ab2e68a9f9d7d6ca6062", "5ab7d596b5d6025072d7f331b3d7167a", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    //{"2014 - Heroes of the Storm/50286", "d1d57e83cbd72cbecd76916c22f6c4b6", "572598a728ac46dd18278636394c4fbc", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    //{"2014 - Heroes of the Storm/65943", "c5d75f4e12dbc05d4560fe61c4b88773", "981b882e090bdc027910ba70744c0e2c", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    //{"2015 - Diablo III/30013",          "86ba76b46c88eb7c6188d28a27d00f49", "b642f0dd232c591f05e6bdd65e28da82", "ENCODING"},
    //{"2015 - Diablo III/50649",          "18cd3eb87a46e2d3aa0c57d1d8f8b8ff", "84f4d3c1815afd69fc7edd8fb403815d", "ENCODING"},

    //{"2015 - Overwatch/24919/data/casc", "53afa15570c29bd40bba4707b607657e", "117073f6e207e8cdcf43b705b80bf120", "ROOT"},
    //{"2015 - Overwatch/47161",           "53db1f3da005211204997a6b50aa71e1", "434d7ff16fe0d283a2dacfc1390cb16e", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

    //{"2016 - Starcraft II/45364/\\/",    "28f8b15b5bbd87c16796246eac3f800c", "4f5d1cd5453557ef7e10d35975df2b12", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    //{"2016 - WoW/18125",                 "d259ca3ed110ed15eab4b1f878698ba9", "515e1e4a52e01164325381e32eefb4b8", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/18379",                 "d1845d0e89a42e8abef58810ace0bff0", "6b1c506bb469c4a720a013fc370222d9", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/18865",                 "6166ff84ff51d98c842484474befdff4", "65d3e817e66ab6b8590fbc8993c1d8d2", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/18888",                 "86a9e3fbfdf918d8ef04c9c7c4d539ec", "89f71a4a0dafde62779494f05538191f", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/19116",                 "80d44137f73304aad50058bf7c9665db", "13748b5c63a0208a0249fefdc7ad2107", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/19342",                 "88c38a8bae64f96e7909242dca0bcbca", "840ae2d707c39b7bb505bdf44577881e", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/21742",                 "f2fae76c751f37ab96e055c29509d0b1", "14af3edcb36182f40d0be939db9a2bf4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/22267",                 "e7ff9e86262cb76d6942d3d0c3e9cc8f", "1fccee4bd1ce543b675895a44136e5f6", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/23420",                 "86606a70b8ef7c6852fbeed74d12a76e", "53abbd39879c5bdc687f904da3adff7d", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    //{"2016 - WoW/29981",                 "10cfc2ab6cad8f10bb2d3d9d1af3a9c9", "ca40f5e4441a047e4a9ccf83de62398b", "dbfilesclient\\battlepetspeciesstate.db2"},
    {"2016 - WoW/30993:wow",             "c8ac90663b2be8321cee7469baa0279e", "62559142b42ef19fe949439a2faa9152", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/30993:wowt",            "01f84acbaaa4bde0532aa2e6490c6162", "80334898119c04ae50032cb2c537dea4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/30993:wow_classic",     "01f84acbaaa4bde0532aa2e6490c6162", "80334898119c04ae50032cb2c537dea4", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},

    {"2017 - Starcraft1/2457",           "3eabb81825735cf66c0fc10990f423fa", "2ed3292de2285f7cf1f9c889a318b240", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4037",           "bb2b76d657a841953fe093b75c2bdaf6", "5bf1dc985f0957d3ba92ed9c5431b31b", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4261",           "59ea96addacccb73938fdf688d7aa29b", "4bade608b78b186a90339aa557ad3332", "music\\radiofreezerg.ogg"},

    {"2018 - New CASC/00001",            "43d576ee81841a63f2211d43a50bb593", "2b7829b59c0b6e7ca6f6111bfb0dc426", "ROOT"},
    {"2018 - New CASC/00002",            "1c76139b51edd3ee114b5225d1b44c86", "4289e1e095dbfaec5dd926b5f9f22c6f", "ENCODING"},

    {"2018 - Warcraft III/09655",        "b1aeb7180848b83a7a3132cba608b254", "5d0e71a47f0b550de6884cfbbe3f50e5", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"2018 - Warcraft III/11889",        "f084ee1713153d8a15f1f75e94719aa8", "3541073dd77d370a01fbbcadd029477e", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },

    //{"2018 - CoD4/3376209",              "9a2ddcfffb7629c0d843f662885975a0", "bf4afee17e60183fbe80b8b5b989e786", "zone/base.xpak" },

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
//  CASC_FIND_DATA cf = {0};
    DWORD dwErrCode = ERROR_SUCCESS;

    printf("\n");

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif  // defined(_MSC_VER) && defined(_DEBUG)

    //
    // Single tests
    //

//  LocalStorage_Test(Storage_OpenFiles, "2014 - Heroes of the Storm\\29049", "ENCODING");
//  LocalStorage_Test(Storage_EnumFiles, "2015 - Diablo III\\30013");
//  LocalStorage_Test(Storage_OpenFiles, "2016 - WoW\\30993", "dbfilesclient\\battlepetspeciesstate.db2");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - New CASC\\00001");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - New CASC\\00002");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - Warcraft III\\11889");
//  LocalStorage_Test(Storage_SeekTest, "2018 - CoD4\\3376209", "zone/base.xpak");
    //OnlineStorage_Test(Storage_ExtractFiles, "agent");
    //OnlineStorage_Test(Storage_SeekTest, "viper", "us", "zone/base.xpak");

    // "dbfilesclient\\battlepetspeciesstate.db2"
    //LocalStorage_Test(Storage_OpenFiles, "z:\\Hry\\World of Warcraft\\Data", "File00666606.bin");
    //LocalStorage_Test(Storage_OpenFiles, "z:\\Hry\\World of Warcraft\\Data", "FILE000C3B2D.bin");

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
        dwErrCode = LocalStorage_Test(Storage_ExtractFiles, StorageInfo1[i].szPath, StorageInfo1[i].szNameHash, StorageInfo1[i].szDataHash);
//      dwErrCode = LocalStorage_Test(Storage_EnumFiles, StorageInfo1[i].szPath);
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
