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

#ifdef __has_include
  #if __has_include(<thread>)
    #define PLATFORM_STD_THREAD
    #include <vector>
    #include <thread>
  #endif
#endif

#include "../src/CascLib.h"
#include "../src/CascCommon.h"

#include "TLogHelper.cpp"

#ifdef _MSC_VER
#pragma warning(disable: 4505)              // 'XXX' : unreferenced local function has been removed
#include <crtdbg.h>
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
#define CASC_PATH_ROOT "/media/ladik/CascStorages/CASC"
#define CASC_WORK_ROOT "/home/ladik/CASC/Work"
#endif

#ifdef PLATFORM_MAC
#define CASC_PATH_ROOT "/media/ladik/CascStorages"
#define CASC_WORK_ROOT "/home/ladik/CASC/Work"  // TODO
#endif

static const char szCircleChar[] = "|/-\\";

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
    DWORD bCheckFileData:1;
};

typedef struct _CASC_FIND_DATA_ARRAY
{
    TEST_PARAMS * pTestParams;
    TLogHelper * pLogHelper;
    HANDLE hStorage;
    DWORD ItemIndex;                // Next index of item that will be retrieved by a worker thread
    DWORD ItemCount;                // Total number of items

    CASC_FIND_DATA cf[1];

} CASC_FIND_DATA_ARRAY, *PCASC_FIND_DATA_ARRAY;

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
        bIsKey = (BinaryFromString(szFileName, MD5_STRING_SIZE, KeyBuffer) == ERROR_SUCCESS);
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

static bool AppendParamSuffix(LPSTR szBuffer, size_t cchBuffer, LPCSTR szSuffix)
{
    LPSTR szBufferPtr = szBuffer + strlen(szBuffer);
    LPSTR szBufferEnd = szBuffer + cchBuffer;

    if(szSuffix && szSuffix[0])
    {
        // Append the colon
        if((szBufferPtr + 1) < szBufferEnd)
        {
            *szBufferPtr++ = ':';
        }

        // Append the suffix
        if((szBufferPtr + strlen(szSuffix)) < szBufferEnd)
        {
            CascStrCopy(szBufferPtr, (szBufferEnd - szBufferPtr), szSuffix);
            return true;
        }
    }

    return false;
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

static const char * GetHash(MD5_CTX & HashContext, char * szBuffer)
{
    unsigned char md5_binary[MD5_HASH_SIZE];

    // Finalize the hashing
    MD5_Final(md5_binary, &HashContext);
    StringFromBinary(md5_binary, MD5_HASH_SIZE, szBuffer);
    return szBuffer;
}

static DWORD ExtractFile(TLogHelper & LogHelper, TEST_PARAMS & Params, CASC_FIND_DATA & cf)
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

    // Show the progress, if open succeeded
    LogHelper.PrintProgress("Extracting: (%u of %u) %s ...", LogHelper.FileCount, LogHelper.TotalFiles, szShortName);

    // Did the open succeed?
    if(CascOpenFile(Params.hStorage, szOpenName, 0, Params.dwOpenFlags | CASC_STRICT_DATA_CHECK, &hFile))
    {
        // Retrieve the information about file spans.
        if((pSpans = GetFileInfo(hFile, FileInfo)) != NULL)
        {
            ULONGLONG FileSize = FileInfo.ContentSize;
            ULONGLONG TotalRead = 0;
            DWORD dwBytesRead = 0;

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
                    dwErrCode = ERROR_NOT_ENOUGH_MEMORY;
                    break;
                }

                // Show the progress, if open succeeded
                //LogHelper.PrintProgress("Extracting: (%u of %u) %s (%u%%) ...", LogHelper.FileCount, LogHelper.TotalFiles, szShortName, (DWORD)((TotalRead * 100) / FileInfo.ContentSize));

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
                            CopyMemory16(pFileSpan->CKey, ContentKey);
                        }
                    }

                    // Increment the total bytes read
                    TotalRead += dwBytesRead;

                    // If we read less than expected, we report read error
                    bReadOk = (dwBytesRead == cbFileSpan);
                }

                // Was there an error reading data?
                if(bReadOk == false)
                {
                    // Do not report some errors; for example, when the file is encrypted,
                    // we can't do much about it. Only report it if we are going to extract one file
                    switch(dwErrCode = GetCascError())
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
            LogHelper.IncrementTotalBytes(TotalRead);

            // Free the span array
            CASC_FREE(pSpans);
        }

        // Increment the number of files processed
        CascInterlockedIncrement(&LogHelper.FileCount);

        // Close the handle
        CascCloseFile(hFile);
    }
    else
    {
        LogHelper.PrintError("Warning: %s: Open error", szShortName);
        assert(GetCascError() != ERROR_SUCCESS);
        dwErrCode = GetCascError();
    }

    return dwErrCode;
}

