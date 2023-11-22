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
#define __CASCLIB_SELF__                   // Don't use CascLib.lib
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
#pragma comment(lib, "winmm.lib")
#endif

#ifdef CASCLIB_PLATFORM_LINUX
#include <dirent.h>
#endif

//------------------------------------------------------------------------------
// Defines

#ifdef CASCLIB_PLATFORM_WINDOWS
#define CASC_PATH_ROOT      "/Multimedia/CASC"
#define CASC_WORK_ROOT      "/Multimedia/CASC/Work"
#endif

#ifdef CASCLIB_PLATFORM_LINUX
#define CASC_PATH_ROOT      "/media/ladik/CascStorages/CASC"
#define CASC_WORK_ROOT      "/home/ladik/CASC/Work"
#endif

#ifdef CASCLIB_PLATFORM_MAC
#define CASC_PATH_ROOT      "/media/ladik/CascStorages"
#define CASC_WORK_ROOT      "/home/ladik/CASC/Work"         // TODO
#endif

static const char szCircleChar[] = "|/-\\";

#define SHORT_NAME_SIZE 59

//-----------------------------------------------------------------------------
// Local structures

// For local storages
typedef struct _STORAGE_INFO
{
    LPCSTR szPath;                          // Path to the CASC storage
    LPCSTR szNameHash;                      // MD5 of all file names extracted sequentially
    LPCSTR szDataHash;                      // MD5 of all file data extracted sequentially
    LPCSTR szFileName;                      // file in the storage
} STORAGE_INFO, *PSTORAGE_INFO;

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

static LPCTSTR szListFile_CSV = _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile.csv");
static LPCTSTR szListFile_TXT = _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile.txt");

//-----------------------------------------------------------------------------
// Local functions

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
        return szListFile_CSV;

    if(dwFeatures & CASC_FEATURE_FNAME_HASHES)
        return szListFile_TXT;

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

static const char * GetHash(LPBYTE md5_binary, char * szBuffer)
{
    StringFromBinary(md5_binary, MD5_HASH_SIZE, szBuffer);
    return szBuffer;
}

