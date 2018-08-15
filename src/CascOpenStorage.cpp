/*****************************************************************************/
/* CascOpenStorage.cpp                    Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Storage functions for CASC                                                */
/* Note: WoW6 offsets refer to WoW.exe 6.0.3.19116 (32-bit)                  */
/* SHA1: c10e9ffb7d040a37a356b96042657e1a0c95c0dd                            */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascOpenStorage.cpp             */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local structures

// Structure describing the 32-bit block size and 32-bit Jenkins hash of the block
typedef struct _BLOCK_SIZE_AND_HASH
{
    DWORD cbBlockSize;
    DWORD dwBlockHash;

} BLOCK_SIZE_AND_HASH, *PBLOCK_SIZE_AND_HASH;

// The ENCODING file is in the form of:
// * File header. Fixed length.
// * Encoding Specification (ESpec) in the string form. Length is stored in FILE_ENCODING_HEADER::ESpecBlockSize
// https://wowdev.wiki/CASC#Encoding
typedef struct _FILE_ENCODING_HEADER
{
    BYTE Magic[2];                                  // "EN"
    BYTE Version;                                   // Expected to be 1 by CascLib
    BYTE CKeyLength;                                // The content key length in ENCODING file. Usually 0x10
    BYTE EKeyLength;                                // The encoded key length in ENCODING file. Usually 0x10
    BYTE CKeyPageSize[2];                           // Size of the CKey page, in KB (big-endian)
    BYTE EKeyPageSize[2];                           // Size of the EKey page, in KB (big-endian)
    BYTE CKeyPageCount[4];                          // Number of CKey pages in the table (big endian)
    BYTE EKeyPageCount[4];                          // Number of EKey pages in the table (big endian)
    BYTE field_11;                                  // Asserted to be zero by the agent
    BYTE ESpecBlockSize[4];                         // Size of the ESpec string block

} FILE_ENCODING_HEADER, *PFILE_ENCODING_HEADER;

typedef struct _FILE_PAGE_HEADER
{
    BYTE FirstKey[MD5_HASH_SIZE];                   // The first CKey/EKey in the segment
    BYTE SegmentHash[MD5_HASH_SIZE];                // MD5 hash of the entire segment

} FILE_PAGE_HEADER, *PFILE_PAGE_HEADER;

// The EKey entry from the ".idx" files (1st block)
typedef struct _FILE_EKEY_ENTRY_V2
{
    BYTE EKey[CASC_EKEY_SIZE];                      // The first 9 bytes of the encoded key
    BYTE FileOffsetBE[5];                           // Index of data file and offset within (big endian).
    BYTE EncodedSize[4];                            // Encoded size (big endian). This is the size of encoded header, all file frame headers and all file frames
} FILE_EKEY_ENTRY_V2, *PFILE_EKEY_ENTRY_V2;

// The index entry from the ".idx" files (2nd block)
typedef struct _FILE_EKEY_ENTRY_V3
{
    DWORD DataHash;                                 // Jenkins hash of (EKey+FileOffsetBE+FileSizeLE)

    BYTE EKey[CASC_EKEY_SIZE];                      // The first 9 bytes of the encoded key
    BYTE StorageOffset[5];                          // Index of data file and offset within (big endian).
    BYTE EncodedSize[4];                            // Encoded size (big endian). This is the size of encoded header, all file frame headers and all file frames
    BYTE Unknown[2];
} FILE_EKEY_ENTRY_V3, *PFILE_EKEY_ENTRY_V3;

typedef struct _FILE_INDEX_HEADER_V1
{
    USHORT field_0;
    BYTE  KeyIndex;                                 // Key index (0 for data.i0x, 1 for data.i1x, 2 for data.i2x etc.)
    BYTE  align_3;
    DWORD field_4;
    ULONGLONG field_8;
    ULONGLONG SegmentSize;                          // Size of one data segment (aka data.### file)
    BYTE  SpanSizeBytes;
    BYTE  SpanOffsBytes;
    BYTE  KeyBytes;
    BYTE  FileOffsetBits;                           // Number of bits for file offset
    DWORD KeyCount1;
    DWORD KeyCount2;
    DWORD KeysHash1;
    DWORD KeysHash2;
    DWORD dwHeaderHash;
} FILE_INDEX_HEADER_V1, *PFILE_INDEX_HEADER_V1;

typedef struct _FILE_INDEX_HEADER_V2
{
    BLOCK_SIZE_AND_HASH BlockHeader;                // Length and hash of the data following BlockHeader
    USHORT IndexVersion;                            // Must be 0x07
    BYTE   BucketIndex;                             // The bucket index of this file; should be the same as the first byte of the hex filename. 
    BYTE   ExtraBytes;                              // Unknown; must be 0
    BYTE   SpanSizeBytes;                           // Size of field with file size
    BYTE   SpanOffsBytes;                           // Size of field with file offset
    BYTE   KeyBytes;                                // Size of the file key (bytes)
    BYTE   FileOffsetBits;                          // Number of bits for the file offset (rest is archive index)
    ULONGLONG SegmentSize;                          // Size of one data segment (aka data.### file)
    BYTE   Padding[8];                              // Always here

} FILE_INDEX_HEADER_V2, *PFILE_INDEX_HEADER_V2;

typedef struct _FILE_EKEY_ENTRIES1
{
    BLOCK_SIZE_AND_HASH BlockHeader;                // Length and hash of the data following BlockHeader
    FILE_EKEY_ENTRY_V2 EKeyEntry[1];                // Variable number of EKey entries
} FILE_EKEY_ENTRIES1, *PFILE_EKEY_ENTRIES1;

typedef struct _FILE_ESPEC_ENTRY
{
    BYTE ESpecKey[MD5_HASH_SIZE];                   // The ESpec key of the file
    BYTE ESpecIndexBE[4];                           // Index of ESPEC entry, assuming zero-terminated strings (big endian)
    BYTE FileSizeBE[5];                             // Size of the encoded version of the file (big endian)

} FILE_ESPEC_ENTRY, *PFILE_ESPEC_ENTRY;

#define FILE_INDEX_HASH_LENGTH  (CASC_EKEY_SIZE + 5 + 4 + 1)
#define FILE_INDEX_BLOCK_SIZE    0x200

//-----------------------------------------------------------------------------
// Local variables

static const TCHAR * szAllowedHexChars = _T("0123456789aAbBcCdDeEfF");
static const TCHAR * szIndexFormat_V1 = _T("data.i%x%x");
static const TCHAR * szIndexFormat_V2 = _T("%02x%08x.idx");

//-----------------------------------------------------------------------------
// Local functions
/*
static void InitQuerySize(QUERY_SIZE & QuerySize)
{
    QuerySize.ContentSize = CASC_INVALID_SIZE;
    QuerySize.EncodedSize = CASC_INVALID_SIZE;
}
*/
TCascStorage * IsValidCascStorageHandle(HANDLE hStorage)
{
    TCascStorage * hs = (TCascStorage *)hStorage;

    return (hs != NULL && hs->szClassName != NULL && !strcmp(hs->szClassName, "TCascStorage")) ? hs : NULL;
}