static PCASC_FIND_DATA GetNextInLine(PCASC_FIND_DATA_ARRAY pFiles)
{
    DWORD ItemIndex;

    // Atomically increment the value in the file array
    ItemIndex = CascInterlockedIncrement(&pFiles->ItemIndex) - 1;
    if(ItemIndex < pFiles->ItemCount)
        return &pFiles->cf[ItemIndex];

    // If we overflowed the total number of files, it means that we are done
    return NULL;
}

static DWORD WINAPI Worker_ExtractFiles(PCASC_FIND_DATA_ARRAY pFiles)
{
    PCASC_FIND_DATA pFindData;

    // Retrieve the next-in-line found file
    while((pFindData = GetNextInLine(pFiles)) != NULL)
    {
        ExtractFile(*pFiles->pLogHelper, *pFiles->pTestParams, *pFindData);
    }

    // Keep extracting files for a very long time
//  for (size_t i = 0; i < 1000000; i++)
//  {
//      ExtractFile(*pFiles->pLogHelper, *pFiles->pTestParams, pFiles->cf[rand() % pFiles->ItemCount]);
//  }

    return 0;
}

static void RunExtractWorkers(PCASC_FIND_DATA_ARRAY pFiles)
{
#ifdef PLATFORM_STD_THREAD

    std::vector<std::thread> threads;
    size_t dwCoresUsed = 10;

#ifdef PLATFORM_WINDOWS
    // Retrieve the number of available cores
    SYSTEM_INFO si = {0};
    DWORD dwFreeCPUs = 2;

    GetSystemInfo(&si);
    dwCoresUsed = (si.dwNumberOfProcessors > dwFreeCPUs) ? (si.dwNumberOfProcessors - dwFreeCPUs) : 1;
    if(dwCoresUsed > MAXIMUM_WAIT_OBJECTS)
        dwCoresUsed = MAXIMUM_WAIT_OBJECTS;
#endif

    // Run up to 40 worker threads
    for (size_t i = 0; i < dwCoresUsed; i++)
    {
        threads.emplace_back(&Worker_ExtractFiles, pFiles);
    }

    // Let them threads finish their job
    for (auto &thread : threads)
    {
        thread.join();
    }

#else

    SYSTEM_INFO si = { 0 };
    HANDLE ThreadHandles[MAXIMUM_WAIT_OBJECTS];
    DWORD dwCoresUsed;
    DWORD dwThreadId;
    DWORD dwFreeCpus = 2;
    DWORD dwThreads = 0;

    // Retrieve the number of available cores
    GetSystemInfo(&si);
    dwCoresUsed = (si.dwNumberOfProcessors > dwFreeCpus) ? (si.dwNumberOfProcessors - dwFreeCpus) : 1;
    if(dwCoresUsed > _countof(ThreadHandles))
        dwCoresUsed = _countof(ThreadHandles);

    // Run up to 40 worker threads
    for (DWORD i = 0; i < dwCoresUsed; i++)
    {
        ThreadHandles[dwThreads] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Worker_ExtractFiles, pFiles, 0, &dwThreadId); 
        if(ThreadHandles[dwThreads] != NULL)
            dwThreads++;
    }

    // Let them threads finish their job
    WaitForMultipleObjects(dwThreads, ThreadHandles, TRUE, INFINITE);