static const char * GetHash(MD5_CTX & HashContext, char * szBuffer)
{
    unsigned char md5_binary[MD5_HASH_SIZE];

    // Finalize the hashing
    MD5_Final(md5_binary, &HashContext);
    return GetHash(md5_binary, szBuffer);
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
    //BREAKIF(_stricmp(cf.szPlainName, "base:BaseComplexTypeDescriptorSizes.dat") == 0);
    //BREAKIF(_stricmp(cf.szPlainName, "DivideAndConquer.w3m:war3map.doo") == 0);
    MakeShortName(szShortName, sizeof(szShortName), cf);

    // Did the open succeed?
    if(CascOpenFile(Params.hStorage, szOpenName, 0, Params.dwOpenFlags | CASC_STRICT_DATA_CHECK | CASC_OPEN_CKEY_ONCE, &hFile))
    {
        // Retrieve the information about file spans.
        if((pSpans = GetFileInfo(hFile, FileInfo)) != NULL)
        {
            ULONGLONG FileSize = FileInfo.ContentSize;
            ULONGLONG TotalRead = 0;
            DWORD dwBytesRead = 0;

            // Print the current file
            //char szEKey[MD5_STRING_SIZE+1];
            //char szCKey[MD5_STRING_SIZE+1];
            //StringFromBinary(FileInfo.EKey, MD5_HASH_SIZE, szEKey);
            //StringFromBinary(FileInfo.CKey, MD5_HASH_SIZE, szCKey);
            //LogHelper.PrintMessage("%s -> %s: %u bytes", szEKey, szCKey, (DWORD)(FileInfo.ContentSize));

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
        // Ignore the ERROR_CKEY_ALREADY_OPENED open error
        if((dwErrCode = GetCascError()) == ERROR_CKEY_ALREADY_OPENED)
            return ERROR_SUCCESS;

        LogHelper.PrintError("Warning: %s: Open error", szShortName);
        assert(dwErrCode != ERROR_SUCCESS);
    }
    return dwErrCode;
}

static DWORD GetNumberOfWorkerThreads()
{
    DWORD dwThreadCount = 10;

    //
    // Retrieve the number of available cores on Windows
    //

#ifdef CASCLIB_PLATFORM_WINDOWS
    SYSTEM_INFO si = {0};
    DWORD dwFreeCPUs = 2;

    GetSystemInfo(&si);
    dwThreadCount = (si.dwNumberOfProcessors > dwFreeCPUs) ? (si.dwNumberOfProcessors - dwFreeCPUs) : 1;
    if(dwThreadCount > MAXIMUM_WAIT_OBJECTS)
        dwThreadCount = MAXIMUM_WAIT_OBJECTS;
#endif

#ifdef _DEBUG
    // Only 1 worker thread in debug version
    dwThreadCount = 1;
#endif

    return dwThreadCount;
}

static PCASC_FIND_DATA GetNextFindData(PCASC_FIND_DATA_ARRAY pFiles)
{
    TLogHelper * pLogHelper = pFiles->pLogHelper;
    DWORD ItemIndex;

    // Atomically increment the value in the file array
    ItemIndex = CascInterlockedIncrement(&pFiles->ItemIndex) - 1;
    if(ItemIndex < pFiles->ItemCount)
    {
        if(pLogHelper->TimeElapsed(1000))
            pLogHelper->PrintProgress("Extracting file %u of %u", ItemIndex, pFiles->ItemCount);
        return &pFiles->cf[ItemIndex];
    }

    // If we overflowed the total number of files, it means that we are done
    return NULL;
}

static DWORD WINAPI Worker_ExtractFiles(PCASC_FIND_DATA_ARRAY pFiles)
{
    PCASC_FIND_DATA pFindData;

    // Retrieve the next-in-line found file
    while((pFindData = GetNextFindData(pFiles)) != NULL)
    {
        ExtractFile(*pFiles->pLogHelper, *pFiles->pTestParams, *pFindData);
    }
    return 0;
}

static void RunExtractWorkers(PCASC_FIND_DATA_ARRAY pFiles)
{
#ifdef PLATFORM_STD_THREAD

    std::vector<std::thread> threads;
    size_t dwCoresUsed = GetNumberOfWorkerThreads();

    // Run up to 40 worker threads
    for(size_t i = 0; i < dwCoresUsed; i++)
    {
        threads.emplace_back(&Worker_ExtractFiles, pFiles);
    }

    // Let them threads finish their job
    for(auto &thread : threads)
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
    for(DWORD i = 0; i < dwCoresUsed; i++)
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
                if(LogHelper.TimeElapsed(1000))
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
                    if(LogHelper.TimeElapsed(1000))
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
    PCASC_FIND_DATA pFindData;
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
    //Params.fp1 = OpenOutputTextFile(hStorage, "\\list-%s-%u-001.txt");

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

#ifdef CASCLIB_WRITE_VERIFIED_FILENAMES
    Params.bCheckFileData = 0;
#endif

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
        if(hFind != INVALID_HANDLE_VALUE)
        {
            // Keep searching as long as we found something
            while(bFileFound)
            {
                // Increment the index
                if(LogHelper.TimeElapsed(1000))
                    LogHelper.PrintProgress("Searching storage (%u of %u) ...", dwFileIndex, dwTotalFileCount);
                dwFileIndex++;

                // Prevent array overflow
                pFindData = (dwFileIndex < dwTotalFileCount) ? &pFiles->cf[dwFileIndex] : &cf;
                bFileFound = CascFindNextFile(hFind, pFindData);
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
                if(Params.fp1 != NULL)
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

static DWORD LocalStorage_Test(PFN_RUN_TEST PfnRunTest, STORAGE_INFO & StorInfo)
{
    TLogHelper LogHelper(StorInfo.szPath);
    HANDLE hStorage;
    TCHAR szFullPath[MAX_PATH];
    DWORD dwErrCode = ERROR_SUCCESS;

    // Prepare the full path of the storage
    MakeFullPath(szFullPath, _countof(szFullPath), StorInfo.szPath);

    // Open the CASC storage
    LogHelper.PrintProgress("Opening storage ...");
    if(CascOpenStorage(szFullPath, 0, &hStorage))
    {
        TEST_PARAMS Params;

        // Configure the test parameters
        Params.hStorage = hStorage;
        Params.szExpectedNameHash = StorInfo.szNameHash;
        Params.szExpectedDataHash = StorInfo.szDataHash;
        Params.szFileName = StorInfo.szFileName;
        dwErrCode = PfnRunTest(LogHelper, Params);
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", StorInfo.szPath);
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

static bool WINAPI OnlineStorage_OpenCB(
    void * PtrUserParam,                        // User-specific parameter passed to the callback
    LPCSTR szWork,                              // Text for the current activity (example: "Loading "ENCODING" file")
    LPCSTR szObject,                            // (optional) name of the object tied to the activity (example: index file name)
    DWORD,                                      // (optional) current object being processed
    DWORD)                                      // (optional) If non-zero, this is the total number of objects to process
{
    TLogHelper * pLogHelper = (TLogHelper *)PtrUserParam;

    if(pLogHelper != NULL)
        pLogHelper->PrintProgress(szWork, szObject);
    return false;
}

static DWORD OnlineStorage_Test(PFN_RUN_TEST PfnRunTest, STORAGE_INFO & StorInfo)
{
    CASC_OPEN_STORAGE_ARGS OpenArgs = {sizeof(CASC_OPEN_STORAGE_ARGS)};
    TLogHelper LogHelper(StorInfo.szPath);
    HANDLE hStorage;
    TCHAR szParams[MAX_PATH+0x40];
    DWORD dwErrCode = ERROR_SUCCESS;

    LogHelper.PrintMessage("Root File: %s", StorInfo.szPath);
    LogHelper.PrintProgress("Opening storage ...");

    // Prepare the path
    CascStrPrintf(szParams, _countof(szParams), _T("%hs/%hs"), CASC_PATH_ROOT, StorInfo.szPath);

    // Prepare the callbacks
    OpenArgs.PfnProgressCallback = OnlineStorage_OpenCB;
    OpenArgs.PtrProgressParam = &LogHelper;

    // Enable or disable reusing VERSIONS and CDNS
    if(strstr(StorInfo.szPath, "current") != NULL)
        OpenArgs.dwFlags |= CASC_FEATURE_FORCE_DOWNLOAD;

    // Open the online storage
    if(CascOpenStorageEx(szParams, &OpenArgs, true, &hStorage))
    {
        TEST_PARAMS Params;

        // Check a specific file
        if(StorInfo.szFileName != NULL)
        {
            CASC_FILE_FULL_INFO FileInfo = {0};
            HANDLE hFile = NULL;
            char szBuffer[MD5_STRING_SIZE + 1];
            bool bSucceeded = false;

            // Just get the file info
            LogHelper.PrintProgress("Querying file \"%s\" ...", GetPlainFileName(StorInfo.szFileName));
            if(CascOpenFile(hStorage, StorInfo.szFileName, 0, CASC_OPEN_BY_NAME, &hFile))
            {
                if(CascGetFileInfo(hFile, CascFileFullInfo, &FileInfo, sizeof(CASC_FILE_FULL_INFO), NULL))
                {
                    LogHelper.PrintMessage("  File name:     %s", StorInfo.szFileName);
                    LogHelper.PrintMessage("  File hash:     %s", GetHash(FileInfo.CKey, szBuffer));
                    LogHelper.PrintMessage("  File size:     %08X", FileInfo.ContentSize);
                    LogHelper.PrintMessage("  Locale flags:  %08X", FileInfo.LocaleFlags);
                    LogHelper.PrintMessage("  Content flags: %08X", FileInfo.ContentFlags);
                    bSucceeded = true;
                }

                CascCloseFile(hFile);
            }

            // Get error code on failure
            if(bSucceeded == false)
            {
                dwErrCode = GetCascError();
                LogHelper.PrintError("Failed to retrieve file information.", StorInfo.szFileName);
            }
        }

        // Configure the test parameters
        Params.szExpectedNameHash = StorInfo.szNameHash;
        Params.szExpectedDataHash = StorInfo.szDataHash;
        Params.hStorage = hStorage;
        Params.szFileName = NULL;
        Params.bOnlineStorage = true;
        dwErrCode = PfnRunTest(LogHelper, Params);
        CascCloseStorage(hStorage);
    }
    else
    {
        LogHelper.PrintError("Error: Failed to open storage %s", StorInfo.szPath);
        assert(GetCascError() != ERROR_SUCCESS);
        dwErrCode = GetCascError();
    }

    return dwErrCode;
}

//-----------------------------------------------------------------------------
// Storage list

static STORAGE_INFO StorageInfo1[] =
{
    //- Storage folder name --------        - Compound file name hash --------  - Compound file data hash --------  - Example file to extract ---
//  {"Beta TVFS/00001",                     "be2ba8b6d02279a1b68c4ee28f07641f", "96e6457b649b11bcee54d52fa4be12e5", "ROOT"},
//  {"Beta TVFS/00002",                     "09fd84ef909ad314d84dc1f037e87ca3", "4da83fa60e0e505d14a5c21284142127", "ENCODING"},
/*
    {"CoD4/3376209",                        "e01180b36a8cfd82cb2daa862f5bbf3e", "79cd4cfc9eddad53e4b4d394c36b8b0c", "zone/base.xpak" },
    {"CoD4-MW/8042902/.build.info",         "cd54a9444812e168b3b920b1479eff71", "033f77f6309bf6c21984fc10d09e5a72" },

    {"Diablo II Resurrected/71776",         "8518f7457729368bcbfc8db60203de78", "180984fc02ee90875d0504952f177f9a", "ENCODING"},

    {"Diablo III/30013",                    "86ba76b46c88eb7c6188d28a27d00f49", "19e37cc3c178ea0521369c09d67791ac", "ENCODING"},
    {"Diablo III/50649",                    "18cd3eb87a46e2d3aa0c57d1d8f8b8ff", "9225b3fa85dd958209ad20495ff6457e", "ENCODING"},
    {"Diablo III/58979",                    "3c5e033739bb58ce1107e59b8d30962a", "901dd9dde4e793ee42414c81874d1c8f", "ENCODING"},
    {"Diablo III/68722",                    "34cb5a5cea775b7194d9cd0ec3458d3b", "eeaa6a963aa19d93bdafc049fe6d3aaf", "ENCODING"},

    {"Diablo IV/39517",                     "5ed287e1ad3b5f1a08f3551eaecc66fb", "e646133acbc9c664ab5b1947578250c4", "Diablo IV.exe"},
    {"Diablo IV/39812-open-beta",           "8f50184d3c5aa5050996c4ebb61cb7ad", "5711f0b48ac785fc81590574527482cc", "Diablo IV.exe"},

    {"Heroes of the Storm/29049",           "98396c1a521e5dee511d835b9e8086c7", "b37e7edc07d465a8e97b47cabcd3fc04", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_autoai1.dds"},
    {"Heroes of the Storm/30027",           "6bcbe7c889cc465e4993f92d6ae1ee75", "978f6332a2f2149d74d48414b834c8f6", "mods\\core.stormmod\\base.stormassets\\assets\\textures\\aicommand_claim1.dds"},
    {"Heroes of the Storm/30414",           "4b377fa69dab736b2ae495920663832e", "367eef337676c902bf6855f54bbda182", "mods\\heromods\\murky.stormmod\\base.stormdata\\gamedata\\buttondata.xml"},
    {"Heroes of the Storm/31726",           "f997a06b3f8c10d9095e542f1ef83a74", "0eb064b28fc6203a48321a15d17f7df8", "mods\\heroes.stormmod\\base.stormassets\\Assets\\modeltextures.db"},
    {"Heroes of the Storm/39445",           "c672b26f8f14ab2e68a9f9d7d6ca6062", "62376a66045c7806e865ef4b056c7060", "versions.osxarchive\\Versions\\Base39153\\Heroes.app\\Contents\\_CodeSignature\\CodeResources"},
    {"Heroes of the Storm/50286",           "d1d57e83cbd72cbecd76916c22f6c4b6", "c1fe97f5fc04a2824449b6c43cf31ce5", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"Heroes of the Storm/65943",           "c5d75f4e12dbc05d4560fe61c4b88773", "f046b2ed9ecc7b27d2a114e16c34c8fd", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"Heroes of the Storm/75589",           "ae2209f1fcb26c730e9757a42bcce17e", "a7f7fbf1e04c87ead423fb567cd6fa5c", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"Heroes of the Storm/81376",           "25597a3f8adc3fa79df243197fecd1cc", "2c36eb3dde7d545a0fa413ccebf84202", "mods\\gameplaymods\\percentscaling.stormmod\\base.stormdata\\GameData\\EffectData.xml"},
    {"Heroes of the Storm/88936",           "e3a4794fcb627f0768ff97834119d20a", "7d2cec9779e9c8baf0f1304df5921858"},

    {"Overwatch/024919/data/casc",          "379e5c9082c2a6bec78c71e5a77d46c9", "78cb77bf93d4563643b01f00831b3321", "ROOT"},
    {"Overwatch/047161",                    "816d8adb3da20493e79f64559843f2d6", "2e935d3f8cc4cf62b94c1ba90214f7c8", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},
    {"Overwatch/072127",                    "a239ec86b6e94c512d37c4836d47bfe1", "26df45c35df2a9530f1912b5a88b2794", "TactManifest\\Win_SPWin_RCN_LesMX_EExt.apm"},
    {"Overwatch/115380",                    "b4b03294e70ed0ef01235a312b46879d", "f8225c828c66e0c839dcf81e31827bdc", "ContentManifestFiles\\Windows-RDEV\\enUS\\speech\\05d0000000000093"},

    {"Starcraft/2457",                      "3eabb81825735cf66c0fc10990f423fa", "ce752a323819c369fba03401ba400332", "music\\radiofreezerg.ogg"},
    {"Starcraft/4037",                      "bb2b76d657a841953fe093b75c2bdaf6", "2f1e9df40da0f6f682ffecbbd920d4fc", "music\\radiofreezerg.ogg"},
    {"Starcraft/4261",                      "59ea96addacccb73938fdf688d7aa29b", "4e07a768999c7887c8c21364961ab07a", "music\\radiofreezerg.ogg"},
    {"Starcraft/6434",                      "e3f929b881ad07028578d202f97c107e", "9bf9597b1f10d32944194334e8dc442a", "music\\radiofreezerg.ogg"},
    {"Starcraft/8713",                      "57da9e2768368d3e31473a70a9286a69", "6a425e9d9e7f3b44773a021ea89f85e3", "music\\radiofreezerg.ogg"},

    {"Starcraft II/45364/\\/",              "28f8b15b5bbd87c16796246eac3f800c", "f9cd7fc20fa53701846109d3d6947d08", NULL},
    {"Starcraft II/75025",                  "79c044e1286b7b18478556e571901294", "e290febb90e06e97b4db6f0eb519ca91", NULL},
    {"Starcraft II/81102",                  "cb6bea299820895f6dcbc72067553743", "63b47f03b1717ded751e0d24d3ddff4f", NULL},
    {"Starcraft II/89720",                  "67d69f09ea73431b22165443c416c1dc", "8a494ff3baaa9a410950f9361a7fb044", "mods\\novastoryassets.sc2mod\\base2.sc2maps\\maps\\campaign\\nova\\nova04.sc2map\\base.sc2data\\GameData\\ActorData.xml"},

    {"Warcraft III/09655",                  "a6b6a6fc519cd1071df65caa56ee7c33", "e45792b7459dc0c78ecb25130fa34d88", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"Warcraft III/11889",                  "5c6bd442c248bad0db50303e0505f147", "4cba488e57f7dccfb77eca8c86578a37", "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j" },
    {"Warcraft III/13369",                  "3212bcad20f7c6ad0eb0864ca9444bb6", "4ac831db9bf0734f01b9d20455a68ab6", "ENCODING" },
    {"Warcraft III/14883",                  "773180e32ac2fac8bd4cd4dfc2ab30a6", "3fd108674117ad4f93885bdd1a525f30", NULL },
    {"Warcraft III/15801",                  "ad571ee968f77bbddc811fd215ee1d37", "f162cd3448219fd9956f9ff8fb5ba915", NULL },
*/
    {"WoW/18125",                           "b31531af094f78f58592249c4d216a8e", "e5c9b3f0da7806d8b239c13bff1d836e", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/18379",                           "fab30626cf94ed1523519729c3701812", "606e4bfd6f8100ae875eb4c00789233b", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/18865",                           "7f252a8c6001938f601b0c91abbb0f2a", "cee96fa43cddc008f564b4615fdbd109", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/18888",                           "a007d0433c71ddc6e9acaa45cbdc4e61", "a093c596240a6b71de125eaa83ea8568", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/19116",                           "a3be9cfd4a15ba184e21eed9ec90417b", "11a973871aef6ab3236676a25381a1e6", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/19342",                           "66f0de0cff477e1d8e982683771f1ada", "69b4c91c977b875fd0a6ffbf89b06408", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/21742",                           "a357c3cbed98e83ac5cd394ceabc01e8", "90ce1aac44299aa2ac6fb44d249d2561", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/22267",                           "101949dfbed06d417d24a65054e8a6b6", "4ef8df3cf9b00b5c7b2c1b9f4166ec0d", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/23420",                           "e62a798989e6db00044b079e74faa1eb", "854e58816e6eb2795d14fe81470ad19e", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/29981",                           "e939d6bfb739eda7049c7ef74efccf60", "3cba30b5e439a6e59b0953d17da9ac6c", "dbfilesclient\\battlepetspeciesstate.db2"},

    {"WoW/31299*wow",                       "d8c1557632959c8eac6c8b4699e40077", "05627c131969bd9394fb345f4037e249", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/31299*wowt",                      "29a147d37b904ace4683ae5aaac7efc3", "423c1b99b14a615a02d8ffc7a7eff4ef", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/31299*wow_classic",               "54f2491ce97f1eae0a57d02aa16f0e43", "b46bd2f81ead285e810e5a049ca2db74", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},

    {"WoW/37497-classic",                   "f7bf0942184f4ff727b2f539fa6ceff0", "102511c7ef53af35b5c3d4a837f076b9"},
    {"WoW/38598-classic-tbcbeta",           "777021f77c2ad16349d9b68484df40e2", "97423f756948e9437defc32b3bec4895"},
    {"WoW/38707-classic-tbc",               "70d621467a36bd768cdbaf0527ef1863", "08cc37803156adaddca74a09211106bd"},
    {"WoW/40892-classic-tbc/.build.info",   "5e9dc8fdb7ffba6c22bcad2330618d1e", "5ae578d0f8986c709477106fa63d8cfb"},
    {"WoW/49821-classic",                   "e54423b6514443aa8d4c326d01db9f87", "0f72450e6f4c7c4dcd06c121bc334d9b", "Sound\\Music\\GlueScreenMusic\\wow_main_theme.mp3"},

    {"WoW/47067*wow",                       "67124947fdd8ea3d1a07850ac40215c0", "70a641d4100b97d77b9b6b3e5a07ee13", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/47067*wowt",                      "0bb892cb57a215033e43ac767663f478", "ef347dfc36cbbef09587384cb9095839", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/47067*wow_classic",               "92e498a7ed7fb6e6cc52b8292dd0864b", "3aae26808a5255477ab49df20b95fb18", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/47067*wow_classic_ptr",           "7cc806b043c18c59dd024ec13bf5c2f1", "cd68e3fd59c97c4b69265bd949b77959", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/47067*wow_classic_era",           "10c8c72c16c55ee44c5554aabe4284da", "47071bdea7e593e5481e2775c4813626", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/47067*wow_classic_era_ptr",       "13792a23d629af232febfe9dc00a6958", "a0736b9aa5dfcd68dcc1fd2b3247ed1d", "Sound\\music\\Draenor\\MUS_60_FelWasteland_A.mp3"},
    {"WoW/51187*wowt",                      "84f02e337fe85806180d935252153f2b", "e10061cb5cafbaf16926f898929a352a"},
};

static STORAGE_INFO StorageInfo2[] =
{
    {"Hearthstone/160183/hearthstone-25.0.3.160183.159202.versions*hsb*us", "34b821747a7911eb98c9141153470fdd", "85096ab761616e1069a4fa5c1da28d9d"},
    {"Hearthstone/160183*hsb*us",                                           "34b821747a7911eb98c9141153470fdd", "85096ab761616e1069a4fa5c1da28d9d"},
    {"WoW/45745-custom-cdn*http://us.falloflordaeron.com:8000*wow*us",      "dd04f7077f57464dc59f39a468daecc3", "e5c03ef3dd196ec24054dbc370fb2fc5", "interface/framexml/localization.lua"},
    {"WoW/45745-meta/wow-45745-custom-cdn.versions*wow*us",                 "f3df28f29b36d50e8e3f68dbb599e035", "e5c03ef3dd196ec24054dbc370fb2fc5", "interface/framexml/localization.lua"},
    {"WoW/45745-meta/wow-45779-tvfs.versions",                              "6fdd73187625ded008be03573d686e52", "dab6a1f3586a2a1408a8152ad7f9f1ad"},
    {"WoW/45745-meta/wow-46144-tvfs.versions",                              "3ce71e0d7ae10c8913be0c226b4684d1", "24d04d1f9f516979a51102f479027c70"},
    {"WoW/45745-meta/wow-46902-classic.versions*wow_classic*us",            "92e498a7ed7fb6e6cc52b8292dd0864b", "3aae26808a5255477ab49df20b95fb18"},
    {"WoW/45745-meta/wow-47186-ptr.versions",                               "7fe5398e286400a47d5f3162998a013a", "e1fff62f0147dd079a0cf73890255863", "interface/framexml/localization.lua"},
    {"WoW/45745-meta*wowt*us",                                              "7fe5398e286400a47d5f3162998a013a", "e1fff62f0147dd079a0cf73890255863", "interface/framexml/localization.lua"},
    {"WoW/5####-current*wow*us",                                            NULL,                               NULL,                               "interface/framexml/localization.lua"},
};

//-----------------------------------------------------------------------------
// Main

static bool WINAPI OnlineStorage_OpenCB_Simple(
    void * /* PtrUserParam */,                  // User-specific parameter passed to the callback
    LPCSTR szWork,                              // Text for the current activity (example: "Loading "ENCODING" file")
    LPCSTR szObject,                            // (optional) name of the object tied to the activity (example: index file name)
    DWORD dwValue,                              // (optional) current object being processed
    DWORD dwTotal)                              // (optional) If non-zero, this is the total number of objects to process
{
    LPCSTR szFmtx = (dwTotal != 0) ? ("%s (%u of %u)    \r") : ("%s    \r");
    char szFormat[256];

    CascStrPrintf(szFormat, _countof(szFormat), szFmtx, szWork, dwValue, dwTotal);
    printf(szFormat, szObject);
    return false;
}


#define LOAD_STORAGES_SINGLE_DEV
#define LOAD_STORAGES_CMD_LINE
#define LOAD_STORAGES_LOCAL
//#define LOAD_STORAGES_ONLINE

int main(int argc, char * argv[])
{
    DWORD dwErrCode = ERROR_SUCCESS;

    CASCLIB_UNUSED(argc);
    CASCLIB_UNUSED(argv);
    printf("\n");

    //printf("%llx\n", CalcFileNameHash("interface/icons/inv_helm_armor_explorer_d_01.blp"));
    //printf("%llx\n", CalcNormNameHash("interface\\icons\\inv_helm_armor_explorer_d_01.blp", 48));

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif  // defined(_MSC_VER) && defined(_DEBUG)

#ifdef LOAD_STORAGES_SINGLE_DEV
    {
        CASC_OPEN_STORAGE_ARGS OpenArgs = {sizeof(CASC_OPEN_STORAGE_ARGS)};
        CASC_FIND_DATA cf;
        HANDLE hStorage;
        HANDLE hFind;
        HANDLE hFile;
        LPCSTR szFile = "interface/icons/inv_armor_explorer_d_01_helm.blp";     // FileDataId = 2965132
        //LPCSTR szFile = "interface/icons/inv_helm_armor_explorer_d_01.blp";     // FileDataId = 2965132
        BYTE Buffer[0x100];

        OpenArgs.PfnProgressCallback = OnlineStorage_OpenCB_Simple;
        OpenArgs.PtrProgressParam = NULL;

        if(CascOpenStorageEx(_T("E:\\Multimedia\\CASC\\Work\\bna*bna*us"), &OpenArgs, true, &hStorage))
        {
            //hFind = CascFindFirstFile(hStorage, szFile, &cf, szListFile_TXT);
            //if(hFind != INVALID_HANDLE_VALUE)
            {
                if(CascOpenFile(hStorage, szFile, 0, CASC_OVERCOME_ENCRYPTED | CASC_OPEN_CKEY_ONCE, &hFile))
                {
                    CascReadFile(hFile, Buffer, sizeof(Buffer), NULL);
                    CascCloseFile(hFile);
                }
                //CascFindClose(hFind);
            }
            CascCloseStorage(hStorage);
        }
    }
#endif

#ifdef LOAD_STORAGES_CMD_LINE
    //
    // Run tests for each storage entered on command line
    //
    for(int i = 1; i < argc; i++)
    {
        STORAGE_INFO StorInfo = {argv[i]};

        // Attempt to open the storage and extract single file
        dwErrCode = LocalStorage_Test(Storage_ReadFiles, StorInfo);
        if(dwErrCode != ERROR_SUCCESS && dwErrCode != ERROR_FILE_NOT_FOUND)
            break;
    }
#endif

#ifdef LOAD_STORAGES_LOCAL
    //
    // Run the tests for every local storage in my collection
    //
    for(size_t i = 0; i < _countof(StorageInfo1); i++)
    {
        // Attempt to open the storage and extract single file
        dwErrCode = LocalStorage_Test(Storage_ReadFiles, StorageInfo1[i]);
        if(dwErrCode != ERROR_SUCCESS && dwErrCode != ERROR_FILE_NOT_FOUND)
            break;
    }
#endif

#ifdef LOAD_STORAGES_ONLINE
    //
    // Run the tests for every available online storage in my collection
    //
    for(size_t i = 0; i < _countof(StorageInfo2); i++)
    {
        // Attempt to open the storage and extract single file
        dwErrCode = OnlineStorage_Test(Storage_EnumFiles, StorageInfo2[i]);
        if(dwErrCode != ERROR_SUCCESS)
            break;
    }
#endif

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtDumpMemoryLeaks();
#endif  // defined(_MSC_VER) && defined(_DEBUG)

    return (int)dwErrCode;
}