// "data.iXY"
static bool IsIndexFileName_V1(const TCHAR * szFileName)
{
    // Check if the name looks like a valid index file
    return (_tcslen(szFileName) == 8 &&
            _tcsnicmp(szFileName, _T("data.i"), 6) == 0 &&
            _tcsspn(szFileName + 6, szAllowedHexChars) == 2);
}

static bool IsIndexFileName_V2(const TCHAR * szFileName)
{
    // Check if the name looks like a valid index file
    return (_tcslen(szFileName) == 14 &&
            _tcsspn(szFileName, _T("0123456789aAbBcCdDeEfF")) == 0x0A &&
            _tcsicmp(szFileName + 0x0A, _T(".idx")) == 0);
}

static bool IsCascIndexHeader_V1(LPBYTE pbFileData, DWORD cbFileData)
{
    PFILE_INDEX_HEADER_V1 pIndexHeader = (PFILE_INDEX_HEADER_V1)pbFileData;
    DWORD dwHeaderHash;
    bool bResult = false;

    // Check the size
    if(cbFileData >= sizeof(FILE_INDEX_HEADER_V1))
    {
        // Save the header hash
        dwHeaderHash = pIndexHeader->dwHeaderHash;
        pIndexHeader->dwHeaderHash = 0;

        // Calculate the hash
        if(hashlittle(pIndexHeader, sizeof(FILE_INDEX_HEADER_V1), 0) == dwHeaderHash)
            bResult = true;

        // Put the hash back
        pIndexHeader->dwHeaderHash = dwHeaderHash;
    }

    return bResult;
}

static bool IsCascIndexHeader_V2(LPBYTE pbFileData, DWORD cbFileData)
{
    PBLOCK_SIZE_AND_HASH pSizeAndHash = (PBLOCK_SIZE_AND_HASH)pbFileData;
    unsigned int HashHigh = 0;
    unsigned int HashLow = 0;

    // Check for the header
    if(cbFileData < sizeof(BLOCK_SIZE_AND_HASH) || pSizeAndHash->cbBlockSize < 0x10)
        return false;
    if(cbFileData < pSizeAndHash->cbBlockSize + sizeof(BLOCK_SIZE_AND_HASH))
        return false;

    // The index header for CASC v 2.0 begins with length and checksum
    hashlittle2(pSizeAndHash + 1, pSizeAndHash->cbBlockSize, &HashHigh, &HashLow);
    return (HashHigh == pSizeAndHash->dwBlockHash);
}

static bool CheckAvailableDataSize(LPBYTE pbDataPtr, LPBYTE pbDataEnd)
{
    PBLOCK_SIZE_AND_HASH pBlockHeader;

    // Capture and check the block header
    if((pbDataPtr + sizeof(BLOCK_SIZE_AND_HASH)) > pbDataEnd)
        return false;
    pBlockHeader = (PBLOCK_SIZE_AND_HASH)pbDataPtr;

    // Capture and check the data
    if((pbDataPtr + sizeof(BLOCK_SIZE_AND_HASH) + pBlockHeader->cbBlockSize) > pbDataEnd)
        return false;

    return true;
}

static bool CutLastPathPart(TCHAR * szWorkPath)
{
    size_t nLength = _tcslen(szWorkPath);

    // Go one character back
    if(nLength > 0)
        nLength--;

    // Cut ending (back)slashes, if any
    while(nLength > 0 && (szWorkPath[nLength] == _T('\\') || szWorkPath[nLength] == _T('/')))
        nLength--;

    // Cut the last path part
    while(nLength > 0)
    {
        // End of path?
        if(szWorkPath[nLength] == _T('\\') || szWorkPath[nLength] == _T('/'))
        {
            szWorkPath[nLength] = 0;
            return true;
        }

        // Go one character back
        nLength--;
    }

    return false;
}

static int InsertNamedInternalFile(
    TCascStorage * hs,
    const char * szFileName,
    CASC_CKEY_ENTRY & CKeyEntry)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    QUERY_KEY CKey;

    // Find the existing CKey entry in the CKey map. Should succeed for all files, except ENCODING
    CKey.pbData = CKeyEntry.CKey;
    CKey.cbData = CASC_CKEY_SIZE;
    pCKeyEntry = FindCKeyEntry(hs, &CKey);
    if(pCKeyEntry == NULL)
    {
        // Check the input structure for valid CKey and EKey count
        if(!IsValidMD5(CKeyEntry.CKey))
            return ERROR_FILE_NOT_FOUND;
        if(CKeyEntry.EKeyCount == 0)
            return ERROR_CAN_NOT_COMPLETE;
        pCKeyEntry = &CKeyEntry;
    }

    // The content size and EKey should be known
    assert(ConvertBytesToInteger_4(pCKeyEntry->ContentSize) != CASC_INVALID_SIZE);
    assert(pCKeyEntry->EKeyCount != 0);

    // Now call the root handler to insert the CKey entry
    return hs->pRootHandler->Insert(szFileName, pCKeyEntry);
}

static TCHAR * CheckForIndexDirectory(TCascStorage * hs, const TCHAR * szSubDir)
{
    TCHAR * szIndexPath;

    // Combine the index path
    szIndexPath = CombinePath(hs->szDataPath, szSubDir);
    if (!DirectoryExists(szIndexPath))
    {
        CASC_FREE(szIndexPath);
        szIndexPath = NULL;
    }

    return szIndexPath;
}

static int InitializeCascDirectories(TCascStorage * hs, const TCHAR * szPath)
{
    TCHAR * szWorkPath;
    int nError = ERROR_NOT_ENOUGH_MEMORY;

    // Find the root directory of the storage. The root directory
    // is the one with ".build.info" or ".build.db".
    szWorkPath = CascNewStr(szPath, 0);
    if(szWorkPath != NULL)
    {
        // Get the length and go up until we find the ".build.info" or ".build.db"
        for(;;)
        {
            // Is this a game directory?
            nError = CheckGameDirectory(hs, szWorkPath);
            if(nError == ERROR_SUCCESS)
            {
                nError = ERROR_SUCCESS;
                break;
            }

            // Cut one path part
            if(!CutLastPathPart(szWorkPath))
            {
                nError = ERROR_FILE_NOT_FOUND;
                break;
            }
        }

        // Free the work path buffer
        CASC_FREE(szWorkPath);
    }

    // Find the index directory
    if (nError == ERROR_SUCCESS)
    {
        // First, check for more common "data" subdirectory
        if ((hs->szIndexPath = CheckForIndexDirectory(hs, _T("data"))) != NULL)
            return ERROR_SUCCESS;

        // Second, try the "darch" subdirectory (older builds of HOTS - Alpha)
        if ((hs->szIndexPath = CheckForIndexDirectory(hs, _T("darch"))) != NULL)
            return ERROR_SUCCESS;

        nError = ERROR_FILE_NOT_FOUND;
    }

    return nError;
}