#endif
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
                // Show the progress to the user
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
                    dwErrCode = GetCascError();
                    break;
                }

                // Write to the target file
                if(!FileStream_Write(pStream, &TotalRead, Buffer, dwBytesRead))
                {
                    LogHelper.PrintMessage("Error: Failed to write %u bytes at offset %llX.", dwBytesToRead, TotalRead);
                    dwErrCode = GetCascError();
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

                    // Show the progress
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
                        dwErrCode = GetCascError();
                        break;
                    }

                    // Load data from the local file
                    if(!FileStream_Read(pStream, &ByteOffset, Buffer2, Length))
                    {
                        LogHelper.PrintMessage("Error: Failed to read %u bytes from LOCAL file (offset %llX).", Length, ByteOffset);
                        dwErrCode = GetCascError();
                        break;
                    }

                    // Compare the loaded data blocks
                    if(memcmp(Buffer, Buffer2, Length))
                    {
                        LogHelper.PrintMessage("Error: Data mismatchat offset %llX, length %u.", ByteOffset, Length);
                        dwErrCode = GetCascError();
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
    PCASC_FIND_DATA_ARRAY pFiles;
    CASC_FIND_DATA cf;
    MD5_CTX NameHashCtx;
    MD5_CTX DataHashCtx;
    LPCTSTR szListFile = GetTheProperListfile(Params.hStorage, Params.szListFile);
    LPCSTR szNameHash;
    LPCSTR szDataHash;
    HANDLE hStorage = Params.hStorage;
    HANDLE hFind;
    size_t cbToAllocate = 0;
    DWORD dwTotalFileCount = 0;
    DWORD dwFileIndex = 0;
    DWORD dwErrCode = ERROR_SUCCESS;
    char szHashString[MD5_STRING_SIZE+1];
    char szTotalBytes[0x20];
//  char szShortName[SHORT_NAME_SIZE];
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

    // Init both hashers
    MD5_Init(&NameHashCtx);
    MD5_Init(&DataHashCtx);

    // Allocate the structure holding all file information
    cbToAllocate = sizeof(CASC_FIND_DATA_ARRAY) + (dwTotalFileCount * sizeof(CASC_FIND_DATA));
    if((pFiles = (PCASC_FIND_DATA_ARRAY)(CASC_ALLOC<BYTE>(cbToAllocate))) != NULL)
    {
        // Init the structure
        pFiles->pTestParams = &Params;
        pFiles->pLogHelper = &LogHelper;
        pFiles->hStorage = hStorage;
        pFiles->ItemIndex = 0;
        pFiles->ItemCount = 0;

        // Iterate over the storage
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &pFiles->cf[dwFileIndex], szListFile);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            // Keep searching as long as we found something
            while (bFileFound)
            {
                // Increment the index
//              if(!_stricmp(pFiles->cf[dwFileIndex].szFileName, "00000f8465973be812c8f2f7c105f02f"))
//                  __debugbreak();
                dwFileIndex++;

                // Prevent array overflow
                if(dwFileIndex < dwTotalFileCount)
                {
                    bFileFound = CascFindNextFile(hFind, &pFiles->cf[dwFileIndex]);
                }
                else
                {
                    bFileFound = CascFindNextFile(hFind, &cf);
                }
            }

            // Finalize searching
            pFiles->ItemCount = dwFileIndex;
            CascFindClose(hFind);

            // Extract the found file if available locally
            if(pFiles->ItemCount && Params.bCheckFileData)
            {
                RunExtractWorkers(pFiles);
            }

            // Get the compound name and data hash
            for(DWORD i = 0; i < pFiles->ItemCount; i++)
            {
                // Print the file name, if needed
                if (Params.fp1 != NULL)
                    fprintf(Params.fp1, "%s\n", pFiles->cf[i].szFileName);
                assert(pFiles->cf[i].szFileName[0] != 0);

                // Update name hash and data hash
                MD5_Update(&NameHashCtx, pFiles->cf[i].szFileName, (unsigned long)(strlen(pFiles->cf[i].szFileName) + 1));
                MD5_Update(&DataHashCtx, pFiles->cf[i].CKey, MD5_HASH_SIZE);
            }

            // Show the total number of extracted data
            if(Params.bCheckFileData)
            {
                LogHelper.FormatTotalBytes(szTotalBytes, _countof(szTotalBytes));
                LogHelper.PrintMessage("Extracted: %u of %u files (%s bytes total)", LogHelper.FileCount, LogHelper.TotalFiles, szTotalBytes);
            }

            // Show the name hash
            if((szNameHash = GetHash(NameHashCtx, szHashString)) != NULL)
            {
                LogHelper.PrintMessage("Name Hash: %s%s", szNameHash, GetHashResult(Params.szExpectedNameHash, szNameHash));
            }

            // Show the data hash
            if((szDataHash = GetHash(DataHashCtx, szHashString)) != NULL)
            {
                LogHelper.PrintMessage("Data Hash: %s%s", szDataHash, GetHashResult(Params.szExpectedDataHash, szDataHash));
            }

            LogHelper.PrintTotalTime();
        }
        else
        {
            LogHelper.PrintMessage("Error: Failed to enumerate the storage.");
            dwErrCode = GetCascError();
        }

        // Free the file array
        CASC_FREE(pFiles);
    }
    else
    {
        LogHelper.PrintMessage("Error: Failed to allocate buffer for files enumeration.");
        dwErrCode = GetCascError();
    }

    if(Params.fp1)
        fclose(Params.fp1);
    return dwErrCode;
}

