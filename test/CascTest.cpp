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

    // Open the CASC file
    if(CascOpenFile(Params.hStorage, szOpenName, 0, Params.dwOpenFlags | CASC_STRICT_DATA_CHECK, &hFile))
    {
        // Retrieve the information about file spans
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
        assert(GetLastError() != ERROR_SUCCESS);
        dwErrCode = GetLastError();
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

    return 0;
}

static void RunExtractWorkers(PCASC_FIND_DATA_ARRAY pFiles)
{
#ifdef PLATFORM_WINDOWS

    SYSTEM_INFO si;
    HANDLE WaitHandles[MAXIMUM_WAIT_OBJECTS];
    HANDLE hThread;
    DWORD dwThreadCount = 0;
    DWORD dwThreadId = 0;

    // Retrieve the number of processors. Let one third of them to do the work.
    GetSystemInfo(&si);
    si.dwNumberOfProcessors = (si.dwNumberOfProcessors > 2) ? (si.dwNumberOfProcessors - 2) : 1;

    // Create threads
    for(DWORD i = 0; i < si.dwNumberOfProcessors; i++)
    {
        if((hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Worker_ExtractFiles, pFiles, 0, &dwThreadId)) == NULL)
            break;
        WaitHandles[dwThreadCount++] = hThread;
    }

    // Wait for all threads to complete
    WaitForMultipleObjects(dwThreadCount, WaitHandles, TRUE, INFINITE);

    // Close all threads
    for(DWORD i = 0; i < dwThreadCount; i++)
        CloseHandle(WaitHandles[i]);

#else

    // For non-Windows systems, directly call one thread
    Worker_ExtractFiles(pFiles);

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
    Params.fp1 = OpenOutputTextFile(hStorage, "\\list-%s-%u-002.txt");

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
        pFiles->ItemIndex = 0;
        pFiles->ItemCount = 0;

        // Iterate over the storage
        LogHelper.PrintProgress("Searching storage ...");
        hFind = CascFindFirstFile(hStorage, "*", &pFiles->cf[dwFileIndex++], szListFile);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            // Keep searching as long as we found something
            while (bFileFound)
            {
                // Prevent array overflow
                if(dwFileIndex < dwTotalFileCount)
                {
                    bFileFound = CascFindNextFile(hFind, &pFiles->cf[dwFileIndex++]);
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
            if(pFiles->ItemCount && Params.bExtractFiles)
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
            if(Params.bExtractFiles)
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
            dwErrCode = GetLastError();
        }

        // Free the file array
        CASC_FREE(pFiles);
    }
    else
    {
        LogHelper.PrintMessage("Error: Failed to allocate buffer for files enumeration.");
        dwErrCode = GetLastError();
    }

    if(Params.fp1)
        fclose(Params.fp1);
    return dwErrCode;
}