static bool IndexDirectory_OnFileFound(
    const TCHAR * szFileName,
    PDWORD IndexArray,
    PDWORD OldIndexArray,
    void * pvContext)
{
    TCascStorage * hs = (TCascStorage *)pvContext;
    DWORD IndexValue = 0;
    DWORD IndexVersion = 0;

    // Auto-detect the format of the index file name
    if(hs->szIndexFormat == NULL)
    {
        if(IsIndexFileName_V2(szFileName))
            hs->szIndexFormat = szIndexFormat_V2;
        else if(IsIndexFileName_V1(szFileName))
            hs->szIndexFormat = szIndexFormat_V1;
        else
            return false;
    }

    if(hs->szIndexFormat == szIndexFormat_V2)
    {
        // Check the index file name format
        if(!IsIndexFileName_V2(szFileName))
            return false;

        // Get the main index from the first two digits
        if(ConvertStringToInt32(szFileName, 2, &IndexValue) != ERROR_SUCCESS)
            return false;
        if(ConvertStringToInt32(szFileName + 2, 8, &IndexVersion) != ERROR_SUCCESS)
            return false;
    }
    else if(hs->szIndexFormat == szIndexFormat_V1)
    {
        // Check the index file name format
        if(!IsIndexFileName_V1(szFileName))
            return false;

        // Get the main index from the first two digits
        if(ConvertDigitToInt32(szFileName + 6, &IndexValue) != ERROR_SUCCESS)
            return false;
        if(ConvertDigitToInt32(szFileName + 7, &IndexVersion) != ERROR_SUCCESS)
            return false;
    }
    else
    {
        // Should never happen
        assert(false);
        return false;
    }

    // The index value must not be greater than 0x0F
    if(IndexValue >= CASC_INDEX_COUNT)
        return false;

    // If the new subindex is greater than the previous one,
    // use this one instead
    if(IndexVersion > IndexArray[IndexValue])
    {
        OldIndexArray[IndexValue] = IndexArray[IndexValue];
        IndexArray[IndexValue] = IndexVersion;
    }
    else if(IndexVersion > OldIndexArray[IndexValue])
    {
        OldIndexArray[IndexValue] = IndexVersion;
    }

    // Note: WoW6 only keeps last two index files
    // Any additional index files are deleted at this point
    return true;
}

static TCHAR * CreateIndexFileName(TCascStorage * hs, DWORD IndexValue, DWORD IndexVersion)
{
    TCHAR szPlainName[0x40];

    // Sanity checks
    assert(hs->szIndexFormat != NULL);
    assert(hs->szIndexPath != NULL);
    assert(IndexValue <= 0x0F);

    // Create the full path
    _stprintf(szPlainName, hs->szIndexFormat, IndexValue, IndexVersion);
    return CombinePath(hs->szIndexPath, szPlainName);
}

static int VerifyAndLoadIndexFile_V1(PCASC_INDEX_FILE pIndexFile, DWORD KeyIndex)
{
    PFILE_INDEX_HEADER_V1 pIndexHeader = (PFILE_INDEX_HEADER_V1)pIndexFile->pbFileData;
    DWORD dwDataHash1;
    DWORD dwDataHash2;

    // Verify the format
    if(pIndexHeader->field_0 != 0x0005)
        return ERROR_NOT_SUPPORTED;
    if(pIndexHeader->KeyIndex != KeyIndex)
        return ERROR_NOT_SUPPORTED;
    if(pIndexHeader->field_8 == 0)
        return ERROR_NOT_SUPPORTED;

    // Verify the byte sizes
    if(pIndexHeader->SpanSizeBytes != 0x04 ||
       pIndexHeader->SpanOffsBytes != 0x05 ||
       pIndexHeader->KeyBytes != 0x09)
        return ERROR_NOT_SUPPORTED;

    pIndexFile->ExtraBytes     = 0;
    pIndexFile->SpanSizeBytes  = pIndexHeader->SpanSizeBytes;
    pIndexFile->SpanOffsBytes  = pIndexHeader->SpanOffsBytes;
    pIndexFile->KeyBytes       = pIndexHeader->KeyBytes;
    pIndexFile->FileOffsetBits = pIndexHeader->FileOffsetBits;
    pIndexFile->SegmentSize    = pIndexHeader->SegmentSize;

    // Get the pointer to the key entry array
    pIndexFile->nEKeyEntries = pIndexHeader->KeyCount1 + pIndexHeader->KeyCount2;
    if(pIndexFile->nEKeyEntries != 0)
        pIndexFile->pEKeyEntries = (PCASC_EKEY_ENTRY)(pIndexFile->pbFileData + sizeof(FILE_INDEX_HEADER_V1));

    // Verify hashes
    dwDataHash1 = hashlittle(pIndexFile->pEKeyEntries, pIndexHeader->KeyCount1 * sizeof(CASC_EKEY_ENTRY), 0);
    dwDataHash2 = hashlittle(pIndexFile->pEKeyEntries + pIndexHeader->KeyCount1, pIndexHeader->KeyCount2 * sizeof(CASC_EKEY_ENTRY), 0);
    if(dwDataHash1 != pIndexHeader->KeysHash1 || dwDataHash2 != pIndexHeader->KeysHash2)
        return ERROR_FILE_CORRUPT;

    return ERROR_SUCCESS;
}