static DWORD Storage_ReadFiles(TLogHelper & LogHelper, TEST_PARAMS & Params)
{
    Params.bCheckFileData = true;
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
        assert(GetCascError() != ERROR_SUCCESS);
        dwErrCode = GetCascError();
    }

    return dwErrCode;
}

static DWORD SpeedStorage_Test(PFN_RUN_TEST PfnRunTest, LPCSTR szStorage, LPCSTR szExpectedNameHash = NULL, LPCSTR szExpectedDataHash = NULL, LPCSTR szFileName = NULL)
{
    TLogHelper LogHelper(szStorage);
    HANDLE hStorage;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwErrCode = ERROR_SUCCESS;
    int nOpenCount = 100;

    // Keep compiler happy
    CASCLIB_UNUSED(PfnRunTest);
    CASCLIB_UNUSED(szExpectedNameHash);
    CASCLIB_UNUSED(szExpectedDataHash);
    CASCLIB_UNUSED(szFileName);

    // Prepare the full path of the storage
    MakeFullPath(szFullPath, _countof(szFullPath), szStorage);

    // Open the storage for the first time to load all files to the cache
    LogHelper.PrintProgress("Opening storage (caching-in) ...");
    if(CascOpenStorage(szFullPath, 0, &hStorage))
    {
        // Close right away
        CascCloseStorage(hStorage);
        hStorage = NULL;

        // Set the start time of the operation
        LogHelper.SetStartTime();

        // Now open the storage again, many times in order to measure how fast can we load it
        for(int i = 0; i < nOpenCount; i++)
        {
            LogHelper.PrintProgress("Opening storage (%u /%u) ...", i, nOpenCount);
            if(!CascOpenStorage(szFullPath, 0, &hStorage))
            {
                LogHelper.PrintError("Error: Failed to open storage %s", szStorage);
                break;
            }

            CascCloseStorage(hStorage);
            hStorage = NULL;
        }

        // Print the total time
        LogHelper.PrintTotalTime();
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", szStorage);
        assert(GetCascError() != ERROR_SUCCESS);
        dwErrCode = GetCascError();
    }

    return dwErrCode;
}