static DWORD Storage_ReadFiles(TLogHelper & LogHelper, TEST_PARAMS & Params)
{
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
    {"2014 - Heroes of the Storm/29049", "12cda9bb481920355b115b94fbb15790", "3f8992f4c6bac22daba9151d33f52d3e", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    {"2014 - Heroes of the Storm/30027", "e8c6b0f329696fde2fb9a74c73e81646", "5673222c5667c64e34c22599e9059d2a", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    {"2014 - Heroes of the Storm/30414", "f4278ae79295b0129e853b8f929199e8", "d70bdeaf19655e78cf44f93cd8161873", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    {"2014 - Heroes of the Storm/31726", "0a94e9b7243f47d41c0d5c98f59faec9", "1cd4ac0a74999103a80f45f2f1f523f3", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"2014 - Heroes of the Storm/39445", "e04074b1ac28fd92980bbe1b80a066ec", "b15409ce7b7cc554c45f23d20f226894", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"2014 - Heroes of the Storm/50286", "bdfc5c4e202080ee2885cd58a5110b5b", "46d8e408ce8c48aaff8c22c8606b45a8", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"2014 - Heroes of the Storm/65943", "6ca09ac7728eb1add2b97ccbb4c957ce", "2c08af8def931e84ecf454818a4bc568", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"2014 - Heroes of the Storm/75589", "ab60f2cb3a5db5a0523706301007b15a", "3688d04130644370cc4ec5ce63fd02d9", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},

    {"2015 - Diablo III/30013",          "bf2f439ff9e19fd2a5a4ed29f9f7d59d", "944c4a2eeef22f8d0e98d692f01e9e39", "ENCODING"},
    {"2015 - Diablo III/50649",          "a7638b59b6affdb0322aae104c8a999a", "f98ae64847dd43338bd1d6e0d01bd4bb", "ENCODING"},
    {"2015 - Diablo III/58979",          "f37bb5352d7e9b13dec14e7229b0457c", "71bce57edae17a6595f955836f8e1b9e", "ENCODING"},

    {"2015 - Overwatch/24919/data/casc", "224547726def07712076d2e19182749b", "c68d583ae5d09bcafbd825cab6718203", "ROOT"},
    {"2015 - Overwatch/47161",           "f8d5ea63582eb259c7f1b062308b9757", "6a88777378dbe006b1d764a23c64ce71", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},

    {"2016 - Starcraft II/45364/\\/",    "7cd0e7e614f3ba7c62337094ffa67ea3", "df14e1c8b8326c9604be16ae7efe1df0", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},
    {"2016 - Starcraft II/75025",        "734514130ca83ef908f37b1d79ef2633", "e9ebfde6cfe74228ac2e26b52f422422", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    {"2016 - WoW/18125",                 "e5541b24851b2b4c23f7ca8203fadeda", "14c09eff0ba3843ac9f06ec7db578c3d", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18379",                 "b2956885147f5aef2243d95010ab257e", "c98443415c5cf5d6b3b2c4a2efd3b97e", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18865",                 "d1f9e440740e349d691abad752751c8e", "83a2a7db4cd26c13783c63618de9ee90", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/18888",                 "e37f70b072733264d86bd3cf33d9fb39", "a30b96a2dbcbfad0604860481d894ada", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19116",                 "ce925830d1a8916ee8a2fb3dcd1dcab3", "2814c70c6e088abc6238ada44730d76a", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/19342",                 "f2900ea0ea3b6d5825ac2feb7db2dcea", "f72c6384f00255547349eb87ba48e81e", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/21742",                 "d846a90b0eeec9394e33c7a9d674c7ae", "56ae48c17e6fd6beee65a36bf5dc9177", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/22267",                 "9c8f30d9dacaac6af741dd643d74e78f", "15414142b9c15d44a0e5aacd7ab3bdeb", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/23420",                 "160bdcb74e4223ee30b803d51ed77546", "431eb8f6ddc26698d6e493aa1130b1ea", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/29981",                 "200527ddd3412ff0b2a13766eeb24cf0", "aaf0a56a9981634f13d99cf2d479f693", "dbfilesclient\\battlepetspeciesstate.db2"},
    {"2016 - WoW/31299:wow",             "73d2a11ee8e6790682aeda39a73c0c60", "2fc120236d8172dc3bbfb7ba1935702e", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/31299:wowt",            "64bfa2200500a3d41740ed1e07824712", "965d9632b417cacd6dc49f3895cef15a", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"2016 - WoW/31299:wow_classic",     "82cbce8cc497e15b77c83ec1fc4aae70", "4955242abf5cfe19430e51ed3d9bdd3f", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},

    {"2017 - Starcraft1/2457",           "e49f1880a14e3197d3bc05aea3befb12", "a7fab5de5d8d072f632930892e05c1c2", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4037",           "9536c1c74703c117496189c507c8758c", "52241438feebeea43cbe8746aa4af893", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/4261",           "64a95b66ab75c9d75bbbd1121324e2f7", "884081fd987dd50412c4446ab8260007", "music\\radiofreezerg.ogg"},
    {"2017 - Starcraft1/6434",           "902cef2cf30171bf1da4f0544404266f", "c197e633f3d3f957589cf90dc58371a8", "music\\radiofreezerg.ogg"},

    {"2018 - New CASC/00001",            "44833489ccf495e78d3a8f2ee9688ba6", "96e6457b649b11bcee54d52fa4be12e5", "ROOT"},
    {"2018 - New CASC/00002",            "0ada2ba6b0decfa4013e0465f577abf1", "4da83fa60e0e505d14a5c21284142127", "ENCODING"},

    {"2018 - Warcraft III/09655",        "f3f5470aa0ab4939fa234d3e29c3d347", "e45792b7459dc0c78ecb25130fa34d88", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"2018 - Warcraft III/11889",        "ff36cd4f58aae23bd77d4a90c333bdb5", "4cba488e57f7dccfb77eca8c86578a37", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },

    {"2018 - CoD4/3376209",              "e01180b36a8cfd82cb2daa862f5bbf3e", "79cd4cfc9eddad53e4b4d394c36b8b0c", "zone/base.xpak" },

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

//  LocalStorage_Test(Storage_ReadFiles, "2014 - Heroes of the Storm\\29049", NULL, NULL, "ENCODING");
//  LocalStorage_Test(Storage_ReadFiles, "2014 - Heroes of the Storm\\30414", NULL, NULL, "84fd9825f313363fd2528cd999bcc852");
//  LocalStorage_Test(Storage_EnumFiles, "2015 - Diablo III\\30013");
//  LocalStorage_Test(Storage_ReadFiles, "2016 - WoW/31299:wow_classic", NULL, NULL, "PATCH");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - New CASC\\00001");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - New CASC\\00002");
//  LocalStorage_Test(Storage_EnumFiles, "2018 - Warcraft III\\11889");
    LocalStorage_Test(Storage_EnumFiles, "d:\\Hry\\World of Warcraft:wowt");
//  LocalStorage_Test(Storage_SeekFiles, "2018 - CoD4\\3376209", NULL, NULL, "zone/base.xpak");
    //OnlineStorage_Test(Storage_OpenFiles, "agent", NULL, "PATCH");
    //OnlineStorage_Test(Storage_EnumFiles, "wow_classic_beta", "us");

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