static int VerifyAndLoadIndexFile_V2(PCASC_INDEX_FILE pIndexFile, DWORD BucketIndex)
{
    PFILE_INDEX_HEADER_V2 pIndexHeader;
    PFILE_EKEY_ENTRIES1 pEKeyEntries1;
    PBLOCK_SIZE_AND_HASH pSizeAndHash;
    PCASC_EKEY_ENTRY pTargetEntry;
    LPBYTE pbFilePtr = pIndexFile->pbFileData;
    LPBYTE pbFileEnd = pbFilePtr + pIndexFile->cbFileData;
    DWORD EKeyEntriesLength;
    unsigned int HashValue;

    // Verify the the header data length
    pIndexHeader = (PFILE_INDEX_HEADER_V2)pbFilePtr;
    if(!CheckAvailableDataSize(pbFilePtr, pbFileEnd))
        return ERROR_BAD_FORMAT;

    // Verify the header hash
    pSizeAndHash = &pIndexHeader->BlockHeader;
    HashValue = hashlittle((pSizeAndHash + 1), pSizeAndHash->cbBlockSize, 0);
    if(HashValue != pSizeAndHash->dwBlockHash)
        return ERROR_BAD_FORMAT;

    // Verify the content of the index header
    if(pIndexHeader->IndexVersion  != 0x07        ||
       pIndexHeader->BucketIndex   != BucketIndex ||
       pIndexHeader->ExtraBytes    != 0x00        ||
       pIndexHeader->SpanSizeBytes != 0x04        ||
       pIndexHeader->SpanOffsBytes != 0x05        ||
       pIndexHeader->KeyBytes  != CASC_EKEY_SIZE)
        return ERROR_BAD_FORMAT;

    // Capture the values from the index header
    pIndexFile->ExtraBytes     = pIndexHeader->ExtraBytes;
    pIndexFile->SpanSizeBytes  = pIndexHeader->SpanSizeBytes;
    pIndexFile->SpanOffsBytes  = pIndexHeader->SpanOffsBytes;
    pIndexFile->KeyBytes       = pIndexHeader->KeyBytes;
    pIndexFile->FileOffsetBits = pIndexHeader->FileOffsetBits;
    pIndexFile->SegmentSize    = pIndexHeader->SegmentSize;
    pbFilePtr += sizeof(FILE_INDEX_HEADER_V2);

    // Get the pointer to the first block of EKey entries
    pEKeyEntries1 = (PFILE_EKEY_ENTRIES1)pbFilePtr;
    if(CheckAvailableDataSize(pbFilePtr, pbFileEnd))
    {
        PFILE_EKEY_ENTRY_V2 pSourceEntry = pEKeyEntries1->EKeyEntry;
        unsigned int HashHigh = 0;
        unsigned int HashLow = 0;
        DWORD dwIndexEntries;

        // Verify if we have something in the data at all
        if(pEKeyEntries1->BlockHeader.cbBlockSize != 0)
        {
            // Load and verify the CASC index entries
            if(pEKeyEntries1->BlockHeader.cbBlockSize < sizeof(FILE_EKEY_ENTRY_V2))
                return ERROR_BAD_FORMAT;
            dwIndexEntries = (pEKeyEntries1->BlockHeader.cbBlockSize / sizeof(FILE_EKEY_ENTRY_V2));

            // Hash all entries
            for(DWORD i = 0; i < dwIndexEntries; i++)
                hashlittle2(pSourceEntry+i, sizeof(FILE_EKEY_ENTRY_V2), &HashHigh, &HashLow);
            if(HashHigh != pEKeyEntries1->BlockHeader.dwBlockHash)
                return ERROR_BAD_FORMAT;

            // FILE_EKEY_ENTRY_V2 and CASC_EKEY_ENTRY are equal structures,
            // so we can assign one to another without memory copy
            pIndexFile->pEKeyEntries = (PCASC_EKEY_ENTRY)pEKeyEntries1->EKeyEntry;
            pIndexFile->nEKeyEntries = dwIndexEntries;
            return ERROR_SUCCESS;
        }
    }

    // Get the pointer to the second block of EKey entries.
    // They are alway at the position aligned to 4096
    pbFilePtr = pIndexFile->pbFileData + 0x1000;
    EKeyEntriesLength = (DWORD)(pbFileEnd - pbFilePtr);
    if(EKeyEntriesLength >= 0x7800)
    {
        DWORD dwMaxEKeyEntries = EKeyEntriesLength / sizeof(FILE_EKEY_ENTRY_V3);

        // Allocate the array of index entries
        pIndexFile->pEKeyEntries = pTargetEntry = CASC_ALLOC(CASC_EKEY_ENTRY, dwMaxEKeyEntries);
        if(pIndexFile->pEKeyEntries == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Parse the chunks with the EKey entries
        while(pbFilePtr < pbFileEnd)
        {
            PFILE_EKEY_ENTRY_V3 pSourceEntry = (PFILE_EKEY_ENTRY_V3)pbFilePtr;
            DWORD DataHash;

            for(int i = 0; i < (FILE_INDEX_BLOCK_SIZE / sizeof(FILE_EKEY_ENTRY_V3)); i++)
            {
                // Check for end marker
                if(pSourceEntry->DataHash == 0)
                    break;

                // Check the hash of the EKey entry
                DataHash = hashlittle(&pSourceEntry->EKey, sizeof(FILE_EKEY_ENTRY_V2)+1, 0) | 0x80000000;
                if(DataHash != pSourceEntry->DataHash)
                    return ERROR_BAD_FORMAT;

                // Copy the entry
                memcpy(pTargetEntry, pSourceEntry->EKey, sizeof(CASC_EKEY_ENTRY));
                pTargetEntry++;
                pSourceEntry++;
            }

            // Move to the next chunk
            pbFilePtr += FILE_INDEX_BLOCK_SIZE;
        }

        // Calculate the proper number of EKey entries
        pIndexFile->nEKeyEntries = (DWORD)(pTargetEntry - pIndexFile->pEKeyEntries);
        pIndexFile->FreeEKeyEntries = true;
        return ERROR_SUCCESS;
    }

    return ERROR_NOT_SUPPORTED;
}

static int VerifyAndLoadIndexFile(PCASC_INDEX_FILE pIndexFile, DWORD KeyIndex)
{
    // Sanity checks
    assert(pIndexFile->pbFileData != NULL);
    assert(pIndexFile->cbFileData != 0);

    // Check for CASC version 2
    if(IsCascIndexHeader_V2(pIndexFile->pbFileData, pIndexFile->cbFileData))
        return VerifyAndLoadIndexFile_V2(pIndexFile, KeyIndex);

    // Check for CASC version 1
    if(IsCascIndexHeader_V1(pIndexFile->pbFileData, pIndexFile->cbFileData))
        return VerifyAndLoadIndexFile_V1(pIndexFile, KeyIndex);

    // Unknown CASC version
    assert(false);
    return ERROR_BAD_FORMAT;
}

static int LoadIndexFile(PCASC_INDEX_FILE pIndexFile, DWORD KeyIndex)
{
    TFileStream * pStream;
    ULONGLONG FileSize = 0;
    int nError = ERROR_SUCCESS;

    // Sanity checks
    assert(pIndexFile->szFileName != NULL && pIndexFile->szFileName[0] != 0);

    // Open the stream for read-only access and read the file
    pStream = FileStream_OpenFile(pIndexFile->szFileName, STREAM_FLAG_READ_ONLY | STREAM_PROVIDER_FLAT | BASE_PROVIDER_FILE);
    if(pStream != NULL)
    {
        // Retrieve the file size
        FileStream_GetSize(pStream, &FileSize);
        if(0 < FileSize && FileSize <= 0x200000)
        {
            // WoW6 actually reads THE ENTIRE file to memory
            // Verified on Mac build (x64)
            pIndexFile->pbFileData = CASC_ALLOC(BYTE, (DWORD)FileSize);
            pIndexFile->cbFileData = (DWORD)FileSize;

            // Load the data to memory and parse it
            if(pIndexFile->pbFileData != NULL)
            {
                if(FileStream_Read(pStream, NULL, pIndexFile->pbFileData, pIndexFile->cbFileData))
                {
                    nError = VerifyAndLoadIndexFile(pIndexFile, KeyIndex);
                }
            }
            else
                nError = ERROR_NOT_ENOUGH_MEMORY;
        }
        else
        {
            assert(false);
            nError = ERROR_BAD_FORMAT;
        }

        // Close the file stream
        FileStream_Close(pStream);
    }
    else
        nError = GetLastError();

    return nError;
}

static int CreateMapOfEKeyEntries(TCascStorage * hs)
{
    PCASC_MAP pMap;
    DWORD TotalCount = 0;
    int nError = ERROR_NOT_ENOUGH_MEMORY;

    // Count the total number of files in the storage
    for(size_t i = 0; i < CASC_INDEX_COUNT; i++)
        TotalCount += hs->IndexFile[i].nEKeyEntries;

    // Create the map of all index entries
    pMap = Map_Create(TotalCount, CASC_EKEY_SIZE, FIELD_OFFSET(CASC_EKEY_ENTRY, EKey));
    if(pMap != NULL)
    {
        // Put all index entries in the map
        for(size_t i = 0; i < CASC_INDEX_COUNT; i++)
        {
            PCASC_EKEY_ENTRY pEKeyEntry = hs->IndexFile[i].pEKeyEntries;
            DWORD nEKeyEntries = hs->IndexFile[i].nEKeyEntries;

            for(DWORD j = 0; j < nEKeyEntries; j++)
            {
                // Insert the index entry to the map
                // Note that duplicate entries will not be inserted to the map
                //
                // Duplicate entries in WoW-WOD build 18179:
                // 9e dc a7 8f e2 09 ad d8 b7 (encoding file)
                // f3 5e bb fb d1 2b 3f ef 8b
                // c8 69 9f 18 a2 5e df 7e 52
                Map_InsertObject(pMap, pEKeyEntry, pEKeyEntry->EKey);

                // Move to the next entry
                pEKeyEntry++;
            }
        }

        // Store the map to the storage handle
        hs->pEKeyEntryMap = pMap;
        nError = ERROR_SUCCESS;
    }

    return nError;
}

static int CreateMapOfCKeyEntries(TCascStorage * hs, CASC_ENCODING_HEADER & EnHeader, PFILE_PAGE_HEADER pPageHeader)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    DWORD dwMaxEntries;
    int nError = ERROR_SUCCESS;

    // Sanity check
    assert(hs->pCKeyEntryMap == NULL);
    assert(hs->pEKeyEntryMap != NULL);

    // Calculate the largest eventual number of CKey entries
    // Also include space for eventual extra entries for well-known CASC files
    dwMaxEntries = (EnHeader.CKeyPageSize / sizeof(CASC_CKEY_ENTRY)) * EnHeader.CKeyPageCount;

    // Create the map of the CKey entries
    hs->pCKeyEntryMap = Map_Create(dwMaxEntries + 1, CASC_CKEY_SIZE, FIELD_OFFSET(CASC_CKEY_ENTRY, CKey));
    if(hs->pCKeyEntryMap != NULL)
    {
        LPBYTE pbPageEntry = (LPBYTE)(pPageHeader + EnHeader.CKeyPageCount);

        // Parse all CKey pages
        for(DWORD i = 0; i < EnHeader.CKeyPageCount; i++)
        {
            LPBYTE pbCKeyEntry = pbPageEntry;
            LPBYTE pbEndOfPage = pbPageEntry + EnHeader.CKeyPageSize;

            // Parse all encoding entries
            while(pbCKeyEntry <= pbEndOfPage)
            {
                // Get pointer to the encoding entry
                pCKeyEntry = (PCASC_CKEY_ENTRY)pbCKeyEntry;
                if(pCKeyEntry->EKeyCount == 0)
                    break;

                // Insert the pointer the array
                Map_InsertObject(hs->pCKeyEntryMap, pCKeyEntry, pCKeyEntry->CKey);

                // Move to the next encoding entry
                pbCKeyEntry += sizeof(CASC_CKEY_ENTRY) + (pCKeyEntry->EKeyCount - 1) * EnHeader.EKeyLength;
            }

            // Move to the next segment
            pbPageEntry += EnHeader.CKeyPageSize;
        }
/*
        FILE * fp = fopen("E:\\110-ckey-map.txt", "wt");
        for(size_t i = 0; i < hs->pCKeyEntryMap->TableSize; i++)
        {
            pCKeyEntry = (PCASC_CKEY_ENTRY)(hs->pCKeyEntryMap->HashTable[i]);
            if(pCKeyEntry != NULL)
            {
                char szCKey[0x40];
                StringFromBinary(pCKeyEntry->CKey, MD5_HASH_SIZE, szCKey);
                fprintf(fp, "%s\n", szCKey);
            }
        }
        fclose(fp);
*/
        // Insert extra entry for ENCODING file. We need to do that artificially,
        // because CKey of ENCODING file is not in ENCODING itself :-)
        Map_InsertObject(hs->pCKeyEntryMap, &hs->EncodingFile, hs->EncodingFile.CKey);
    }
    else
        nError = ERROR_NOT_ENOUGH_MEMORY;

    return nError;
}