static DWORD OnlineStorage_Test(PFN_RUN_TEST PfnRunTest, LPCSTR szCodeName, LPCSTR szRegion = NULL, LPCSTR szBuildKey = NULL, LPCSTR szFileName = NULL)
{
    TLogHelper LogHelper(szCodeName);
    HANDLE hStorage;
    TCHAR szParamsT[MAX_PATH+0x40];
    char szParamsA[MAX_PATH+0x40];
    DWORD dwErrCode = ERROR_SUCCESS;

    // Prepare the path
    CascStrPrintf(szParamsA, _countof(szParamsA), "%s/%s", CASC_WORK_ROOT, szCodeName);

    // Append codename, if any
    if(AppendParamSuffix(szParamsA, _countof(szParamsA), szCodeName))
    {
        // Append region, if any
        if(AppendParamSuffix(szParamsA, _countof(szParamsA), szRegion))
        {
            // Append build key, if any
            AppendParamSuffix(szParamsA, _countof(szParamsA), szBuildKey);
        }
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
        assert(GetCascError() != ERROR_SUCCESS);
        dwErrCode = GetCascError();
    }

    return dwErrCode;
}

//-----------------------------------------------------------------------------
// Storage list

static STORAGE_INFO1 StorageInfo1[] =
{
    //- Name of the storage folder -------- Compound file name hash ----------- Compound file data hash ----------- Example file to extract -----------------------------------------------------------
    {"Beta TVFS/00001",             "44833489ccf495e78d3a8f2ee9688ba6", "96e6457b649b11bcee54d52fa4be12e5", "ROOT"},
    {"Beta TVFS/00002",             "0ada2ba6b0decfa4013e0465f577abf1", "4da83fa60e0e505d14a5c21284142127", "ENCODING"},

    {"CoD4/3376209",                "e01180b36a8cfd82cb2daa862f5bbf3e", "79cd4cfc9eddad53e4b4d394c36b8b0c", "zone/base.xpak" },

    {"Diablo III/30013",            "86ba76b46c88eb7c6188d28a27d00f49", "19e37cc3c178ea0521369c09d67791ac", "ENCODING"},
    {"Diablo III/50649",            "18cd3eb87a46e2d3aa0c57d1d8f8b8ff", "9225b3fa85dd958209ad20495ff6457e", "ENCODING"},
    {"Diablo III/58979",            "3c5e033739bb58ce1107e59b8d30962a", "901dd9dde4e793ee42414c81874d1c8f", "ENCODING"},
    {"Diablo III/68722",            "34cb5a5cea775b7194d9cd0ec3458d3b", "eeaa6a963aa19d93bdafc049fe6d3aaf", "ENCODING"},

    {"Heroes of the Storm/29049",   "98396c1a521e5dee511d835b9e8086c7", "b37e7edc07d465a8e97b47cabcd3fc04", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    {"Heroes of the Storm/30027",   "6bcbe7c889cc465e4993f92d6ae1ee75", "978f6332a2f2149d74d48414b834c8f6", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    {"Heroes of the Storm/30414",   "4b377fa69dab736b2ae495920663832e", "367eef337676c902bf6855f54bbda182", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    {"Heroes of the Storm/31726",   "f997a06b3f8c10d9095e542f1ef83a74", "0eb064b28fc6203a48321a15d17f7df8", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"Heroes of the Storm/39445",   "c672b26f8f14ab2e68a9f9d7d6ca6062", "62376a66045c7806e865ef4b056c7060", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"Heroes of the Storm/50286",   "d1d57e83cbd72cbecd76916c22f6c4b6", "c1fe97f5fc04a2824449b6c43cf31ce5", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"Heroes of the Storm/65943",   "c5d75f4e12dbc05d4560fe61c4b88773", "f046b2ed9ecc7b27d2a114e16c34c8fd", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"Heroes of the Storm/75589",   "ae2209f1fcb26c730e9757a42bcce17e", "a7f7fbf1e04c87ead423fb567cd6fa5c", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"Heroes of the Storm/81376",   "25597a3f8adc3fa79df243197fecd1cc", "2c36eb3dde7d545a0fa413ccebf84202", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    {"Overwatch/24919/data/casc",   "53afa15570c29bd40bba4707b607657e", "6f9131fc0e7ad558328bbded2c996959", "ROOT"},
    {"Overwatch/47161",             "53db1f3da005211204997a6b50aa71e1", "12be32a2f86ea1f4e0bf2b62fe4b7f6e", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},
    {"Overwatch/72127",             "bef17230badb29e5c7dad18a2b30df8a", "bae70b787316d724646b954978284c14", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

    {"Starcraft/2457",              "3eabb81825735cf66c0fc10990f423fa", "ce752a323819c369fba03401ba400332", "music\\radiofreezerg.ogg"},
    {"Starcraft/4037",              "bb2b76d657a841953fe093b75c2bdaf6", "2f1e9df40da0f6f682ffecbbd920d4fc", "music\\radiofreezerg.ogg"},
    {"Starcraft/4261",              "59ea96addacccb73938fdf688d7aa29b", "4e07a768999c7887c8c21364961ab07a", "music\\radiofreezerg.ogg"},
    {"Starcraft/6434",              "e3f929b881ad07028578d202f97c107e", "9bf9597b1f10d32944194334e8dc442a", "music\\radiofreezerg.ogg"},
    {"Starcraft/8713",              "57da9e2768368d3e31473a70a9286a69", "6a425e9d9e7f3b44773a021ea89f85e3", "music\\radiofreezerg.ogg"},

    {"Starcraft II/45364/\\/",      "28f8b15b5bbd87c16796246eac3f800c", "f9cd7fc20fa53701846109d3d6947d08", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},
    {"Starcraft II/75025",          "79c044e1286b7b18478556e571901294", "e290febb90e06e97b4db6f0eb519ca91", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},
    {"Starcraft II/81102",          "cb6bea299820895f6dcbc72067553743", "63b47f03b1717ded751e0d24d3ddff4f", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    {"Warcraft III/09231",          "8147106d7c05eaaf3f3611cc6f5314fe", "1b47c84d9b4ce58beeb2604a934cf83c", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"Warcraft III/09655",          "f3f5470aa0ab4939fa234d3e29c3d347", "e45792b7459dc0c78ecb25130fa34d88", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"Warcraft III/11889",          "ff36cd4f58aae23bd77d4a90c333bdb5", "4cba488e57f7dccfb77eca8c86578a37", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"Warcraft III/13369",          "9c3fce648bf75d93a8765e84dcd10377", "4ac831db9bf0734f01b9d20455a68ab6", "ENCODING" },
    {"Warcraft III/14883",          "a4b269415f1f4adec4df8bb736dc1297", "3fd108674117ad4f93885bdd1a525f30", NULL },
    {"Warcraft III/15801",          "e1c3cfa897c8a25ef493455469955186", "f162cd3448219fd9956f9ff8fb5ba915", NULL },

    {"WoW/18125",                   "b31531af094f78f58592249c4d216a8e", "e5c9b3f0da7806d8b239c13bff1d836e", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/18379",                   "fab30626cf94ed1523519729c3701812", "606e4bfd6f8100ae875eb4c00789233b", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/18865",                   "7f252a8c6001938f601b0c91abbb0f2a", "cee96fa43cddc008f564b4615fdbd109", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/18888",                   "a007d0433c71ddc6e9acaa45cbdc4e61", "a093c596240a6b71de125eaa83ea8568", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/19116",                   "a3be9cfd4a15ba184e21eed9ec90417b", "11a973871aef6ab3236676a25381a1e6", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/19342",                   "66f0de0cff477e1d8e982683771f1ada", "69b4c91c977b875fd0a6ffbf89b06408", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/21742",                   "a357c3cbed98e83ac5cd394ceabc01e8", "90ce1aac44299aa2ac6fb44d249d2561", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/22267",                   "101949dfbed06d417d24a65054e8a6b6", "4ef8df3cf9b00b5c7b2c1b9f4166ec0d", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/23420",                   "e62a798989e6db00044b079e74faa1eb", "854e58816e6eb2795d14fe81470ad19e", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/29981",                   "a35f7de61584644d4877aac1380ef090", "3cba30b5e439a6e59b0953d17da9ac6c", "dbfilesclient\\battlepetspeciesstate.db2"},
    {"WoW/31299:wow",               "6220549f2b8936af6e63179f6ece78ab", "05627c131969bd9394fb345f4037e249", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/31299:wowt",              "959fa63cbcd9ced02a8977ed128df828", "423c1b99b14a615a02d8ffc7a7eff4ef", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/31299:wow_classic",       "184794b8a191429e2aae9b8a5334651b", "b46bd2f81ead285e810e5a049ca2db74", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
};

static STORAGE_INFO2 StorageInfo2[] =
{
//  {"agent",       "us"},
//  {"bna",         "us"},
//  {"catalogs",    NULL},
//  {"clnt",        "us"},
//  {"hsb",         "us"},
    {"wow_classic", "us"},
};

//-----------------------------------------------------------------------------
// Main

int main(int argc, char * argv[])
{
    DWORD dwErrCode = ERROR_SUCCESS;

    printf("\n");

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif  // defined(_MSC_VER) && defined(_DEBUG)

#ifdef _DEBUG
    //PCASC_SOCKET pSocket;
    //const char * request;
    //char * response;

    //if((pSocket = sockets_connect("level3.blizzard.com", CASC_PORT_HTTP)) != NULL)
    //{
    //    request = "GET /tpr/wow/data/a3/e6/a3e604a2b89d7a9e0784cbbee57793b4.index HTTP/1.1\r\nHost: level3.blizzard.com\r\nConnection: Keep-Alive\r\n\r\n";
    //    response = pSocket->ReadResponse(request);
    //    if(response != NULL)
    //        CASC_FREE(response);

    //    pSocket->Release();
    //}
#endif

    //
    // Run tests for each storage entered on command line
    //
    for(int i = 1; i < argc; i++)
    {
        // Attempt to open the storage and extract single file
        dwErrCode = LocalStorage_Test(Storage_ReadFiles, argv[i], NULL, NULL);
        if(dwErrCode != ERROR_SUCCESS && dwErrCode != ERROR_FILE_NOT_FOUND)
            break;
    }

    //
    // Run the tests for every local storage in my collection
    //
    for(size_t i = 0; i < _countof(StorageInfo1); i++)
    {
        // Attempt to open the storage and extract single file
        dwErrCode = LocalStorage_Test(Storage_ReadFiles, StorageInfo1[i].szPath, StorageInfo1[i].szNameHash, StorageInfo1[i].szDataHash);
        if(dwErrCode != ERROR_SUCCESS && dwErrCode != ERROR_FILE_NOT_FOUND)
            break;
    }

    //
    // Run the tests for every available online storage in my collection
    //
    //for (size_t i = 0; i < _countof(StorageInfo2); i++)
    //{
    //    // Attempt to open the storage and extract single file
    //    dwErrCode = OnlineStorage_Test(Storage_EnumFiles, StorageInfo2[i].szCodeName, StorageInfo2[i].szRegion, StorageInfo2[i].szFile);
    //    if (dwErrCode != ERROR_SUCCESS)
    //        break;
    //}

#ifdef _MSC_VER
    //_CrtDumpMemoryLeaks();
#endif  // _MSC_VER

    return (int)dwErrCode;
}