static int LoadIndexFiles(TCascStorage * hs)
{
    DWORD IndexArray[CASC_INDEX_COUNT];
    DWORD OldIndexArray[CASC_INDEX_COUNT];
    int nError;
    int i;

    // Scan all index files and load contained EKEY entries
    memset(IndexArray, 0, sizeof(IndexArray));
    memset(OldIndexArray, 0, sizeof(OldIndexArray));
    nError = ScanIndexDirectory(hs->szIndexPath, IndexDirectory_OnFileFound, IndexArray, OldIndexArray, hs);
    if(nError == ERROR_SUCCESS)
    {
        // Load each index file
        for(i = 0; i < CASC_INDEX_COUNT; i++)
        {
            hs->IndexFile[i].szFileName = CreateIndexFileName(hs, i, IndexArray[i]);
            if(hs->IndexFile[i].szFileName != NULL)
            {
                nError = LoadIndexFile(&hs->IndexFile[i], i);
                if(nError != ERROR_SUCCESS)
                    break;
            }
        }
    }

    // Now we need to build the map of the EKey entries (EKey -> CASC_EKEY_ENTRY)
    if(nError == ERROR_SUCCESS)
    {
        nError = CreateMapOfEKeyEntries(hs);
    }

    return nError;
}

static int CaptureEncodingHeader(PCASC_ENCODING_HEADER pEncodingHeader, void * pvFileHeader, size_t cbFileHeader)
{
    PFILE_ENCODING_HEADER pFileHeader = (PFILE_ENCODING_HEADER)pvFileHeader;

    // Check the signature and version
    if(cbFileHeader < sizeof(FILE_ENCODING_HEADER) || pFileHeader->Magic[0] != 'E' || pFileHeader->Magic[1] != 'N' || pFileHeader->Version != 0x01)
        return ERROR_BAD_FORMAT;

    // Note that we don't support CKey and EKey sizes other than 0x10 in the ENCODING file
    if(pFileHeader->CKeyLength != MD5_HASH_SIZE || pFileHeader->EKeyLength != MD5_HASH_SIZE)
        return ERROR_BAD_FORMAT;

    pEncodingHeader->Version = pFileHeader->Version;
    pEncodingHeader->CKeyLength = pFileHeader->CKeyLength;
    pEncodingHeader->EKeyLength = pFileHeader->EKeyLength;
    pEncodingHeader->CKeyPageCount = ConvertBytesToInteger_4(pFileHeader->CKeyPageCount);
    pEncodingHeader->CKeyPageSize = ConvertBytesToInteger_2(pFileHeader->CKeyPageSize) * 1024;
    pEncodingHeader->EKeyPageCount = ConvertBytesToInteger_4(pFileHeader->EKeyPageCount);
    pEncodingHeader->EKeyPageSize = ConvertBytesToInteger_2(pFileHeader->EKeyPageSize) * 1024;
    pEncodingHeader->ESpecBlockSize = ConvertBytesToInteger_4(pFileHeader->ESpecBlockSize);
    return ERROR_SUCCESS;
}

static int LoadEncodingFile(TCascStorage * hs)
{
    LPBYTE pbEncodingFile;
    DWORD cbEncodingFile = ConvertBytesToInteger_4(hs->EncodingFile.ContentSize);
    int nError = ERROR_SUCCESS;

    // Load the entire encoding file to memory
    pbEncodingFile = LoadInternalFileToMemory(hs, hs->EncodingFile.EKey, CASC_OPEN_BY_EKEY, &cbEncodingFile);
    if(pbEncodingFile == NULL)
        nError = GetLastError();

    // The CKey pages follow after the ESpec block. For each CKey page,
    // there is a FILE_PAGE_HEADER structure with MD5 checksum of the entire page
    if(nError == ERROR_SUCCESS)
    {
        CASC_ENCODING_HEADER EnHeader;

        // Store the ENCODING file data to the CASC storage
        hs->EncodingData.pbData = pbEncodingFile;
        hs->EncodingData.cbData = cbEncodingFile;

        // Store the ENCODING file size to the fake CKey entry
        ConvertIntegerToBytes_4(cbEncodingFile, hs->EncodingFile.ContentSize);

        // Capture the header of the ENCODING file
        nError = CaptureEncodingHeader(&EnHeader, pbEncodingFile, cbEncodingFile);
        if(nError == ERROR_SUCCESS)
        {
            // Get the CKey page header and the page itself
            PFILE_PAGE_HEADER pPageHeader = (PFILE_PAGE_HEADER)(pbEncodingFile + sizeof(FILE_ENCODING_HEADER) + EnHeader.ESpecBlockSize);
            LPBYTE pbPageEntry = (LPBYTE)(pPageHeader + EnHeader.CKeyPageCount);

            // Go through all CKey pages and verify them
            for(DWORD i = 0; i < EnHeader.CKeyPageCount; i++)
            {
                PCASC_CKEY_ENTRY pCKeyEntry = (PCASC_CKEY_ENTRY)pbPageEntry;

                // Check if there is enough space in the buffer
                if((pbPageEntry + EnHeader.CKeyPageSize) > (pbEncodingFile + cbEncodingFile))
                {
                    nError = ERROR_FILE_CORRUPT;
                    break;
                }

                // Check the hash of the entire segment
                // Note that verifying takes considerable time of the storage loading
//              if(!VerifyDataBlockHash(pbCKeyPage, EnHeader.CKeyPageSize, pEncodingSegment->SegmentHash))
//              {
//                  nError = ERROR_FILE_CORRUPT;
//                  break;
//              }

                // Check if the CKey matches with the expected first value
                if(memcmp(pCKeyEntry->CKey, pPageHeader[i].FirstKey, CASC_CKEY_SIZE))
                {
                    nError = ERROR_FILE_CORRUPT;
                    break;
                }

                // Move to the next CKey page
                pbPageEntry += EnHeader.CKeyPageSize;
            }

            // Create the map of the CKeys
            // Note that the array of CKeys is already sorted - no need to sort it
            if(nError == ERROR_SUCCESS)
            {
                nError = CreateMapOfCKeyEntries(hs, EnHeader, pPageHeader);
            }
        }
    }

    return nError;
}

static int LoadRootFile(TCascStorage * hs, DWORD dwLocaleMask)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    PDWORD FileSignature;
    LPBYTE pbRootFile = NULL;
    DWORD cbRootFile = CASC_INVALID_SIZE;
    int nError = ERROR_SUCCESS;

    // Sanity checks
    assert(hs->pCKeyEntryMap != NULL);
    assert(hs->pRootHandler == NULL);

    // Locale: The default parameter is 0 - in that case,
    // we assign the default locale, loaded from the .build.info file
    if(dwLocaleMask == 0)
        dwLocaleMask = hs->dwDefaultLocale;

    // Prioritize the VFS root over legacy ROOT file
    pCKeyEntry = hs->VfsRoot.EKeyCount ? &hs->VfsRoot : &hs->RootFile;

    // Load the entire ROOT file to memory
    pbRootFile = LoadInternalFileToMemory(hs, pCKeyEntry->CKey, CASC_OPEN_BY_CKEY, &cbRootFile);
    if(pbRootFile == NULL)
        nError = GetLastError();

    // Check if the version of the ROOT file
    if(nError == ERROR_SUCCESS && pbRootFile != NULL)
    {
        FileSignature = (PDWORD)pbRootFile;
        switch(FileSignature[0])
        {
            case CASC_MNDX_ROOT_SIGNATURE:
                nError = RootHandler_CreateMNDX(hs, pbRootFile, cbRootFile);
                break;

            case CASC_DIABLO3_ROOT_SIGNATURE:
                nError = RootHandler_CreateDiablo3(hs, pbRootFile, cbRootFile);
                break;

            case CASC_TVFS_ROOT_SIGNATURE:
                nError = RootHandler_CreateTVFS(hs, pbRootFile, cbRootFile);
                break;

            default:

                //
                // Each of these handler creators must verify their format first.
                // If the format was not recognized, they need to return ERROR_BAD_FORMAT
                //

                nError = RootHandler_CreateOverwatch(hs, pbRootFile, cbRootFile);
                if(nError == ERROR_BAD_FORMAT)
                {
                    nError = RootHandler_CreateStarcraft1(hs, pbRootFile, cbRootFile);
                    if(nError == ERROR_BAD_FORMAT)
                    {
                        nError = RootHandler_CreateWoW6(hs, pbRootFile, cbRootFile, dwLocaleMask);
                    }
                }
                break;
        }
    }

    // Insert entries for files with well-known names, mainly from the BUILD file
    if(nError == ERROR_SUCCESS)
    {
        InsertNamedInternalFile(hs, "ENCODING", hs->EncodingFile);
        InsertNamedInternalFile(hs, "ROOT", hs->RootFile);
        InsertNamedInternalFile(hs, "INSTALL", hs->InstallFile);
        InsertNamedInternalFile(hs, "DOWNLOAD", hs->DownloadFile);
        InsertNamedInternalFile(hs, "PATCH", hs->PatchFile);
    }

    // Free the root file
    CASC_FREE(pbRootFile);
    return nError;
}

static TCascStorage * FreeCascStorage(TCascStorage * hs)
{
    size_t i;

    if(hs != NULL)
    {
        // Free the root handler
        if(hs->pRootHandler != NULL)
            delete hs->pRootHandler;
        hs->pRootHandler = NULL;

        // Free the VFS root list
        hs->VfsRootList.Free();

        // Free the pointers to file entries
        if(hs->pCKeyEntryMap != NULL)
            Map_Free(hs->pCKeyEntryMap);
        if(hs->pEKeyEntryMap != NULL)
            Map_Free(hs->pEKeyEntryMap);
        if(hs->EncodingData.pbData != NULL)
            CASC_FREE(hs->EncodingData.pbData);

        // Close all data files
        for(i = 0; i < CASC_MAX_DATA_FILES; i++)
        {
            if(hs->DataFiles[i] != NULL)
            {
                FileStream_Close(hs->DataFiles[i]);
                hs->DataFiles[i] = NULL;
            }
        }

        // Close all key mappings
        for(i = 0; i < CASC_INDEX_COUNT; i++)
        {
            if(hs->IndexFile[i].szFileName != NULL)
                CASC_FREE(hs->IndexFile[i].szFileName);
            if(hs->IndexFile[i].pbFileData != NULL)
                CASC_FREE(hs->IndexFile[i].pbFileData);
            if(hs->IndexFile[i].pEKeyEntries && hs->IndexFile[i].FreeEKeyEntries)
                CASC_FREE(hs->IndexFile[i].pEKeyEntries);
            hs->IndexFile[i].pEKeyEntries = NULL;
        }

        // Free the file paths
        if(hs->szDataPath != NULL)
            CASC_FREE(hs->szDataPath);
        if(hs->szBuildFile != NULL)
            CASC_FREE(hs->szBuildFile);
        if(hs->szIndexPath != NULL)
            CASC_FREE(hs->szIndexPath);
        if(hs->szCdnList != NULL)
            CASC_FREE(hs->szCdnList);

        // Free the blobs
        FreeCascBlob(&hs->CdnConfigKey);
        FreeCascBlob(&hs->CdnBuildKey);
        
        FreeCascBlob(&hs->ArchiveGroup);
        FreeCascBlob(&hs->ArchivesKey);
        FreeCascBlob(&hs->PatchArchivesKey);
        FreeCascBlob(&hs->PatchArchivesGroup);
        FreeCascBlob(&hs->BuildFiles);

        // Free the storage structure
        hs->szClassName = NULL;
        CASC_FREE(hs);
    }

    return NULL;
}

//-----------------------------------------------------------------------------
// Public functions

bool WINAPI CascOpenStorage(const TCHAR * szPath, DWORD dwLocaleMask, HANDLE * phStorage)
{
    TCascStorage * hs;
    int nError = ERROR_SUCCESS;

    // Allocate the storage structure
    hs = (TCascStorage *)CASC_ALLOC(TCascStorage, 1);
    if(hs == NULL)
        nError = ERROR_NOT_ENOUGH_MEMORY;

    // Load the storage configuration
    if(nError == ERROR_SUCCESS)
    {
        // Prepare the base storage parameters
        memset(hs, 0, sizeof(TCascStorage));
        hs->szClassName = "TCascStorage";
        hs->dwHeaderSpanSize = CASC_INVALID_SIZE;
        hs->dwDefaultLocale = CASC_LOCALE_ENUS | CASC_LOCALE_ENGB;
        hs->dwRefCount = 1;
        nError = InitializeCascDirectories(hs, szPath);
    }

    // Now, load the main storage file ".build.info" (or ".build.db" in old storages) 
    if(nError == ERROR_SUCCESS)
    {
        nError = LoadBuildInfo(hs);
    }

    // If the .build.info OR .build.db file has been loaded,
    // proceed with loading the CDN config file
    if (nError == ERROR_SUCCESS)
    {
        nError = LoadCdnConfigFile(hs);
    }

    // Proceed with loading the CDN build file
    if (nError == ERROR_SUCCESS)
    {
        nError = LoadCdnBuildFile(hs);
    }

    // Load the index files
    if(nError == ERROR_SUCCESS)
    {
        nError = LoadIndexFiles(hs);
    }

    // Load the ENCODING file
    if(nError == ERROR_SUCCESS)
    {
        nError = LoadEncodingFile(hs);
    }

    // Load the build manifest ("ROOT" file)
    if(nError == ERROR_SUCCESS)
    {
        nError = LoadRootFile(hs, dwLocaleMask);
    }

    // If something failed, free the storage and return
    if(nError != ERROR_SUCCESS)
    {
        hs = FreeCascStorage(hs);
        SetLastError(nError);
    }

    *phStorage = (HANDLE)hs;
    return (nError == ERROR_SUCCESS);
}

bool WINAPI CascGetStorageInfo(
    HANDLE hStorage,
    CASC_STORAGE_INFO_CLASS InfoClass,
    void * pvStorageInfo,
    size_t cbStorageInfo,
    size_t * pcbLengthNeeded)
{
    TCascStorage * hs;
    DWORD dwInfoValue = 0;

    // Verify the storage handle
    hs = IsValidCascStorageHandle(hStorage);
    if(hs == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Differentiate between info classes
    switch(InfoClass)
    {
        case CascStorageFileCount:
            dwInfoValue = (DWORD)hs->pEKeyEntryMap->ItemCount;
            break;

        case CascStorageFeatures:
            dwInfoValue |= (hs->pRootHandler->GetFlags() & ROOT_FLAG_HAS_NAMES) ? CASC_FEATURE_HAS_NAMES : 0;
            break;

        case CascStorageGameInfo:
            dwInfoValue = hs->dwGameInfo;
            break;

        case CascStorageGameBuild:
            dwInfoValue = hs->dwBuildNumber;
            break;

        case CascStorageInstalledLocales:
            dwInfoValue = hs->dwDefaultLocale;
            break;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
    }

    //
    // Return the required DWORD value
    //

    if(cbStorageInfo < sizeof(DWORD))
    {
        *pcbLengthNeeded = sizeof(DWORD);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    // Give the number of files
    *(PDWORD)pvStorageInfo = dwInfoValue;
    return true;
}

bool WINAPI CascCloseStorage(HANDLE hStorage)
{
    TCascStorage * hs;

    // Verify the storage handle
    hs = IsValidCascStorageHandle(hStorage);
    if(hs == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Only free the storage if the reference count reaches 0
    if(hs->dwRefCount == 1)
    {
        FreeCascStorage(hs);
        return true;
    }

    // Just decrement number of references
    hs->dwRefCount--;
    return true;
}

//-----------------------------------------------------------------------------
// Dumpers

#ifdef _DEBUG
static void DumpCKeyEntry(PCASC_CKEY_ENTRY pCKeyEntry, FILE * fp)
{
    LPBYTE pbEKey;
    char szStringBuff[0x80];

    // Print the CKey and number of EKeys
    fprintf(fp, "%s (e-keys: %02u): ", StringFromBinary(pCKeyEntry->CKey, MD5_HASH_SIZE, szStringBuff), pCKeyEntry->EKeyCount);
    pbEKey = pCKeyEntry->EKey;

    // Print the EKeys
    for(USHORT i = 0; i < pCKeyEntry->EKeyCount; i++, pbEKey += MD5_HASH_SIZE)
        fprintf(fp, "%s ", StringFromBinary(pbEKey, MD5_HASH_SIZE, szStringBuff));
    fprintf(fp, "\n");
}

static void DumpEKeyEntry(FILE * fp, LPBYTE pbEKey, DWORD cbEKey, LPBYTE FileOffsetBE, DWORD FileSize)
{
    ULONGLONG FileOffset = ConvertBytesToInteger_5(FileOffsetBE);
    char szStringBuff[0x80];

    // Dump the entry
    fprintf(fp, "%s  data.%03u  Offset:%08X  Size:%08X\n", StringFromBinary(pbEKey, cbEKey, szStringBuff),
                                                   (DWORD)(FileOffset >> 0x1E),
                                                   (DWORD)(FileOffset & 0x3FFFFFFF),
                                                           FileSize);
}

static void DumpESpecEntry(FILE * fp, PFILE_ESPEC_ENTRY pEspecEntry)
{
    ULONGLONG FileSize = ConvertBytesToInteger_5(pEspecEntry->FileSizeBE);
    char szStringBuff[0x80];

    // Dump the entry
    fprintf(fp, "%s  Index:%08X  Size:%08X\n", StringFromBinary(pEspecEntry->ESpecKey, MD5_HASH_SIZE, szStringBuff),
                                                                ConvertBytesToInteger_4(pEspecEntry->ESpecIndexBE),
                                                        (DWORD)(FileSize));
}

static void DumpESpecEntries(FILE * fp, LPBYTE pbDataPtr, LPBYTE pbDataEnd)
{
    fprintf(fp, "--- ESpec Entries -----------------------------------------------------------\n");

    while(pbDataPtr < pbDataEnd)
    {
        const char * szESpecEntry = (const char *)pbDataPtr;

        // Find the end of the entry
        while((pbDataPtr < pbDataEnd) && (pbDataPtr[0] != 0))
        {
            pbDataPtr++;
        }

        // Dump the entry
        if(pbDataPtr[0] == 0)
        {
            fprintf(fp, "%s\n", szESpecEntry);
            pbDataPtr++;
        }
    }
}

static void DumpCKeyEntries(FILE * fp, CASC_ENCODING_HEADER & EnHeader, LPBYTE pbDataPtr, LPBYTE pbDataEnd)
{
    // Get the CKey page header and the page itself
    PFILE_PAGE_HEADER pPageHeader = (PFILE_PAGE_HEADER)pbDataPtr;
    LPBYTE pbPageEntry = (LPBYTE)(pPageHeader + EnHeader.CKeyPageCount);

    fprintf(fp, "--- CKey Entries ------------------------------------------------------------\n");

    for(DWORD i = 0; i < EnHeader.CKeyPageCount; i++)
    {
        LPBYTE pbCKeyEntry = pbPageEntry;
        LPBYTE pbEndOfPage = pbPageEntry + EnHeader.CKeyPageSize;

        // Check if there is enough space in the buffer
        if((pbPageEntry + EnHeader.CKeyPageSize) > pbDataEnd)
            break;

        // Parse all CKey entries
        while(pbCKeyEntry <= pbEndOfPage)
        {
            PCASC_CKEY_ENTRY pCKeyEntry = (PCASC_CKEY_ENTRY)pbCKeyEntry;

            // Get pointer to the encoding entry
            if(pCKeyEntry->EKeyCount == 0)
                break;

            // Dump this encoding entry
            DumpCKeyEntry(pCKeyEntry, fp);

            // Move to the next encoding entry
            pbCKeyEntry += sizeof(CASC_CKEY_ENTRY) + ((pCKeyEntry->EKeyCount - 1) * EnHeader.CKeyLength);
        }

        // Move to the next segment
        pbPageEntry += EnHeader.CKeyPageSize;
    }
}

static void DumpESpecEntries(FILE * fp, CASC_ENCODING_HEADER & EnHeader, LPBYTE pbDataPtr, LPBYTE pbDataEnd)
{
    PFILE_PAGE_HEADER pPageHeader = (PFILE_PAGE_HEADER)pbDataPtr;
    LPBYTE pbEKeyPageEntry = (LPBYTE)(pPageHeader + EnHeader.EKeyPageCount);

    fprintf(fp, "--- ESpec Entries -----------------------------------------------------------\n");

    for(DWORD i = 0; i < EnHeader.EKeyPageCount; i++)
    {
        LPBYTE pbESpecEntry = pbEKeyPageEntry;
        LPBYTE pbEndOfPage = pbEKeyPageEntry + EnHeader.EKeyPageSize;

        // Check if there is enough space in the buffer
        if((pbEKeyPageEntry + EnHeader.EKeyPageSize) > pbDataEnd)
            break;

        // Parse all EKey entries
        while(pbESpecEntry <= pbEndOfPage)
        {
            PFILE_ESPEC_ENTRY pESpecEntry = (PFILE_ESPEC_ENTRY)pbESpecEntry;

            if(!IsValidMD5(pESpecEntry->ESpecKey))
                break;

            // Dump this encoding entry
            DumpESpecEntry(fp, pESpecEntry);

            // Move to the next encoding entry
            pbESpecEntry += sizeof(FILE_ESPEC_ENTRY);
        }

        // Move to the next page
        pbEKeyPageEntry += EnHeader.EKeyPageSize;
    }
}


void DumpEncodingFile(TCascStorage * hs, FILE * fp)
{
    // Dump header
    fprintf(fp, "=== ENCODING File ===========================================================\n");

    // Dump the encoding file
    if(hs->EncodingData.pbData && hs->EncodingData.cbData)
    {
        CASC_ENCODING_HEADER EnHeader;
        LPBYTE pbEncodingPtr = hs->EncodingData.pbData;
        DWORD cbDataSize;

        // Capture the header of the ENCODING file
        if(CaptureEncodingHeader(&EnHeader, hs->EncodingData.pbData, hs->EncodingData.cbData) == ERROR_SUCCESS)
        {
            // Dump the ENCODING file info
            fprintf(fp, "Version:          %u\n", EnHeader.Version);
            fprintf(fp, "CKey Length:      %02X\n", EnHeader.CKeyLength);
            fprintf(fp, "EKey Length:      %02X\n", EnHeader.EKeyLength);
            fprintf(fp, "CKey page size:   %08X\n", EnHeader.CKeyPageSize);
            fprintf(fp, "CKey page count:  %08X\n", EnHeader.CKeyPageCount);
            fprintf(fp, "EKey page size:   %08X\n", EnHeader.EKeyPageSize);
            fprintf(fp, "EKey page count:  %08X\n", EnHeader.EKeyPageCount);
            fprintf(fp, "ESpec block size: %08X\n", EnHeader.ESpecBlockSize);
            pbEncodingPtr += sizeof(FILE_ENCODING_HEADER);

            // Dump all ESpec entries
            DumpESpecEntries(fp, pbEncodingPtr, pbEncodingPtr + EnHeader.ESpecBlockSize);
            pbEncodingPtr += EnHeader.ESpecBlockSize;

            // Parse all CKey pages and dump them
            cbDataSize = EnHeader.CKeyPageCount * (sizeof(FILE_PAGE_HEADER) + EnHeader.CKeyPageSize);
            DumpCKeyEntries(fp, EnHeader, pbEncodingPtr, pbEncodingPtr + cbDataSize);
            pbEncodingPtr += cbDataSize;

            // Parse all EKey pages and dump them
            cbDataSize = EnHeader.EKeyPageCount * (sizeof(FILE_PAGE_HEADER) + EnHeader.EKeyPageSize);
            DumpESpecEntries(fp, EnHeader, pbEncodingPtr, pbEncodingPtr + cbDataSize);
        }
    }
    else
    {
        fprintf(fp, "(empty)\n");
    }

    // Dump tail
    fprintf(fp, "=============================================================================\n\n");
}

void DumpEKeyEntries(TCascStorage * hs, FILE * fp)
{
    // Dump header
    fprintf(fp, "=== Ekey Entries ============================================================\n");

    // Dump index entries from all index files
    for(int file = 0; file < CASC_INDEX_COUNT; file++)
    {
        PCASC_EKEY_ENTRY pEKeyEntry = hs->IndexFile[file].pEKeyEntries;
        DWORD nEKeyEntries = hs->IndexFile[file].nEKeyEntries;

        // Only if they are present
        if(pEKeyEntry && nEKeyEntries)
        {
//          fprintf(fp, "--- Index: %03u --------------------------------------------------------------\n", file);
            for(DWORD entry = 0; entry < nEKeyEntries; entry++, pEKeyEntry++)
                DumpEKeyEntry(fp, pEKeyEntry->EKey, CASC_EKEY_SIZE, pEKeyEntry->StorageOffset, ConvertBytesToInteger_4_LE(pEKeyEntry->EncodedSize));
        }
    }

    // Dump tail
    fprintf(fp, "=============================================================================\n\n");
}

#endif  // _DEBUG
