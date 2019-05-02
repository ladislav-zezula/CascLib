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

void * ProbeOutputBuffer(void * pvBuffer, size_t cbLength, size_t cbMinLength, size_t * pcbLengthNeeded)
{
    // Verify the output length
    if(cbLength < cbMinLength)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        pvBuffer = NULL;
    }

    // Give the output length and return result
    if(pcbLengthNeeded != NULL)
        pcbLengthNeeded[0] = cbMinLength;
    return pvBuffer;
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
        // Check the input structure for valid CKey
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

// Verifies a guarded block - data availability and checksum match
static LPBYTE CaptureGuardedBlock(LPBYTE pbFileData, LPBYTE pbFileEnd)
{
    PFILE_INDEX_GUARDED_BLOCK pBlock = (PFILE_INDEX_GUARDED_BLOCK)pbFileData;

    // Check the guarded block
    if((pbFileData + sizeof(FILE_INDEX_GUARDED_BLOCK)) >= pbFileEnd)
        return NULL;
    if((pbFileData + sizeof(FILE_INDEX_GUARDED_BLOCK) + pBlock->BlockSize) > pbFileEnd)
        return NULL;
    
    // Verify the hash
    if(hashlittle(pbFileData + sizeof(FILE_INDEX_GUARDED_BLOCK), pBlock->BlockSize, 0) != pBlock->BlockHash)
        return NULL;

    // Give the output
    return (LPBYTE)(pBlock + 1);
}

// Second method of checking a guarded block; hash is calculated entry-by-entry.
// Unfortunately, due to implementation in hashlittle2(), we cannot calculate the hash of a continuous block
static LPBYTE CaptureGuardedBlock2(LPBYTE pbFileData, LPBYTE pbFileEnd, size_t EntryLength, PDWORD PtrBlockSize = NULL)
{
    PFILE_INDEX_GUARDED_BLOCK pBlock = (PFILE_INDEX_GUARDED_BLOCK)pbFileData;
    LPBYTE pbEntryPtr;
    size_t EntryCount;
    unsigned int HashHigh = 0;
    unsigned int HashLow = 0;

    // Check the guarded block. There must be enough bytes to contain FILE_INDEX_GUARDED_BLOCK
    // and also the block length must not be NULL
    if ((pbFileData + sizeof(FILE_INDEX_GUARDED_BLOCK)) >= pbFileEnd)
        return NULL;
    if (pBlock->BlockSize == 0 || (pbFileData + sizeof(FILE_INDEX_GUARDED_BLOCK) + pBlock->BlockSize) > pbFileEnd)
        return NULL;

    // Compute the hash entry-by-entry
    pbEntryPtr = pbFileData + sizeof(FILE_INDEX_GUARDED_BLOCK);
    EntryCount = pBlock->BlockSize / EntryLength;
    for (size_t i = 0; i < EntryCount; i++)
    {
        hashlittle2(pbEntryPtr, EntryLength, &HashHigh, &HashLow);
        pbEntryPtr += EntryLength;
    }

    // Verify hash
    if (HashHigh != pBlock->BlockHash)
        return NULL;

    // Give the output
    if (PtrBlockSize != NULL)
        PtrBlockSize[0] = pBlock->BlockSize;
    return (LPBYTE)(pBlock + 1);
}

// Third method of checking a guarded block; There is 32-bit hash, followed by EKey entry
// The hash covers the EKey entry plus one byte
static LPBYTE CaptureGuardedBlock3(LPBYTE pbFileData, LPBYTE pbFileEnd, size_t EntryLength)
{
    PDWORD PtrEntryHash = (PDWORD)pbFileData;
    DWORD EntryHash;

    // Check the guarded block. There must be enough bytes to contain single entry and the hash
    // Also, the hash must be present
    if ((pbFileData + sizeof(DWORD) + EntryLength) >= pbFileEnd)
        return NULL;
    if (PtrEntryHash[0] == 0)
        return NULL;

    EntryHash = hashlittle(pbFileData + sizeof(DWORD), EntryLength+1, 0) | 0x80000000;
    if(EntryHash != PtrEntryHash[0])
        return NULL;

    // Give the output
    return (LPBYTE)(PtrEntryHash + 1);
}

static bool CaptureEKeyEntry(CASC_INDEX_HEADER & InHeader, PCASC_EKEY_ENTRY pEKeyEntry, LPBYTE pbEKeyEntry)
{
    // Clear the second half of the EKey
    *(PDWORD)(pEKeyEntry->EKey + 0x08) = 0;
    *(PDWORD)(pEKeyEntry->EKey + 0x0C) = 0;

    // Copy the EKey. We assume 9 bytes
    pEKeyEntry->EKey[0x00] = pbEKeyEntry[0];
    pEKeyEntry->EKey[0x01] = pbEKeyEntry[1];
    pEKeyEntry->EKey[0x02] = pbEKeyEntry[2];
    pEKeyEntry->EKey[0x03] = pbEKeyEntry[3];
    pEKeyEntry->EKey[0x04] = pbEKeyEntry[4];
    pEKeyEntry->EKey[0x05] = pbEKeyEntry[5];
    pEKeyEntry->EKey[0x06] = pbEKeyEntry[6];
    pEKeyEntry->EKey[0x07] = pbEKeyEntry[7];
    pEKeyEntry->EKey[0x08] = pbEKeyEntry[8];
    pEKeyEntry->EKey[0x09] = pbEKeyEntry[9];
    pbEKeyEntry += InHeader.EKeyLength;

    // Copy the storage offset
    pEKeyEntry->StorageOffset = ConvertBytesToInteger_5(pbEKeyEntry);
    pbEKeyEntry += InHeader.StorageOffsetLength;

    // Clear the tag bit mask
    pEKeyEntry->TagBitMask = 0;

    // Copy the encoded length
    pEKeyEntry->EncodedSize = ConvertBytesToInteger_4_LE(pbEKeyEntry);
    pEKeyEntry->Alignment = 0;
    return true;
}

static int LoadEKeyItems(TCascStorage * hs, LPBYTE pbEKeyEntry, LPBYTE pbEKeyEnd)
{
    PCASC_EKEY_ENTRY pEKeyEntry;
    size_t EntryLength = hs->InHeader.EntryLength;

    while((pbEKeyEntry + EntryLength) <= pbEKeyEnd)
    {
        // Insert new entry to the array
        pEKeyEntry = (PCASC_EKEY_ENTRY)hs->EKeyArray.Insert(NULL, 1);
        if(pEKeyEntry == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Capture the EKey entry
        if(!CaptureEKeyEntry(hs->InHeader, pEKeyEntry, pbEKeyEntry))
            break;

        pbEKeyEntry += EntryLength;
    }

    return ERROR_SUCCESS;
}

static int CaptureIndexHeader_V1(CASC_INDEX_HEADER & InHeader, LPBYTE pbFileData, DWORD cbFileData, DWORD BucketIndex)
{
    PFILE_INDEX_HEADER_V1 pIndexHeader = (PFILE_INDEX_HEADER_V1)pbFileData;
    LPBYTE pbKeyEntries;
    LPBYTE pbFileEnd = pbFileData + cbFileData;
    size_t cbKeyEntries;
    DWORD HeaderHash;

    // Check the available size. Note that the index file can be just a header.
    if((pbFileData + sizeof(FILE_INDEX_HEADER_V1)) > pbFileEnd)
        return ERROR_BAD_FORMAT;
    if(pIndexHeader->IndexVersion != 0x05 || pIndexHeader->BucketIndex != (BYTE)BucketIndex || pIndexHeader->field_8 == 0)
        return ERROR_BAD_FORMAT;
    if(pIndexHeader->EncodedSizeLength != 0x04 || pIndexHeader->StorageOffsetLength != 0x05 || pIndexHeader->EKeyLength != 0x09)
        return ERROR_NOT_SUPPORTED;

    // Verify the header hash
    HeaderHash = pIndexHeader->HeaderHash;
    pIndexHeader->HeaderHash = 0;
    if(hashlittle(pbFileData, sizeof(FILE_INDEX_HEADER_V1), 0) != HeaderHash)
        return ERROR_BAD_FORMAT;

    // Return the header hash back
    pIndexHeader->HeaderHash = HeaderHash;

    // Copy the fields
    InHeader.IndexVersion        = pIndexHeader->IndexVersion;
    InHeader.BucketIndex         = pIndexHeader->BucketIndex;
    InHeader.StorageOffsetLength = pIndexHeader->StorageOffsetLength;
    InHeader.EncodedSizeLength   = pIndexHeader->EncodedSizeLength;
    InHeader.EKeyLength          = pIndexHeader->EKeyLength;
    InHeader.FileOffsetBits      = pIndexHeader->FileOffsetBits;
    InHeader.Alignment           = 0;
    InHeader.SegmentSize         = pIndexHeader->SegmentSize;

    // Determine the size of the header
    InHeader.HeaderLength = sizeof(FILE_INDEX_HEADER_V1);
    InHeader.HeaderPadding = 0;
    InHeader.EntryLength = pIndexHeader->EKeyLength + pIndexHeader->StorageOffsetLength + pIndexHeader->EncodedSizeLength;
    InHeader.EKeyCount = pIndexHeader->EKeyCount1 + pIndexHeader->EKeyCount2;

    // Verify the entries hash - 1st block
    pbKeyEntries = pbFileData + InHeader.HeaderLength;
    cbKeyEntries = pIndexHeader->EKeyCount1 * InHeader.EntryLength;
    if((pbKeyEntries + cbKeyEntries) > pbFileEnd)
        return ERROR_FILE_CORRUPT;
    if(hashlittle(pbKeyEntries, cbKeyEntries, 0) != pIndexHeader->KeysHash1)
        return ERROR_FILE_CORRUPT;

    // Verify the entries hash - 2nd block
    pbKeyEntries = pbKeyEntries + cbKeyEntries;
    cbKeyEntries = pIndexHeader->EKeyCount2 * InHeader.EntryLength;
    if((pbKeyEntries + cbKeyEntries) > pbFileEnd)
        return ERROR_FILE_CORRUPT;
    if(hashlittle(pbKeyEntries, cbKeyEntries, 0) != pIndexHeader->KeysHash2)
        return ERROR_FILE_CORRUPT;

    return ERROR_SUCCESS;
}

static int CaptureIndexHeader_V2(CASC_INDEX_HEADER & InHeader, LPBYTE pbFileData, DWORD cbFileData, DWORD BucketIndex)
{
    PFILE_INDEX_HEADER_V2 pIndexHeader;
    LPBYTE pbFileEnd = pbFileData + cbFileData;

    // Check for guarded block
    if((pbFileData = CaptureGuardedBlock(pbFileData, pbFileEnd)) == NULL)
        return ERROR_FILE_CORRUPT;
    pIndexHeader = (PFILE_INDEX_HEADER_V2)pbFileData;

    // Verify the content of the index header
    if(pIndexHeader->IndexVersion != 0x07 || pIndexHeader->BucketIndex != (BYTE)BucketIndex || pIndexHeader->ExtraBytes != 0x00)
        return ERROR_BAD_FORMAT;
    if(pIndexHeader->EncodedSizeLength != 0x04 || pIndexHeader->StorageOffsetLength != 0x05 || pIndexHeader->EKeyLength != 0x09)
        return ERROR_BAD_FORMAT;

    // Capture the values from the index header
    InHeader.IndexVersion        = pIndexHeader->IndexVersion;
    InHeader.BucketIndex         = pIndexHeader->BucketIndex;
    InHeader.StorageOffsetLength = pIndexHeader->StorageOffsetLength;
    InHeader.EncodedSizeLength   = pIndexHeader->EncodedSizeLength;
    InHeader.EKeyLength          = pIndexHeader->EKeyLength;
    InHeader.FileOffsetBits      = pIndexHeader->FileOffsetBits;
    InHeader.Alignment           = 0;
    InHeader.SegmentSize         = pIndexHeader->SegmentSize;

    // Supply the lengths
    InHeader.HeaderLength = sizeof(FILE_INDEX_GUARDED_BLOCK) + sizeof(FILE_INDEX_HEADER_V2);
    InHeader.HeaderPadding = 8;
    InHeader.EntryLength = pIndexHeader->EKeyLength + pIndexHeader->StorageOffsetLength + pIndexHeader->EncodedSizeLength;
    InHeader.EKeyCount = 0;
    return ERROR_SUCCESS;
}    

static int LoadIndexFile_V1(TCascStorage * hs, LPBYTE pbFileData, DWORD cbFileData)
{
    CASC_INDEX_HEADER & InHeader = hs->InHeader;
    LPBYTE pbEKeyEntries = pbFileData + InHeader.HeaderLength + InHeader.HeaderPadding;

    // Load the entries from a continuous array
    return LoadEKeyItems(hs, pbEKeyEntries, pbFileData + cbFileData);
}

static int LoadIndexFile_V2(TCascStorage * hs, LPBYTE pbFileData, DWORD cbFileData)
{
    CASC_INDEX_HEADER & InHeader = hs->InHeader;
    LPBYTE pbEKeyEntry;
    LPBYTE pbFileEnd = pbFileData + cbFileData;
    LPBYTE pbFilePtr = pbFileData + InHeader.HeaderLength + InHeader.HeaderPadding;
    size_t EKeyEntriesLength;
    DWORD BlockSize = 0;
    int nError = ERROR_NOT_SUPPORTED;

    // Get the pointer to the first block of EKey entries
    if((pbEKeyEntry = CaptureGuardedBlock2(pbFilePtr, pbFileEnd, InHeader.EntryLength, &BlockSize)) != NULL)
    {
        // Supply the number of EKey entries
        InHeader.HeaderPadding += sizeof(FILE_INDEX_GUARDED_BLOCK);

        // Load the continuous array of EKeys
        return LoadEKeyItems(hs, pbEKeyEntry, pbEKeyEntry + BlockSize);
    }

    // Get the pointer to the second block of EKey entries.
    // They are alway at the position aligned to 4096
    EKeyEntriesLength = pbFileEnd - pbFilePtr;
    if(EKeyEntriesLength >= 0x7800)
    {
        PCASC_EKEY_ENTRY pEKeyEntry;
        LPBYTE pbStartPage = pbFileData + 0x1000;
        LPBYTE pbEndPage = pbStartPage + FILE_INDEX_PAGE_SIZE;
        size_t AlignedLength = ALIGN_TO_SIZE(InHeader.EntryLength, 4);

        // Parse the chunks with the EKey entries
        while(pbStartPage < pbFileEnd)
        {
            LPBYTE pbEKeyEntry = pbStartPage;
            
            while(pbEKeyEntry < pbEndPage)
            {
                // Check the EKey entry protected by 32-bit hash
                if((pbEKeyEntry = CaptureGuardedBlock3(pbEKeyEntry, pbEndPage, InHeader.EntryLength)) == NULL)
                    break;

                // Insert the EKey entry to the array
                pEKeyEntry = (PCASC_EKEY_ENTRY)hs->EKeyArray.Insert(NULL, 1);
                if(pEKeyEntry == NULL)
                    return ERROR_NOT_ENOUGH_MEMORY;

                // Capture the EKey entry
                if(!CaptureEKeyEntry(InHeader, pEKeyEntry, pbEKeyEntry))
                    break;
                pbEKeyEntry += AlignedLength;
            }

            // Move to the next chunk
            pbStartPage += FILE_INDEX_PAGE_SIZE;
        }
        nError = ERROR_SUCCESS;
    }

    return nError;
}

static int LoadIndexFile(TCascStorage * hs, LPBYTE pbFileData, ULONG cbFileData, DWORD BucketIndex)
{
    // Check for CASC version 2
    if(CaptureIndexHeader_V2(hs->InHeader, pbFileData, cbFileData, BucketIndex) == ERROR_SUCCESS)
        return LoadIndexFile_V2(hs, pbFileData, cbFileData);

    // Check for CASC index version 1
    if(CaptureIndexHeader_V1(hs->InHeader, pbFileData, cbFileData, BucketIndex) == ERROR_SUCCESS)
        return LoadIndexFile_V1(hs, pbFileData, cbFileData);

    // Should never happen
    assert(false);
    return ERROR_BAD_FORMAT;
}

static int LoadIndexFile(TCascStorage * hs, const TCHAR * szFileName, DWORD BucketIndex)
{
    LPBYTE pbFileData;
    ULONG cbFileData;
    size_t MaxEntryCount;
    int nError = ERROR_SUCCESS;

    // WoW6 actually reads THE ENTIRE file to memory. Verified on Mac build (x64).
    pbFileData = LoadExternalFileToMemory(szFileName, &cbFileData);
    if(pbFileData && cbFileData)
    {
        // Make sure that we created the dynamic array for CKey entries
        // Estimate the maximum number of entries from the first index file's size
        if(BucketIndex == 0)
        {
            MaxEntryCount = cbFileData / (9 + 5 + 4);
            nError = hs->EKeyArray.Create(sizeof(CASC_EKEY_ENTRY), MaxEntryCount * CASC_INDEX_COUNT);
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        // Parse and load the index file
        nError = LoadIndexFile(hs, pbFileData, cbFileData, BucketIndex);

        // Free the loaded file
        CASC_FREE(pbFileData);
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
}

static int CreateMapOfEKeyEntries(TCascStorage * hs)
{
    size_t TotalCount = hs->EKeyArray.ItemCount();
    int nError;

    // Create the map of all index entries
    nError = hs->EKeyMap.Create(TotalCount, hs->InHeader.EKeyLength, FIELD_OFFSET(CASC_EKEY_ENTRY, EKey), true);
    if(nError == ERROR_SUCCESS)
    {
        // Put all EKey entries in the map
        for(size_t i = 0; i < TotalCount; i++)
        {
            PCASC_EKEY_ENTRY pEKeyEntry = (PCASC_EKEY_ENTRY)hs->EKeyArray.ItemAt(i);

            // Insert the index entry to the map
            // Note that duplicate entries will not be inserted to the map
            //
            // Duplicate entries in WoW-WOD build 18179:
            // 9e dc a7 8f e2 09 ad d8 b7 (encoding file)
            // f3 5e bb fb d1 2b 3f ef 8b
            // c8 69 9f 18 a2 5e df 7e 52
            hs->EKeyMap.InsertObject(pEKeyEntry, pEKeyEntry->EKey);
        }
    }

    return nError;
}

static int LoadIndexFiles(TCascStorage * hs)
{
    TCHAR * szFileName;
    DWORD OldIndexArray[CASC_INDEX_COUNT];
    DWORD IndexArray[CASC_INDEX_COUNT];
    int nError;

    // Scan all index files and load contained EKEY entries
    memset(OldIndexArray, 0, sizeof(OldIndexArray));
    memset(IndexArray, 0, sizeof(IndexArray));
    nError = ScanIndexDirectory(hs->szIndexPath, IndexDirectory_OnFileFound, IndexArray, OldIndexArray, hs);
    if(nError == ERROR_SUCCESS)
    {
        // Load each index file
        for(DWORD i = 0; i < CASC_INDEX_COUNT; i++)
        {
            // Create the name of the index file
            if((szFileName = CreateIndexFileName(hs, i, IndexArray[i])) != NULL)
            {
                if((nError = LoadIndexFile(hs, szFileName, i)) != ERROR_SUCCESS)
                    break;
                CASC_FREE(szFileName);
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

int CaptureEncodingHeader(CASC_ENCODING_HEADER & EnHeader, LPBYTE pbFileData, size_t cbFileData)
{
    PFILE_ENCODING_HEADER pFileHeader = (PFILE_ENCODING_HEADER)pbFileData;

    // Check the signature ('EN') and version
    if(cbFileData < sizeof(FILE_ENCODING_HEADER) || pFileHeader->Magic != FILE_MAGIC_ENCODING || pFileHeader->Version != 0x01)
        return ERROR_BAD_FORMAT;

    // Note that we don't support CKey and EKey sizes other than 0x10 in the ENCODING file
    if(pFileHeader->CKeyLength != MD5_HASH_SIZE || pFileHeader->EKeyLength != MD5_HASH_SIZE)
        return ERROR_BAD_FORMAT;

    EnHeader.Magic = pFileHeader->Magic;
    EnHeader.Version = pFileHeader->Version;
    EnHeader.CKeyLength = pFileHeader->CKeyLength;
    EnHeader.EKeyLength = pFileHeader->EKeyLength;
    EnHeader.CKeyPageCount = ConvertBytesToInteger_4(pFileHeader->CKeyPageCount);
    EnHeader.CKeyPageSize = ConvertBytesToInteger_2(pFileHeader->CKeyPageSize) * 1024;
    EnHeader.EKeyPageCount = ConvertBytesToInteger_4(pFileHeader->EKeyPageCount);
    EnHeader.EKeyPageSize = ConvertBytesToInteger_2(pFileHeader->EKeyPageSize) * 1024;
    EnHeader.ESpecBlockSize = ConvertBytesToInteger_4(pFileHeader->ESpecBlockSize);
    return ERROR_SUCCESS;
}

static void LoadCKeyPage(TCascStorage * hs, CASC_ENCODING_HEADER & EnHeader, LPBYTE pbPageBegin, LPBYTE pbEndOfPage)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    LPBYTE pbCKeyEntry = pbPageBegin;

    // Sanity checks
    assert(hs->CKeyMap.IsInitialized() == true);

    // Parse all encoding entries
    while(pbCKeyEntry < pbEndOfPage)
    {
        // Get pointer to the encoding entry
        pCKeyEntry = (PCASC_CKEY_ENTRY)pbCKeyEntry;
        if(pCKeyEntry->EKeyCount == 0)
            break;

        // Insert the object to the map
        hs->CKeyMap.InsertObject(pCKeyEntry, pCKeyEntry->CKey);

        // Move to the next encoding entry
        pbCKeyEntry = pbCKeyEntry + 2 + 4 + EnHeader.CKeyLength + (pCKeyEntry->EKeyCount * EnHeader.EKeyLength);
    }
}

static int LoadEncodingManifest(TCascStorage * hs)
{
    LPBYTE pbEncodingFile;
    DWORD cbEncodingFile = ConvertBytesToInteger_4(hs->EncodingFile.ContentSize);
    int nError = ERROR_SUCCESS;

    // Load the entire encoding file to memory
    pbEncodingFile = LoadInternalFileToMemory(hs, hs->EncodingFile.EKey, CASC_OPEN_BY_EKEY, cbEncodingFile, &cbEncodingFile);
    if(pbEncodingFile != NULL && cbEncodingFile != 0)
    {
        CASC_ENCODING_HEADER EnHeader;
        DWORD MaxCKeyItems;

        // Store the ENCODING file data to the CASC storage
        hs->EncodingManifest.pbData = pbEncodingFile;
        hs->EncodingManifest.cbData = cbEncodingFile;

        // Store the ENCODING file size to the fake CKey entry
        ConvertIntegerToBytes_4(cbEncodingFile, hs->EncodingFile.ContentSize);

        // Capture the header of the ENCODING file
        nError = CaptureEncodingHeader(EnHeader, pbEncodingFile, cbEncodingFile);
        if(nError == ERROR_SUCCESS)
        {
            // Get the CKey page header and the first page
            PFILE_CKEY_PAGE pPageHeader = (PFILE_CKEY_PAGE)(pbEncodingFile + sizeof(FILE_ENCODING_HEADER) + EnHeader.ESpecBlockSize);
            LPBYTE pbCKeyPage = (LPBYTE)(pPageHeader + EnHeader.CKeyPageCount);

            // Create the map of content keys
            MaxCKeyItems = (EnHeader.CKeyPageCount * EnHeader.CKeyPageSize) / sizeof(FILE_CKEY_ENTRY);
            nError = hs->CKeyMap.Create(MaxCKeyItems + CASC_EXTRA_FILES, EnHeader.CKeyLength, FIELD_OFFSET(CASC_CKEY_ENTRY, CKey), true);
            if(nError == ERROR_SUCCESS)
            {
                // Go through all CKey pages and verify them
                for(DWORD i = 0; i < EnHeader.CKeyPageCount; i++)
                {
                    PFILE_CKEY_ENTRY pCKeyEntry = (PFILE_CKEY_ENTRY)pbCKeyPage;

                    // Check if there is enough space in the buffer
                    if((pbCKeyPage + EnHeader.CKeyPageSize) > (pbEncodingFile + cbEncodingFile))
                    {
                        nError = ERROR_FILE_CORRUPT;
                        break;
                    }

                    // Check the hash of the entire segment
                    // Note that verifying takes considerable time of the storage loading
//                  if(!VerifyDataBlockHash(pbCKeyPage, EnHeader.CKeyPageSize, pEncodingSegment->SegmentHash))
//                  {
//                      nError = ERROR_FILE_CORRUPT;
//                      break;
//                  }

                    // Check if the CKey matches with the expected first value
                    if(memcmp(pCKeyEntry->CKey, pPageHeader[i].FirstKey, CASC_CKEY_SIZE))
                    {
                        nError = ERROR_FILE_CORRUPT;
                        break;
                    }

                    // Load the entire page of CKey entries.
                    // This operation will never fail, because all memory is already pre-allocated
                    LoadCKeyPage(hs, EnHeader, pbCKeyPage, pbCKeyPage + EnHeader.CKeyPageSize);

                    // Move to the next CKey page
                    pbCKeyPage += EnHeader.CKeyPageSize;
                }

                // Insert extra entry for ENCODING file. We need to do that artificially,
                // because CKey of ENCODING file is not in ENCODING itself :-)
                hs->CKeyMap.InsertObject(&hs->EncodingFile, hs->EncodingFile.CKey);
            }
        }
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
}

// Returns the length of the DOWNLOAD header. Returns 0 if bad format
int CaptureDownloadHeader(CASC_DOWNLOAD_HEADER & DlHeader, LPBYTE pbFileData, size_t cbFileData)
{
    PFILE_DOWNLOAD_HEADER pFileHeader = (PFILE_DOWNLOAD_HEADER)pbFileData;

    // Check the signature ('DL') and version
    if(cbFileData < sizeof(FILE_DOWNLOAD_HEADER) || pFileHeader->Magic != FILE_MAGIC_DOWNLOAD || pFileHeader->Version > 3)
        return ERROR_BAD_FORMAT;

    // Note that we don't support CKey sizes greater than 0x10 in the DOWNLOAD file
    if(pFileHeader->EKeyLength > MD5_HASH_SIZE)
        return ERROR_BAD_FORMAT;

    // Capture the header version 1
    memset(&DlHeader, 0, sizeof(CASC_DOWNLOAD_HEADER));
    DlHeader.Magic = pFileHeader->Magic;
    DlHeader.Version = pFileHeader->Version;
    DlHeader.EKeyLength = pFileHeader->EKeyLength;
    DlHeader.EntryHasChecksum = pFileHeader->EntryHasChecksum;
    DlHeader.EntryCount = ConvertBytesToInteger_4(pFileHeader->EntryCount);
    DlHeader.TagCount = ConvertBytesToInteger_2(pFileHeader->TagCount);
    DlHeader.HeaderLength = FIELD_OFFSET(FILE_DOWNLOAD_HEADER, FlagByteSize);
    DlHeader.EntryLength = DlHeader.EKeyLength + 5 + 1 + (DlHeader.EntryHasChecksum ? 4 : 0);

    // Capture header version 2
    if (pFileHeader->Version >= 2)
    {
        DlHeader.FlagByteSize = pFileHeader->FlagByteSize;
        DlHeader.HeaderLength = FIELD_OFFSET(FILE_DOWNLOAD_HEADER, BasePriority);
        DlHeader.EntryLength += DlHeader.FlagByteSize;

        // Capture header version 3
        if (pFileHeader->Version >= 3)
        {
            DlHeader.BasePriority = pFileHeader->BasePriority;
            DlHeader.HeaderLength = sizeof(FILE_DOWNLOAD_HEADER);
        }
    }

    return ERROR_SUCCESS;
}

int CaptureDownloadEntry(CASC_DOWNLOAD_HEADER & DlHeader, CASC_DOWNLOAD_ENTRY & DlEntry, LPBYTE pbFilePtr, LPBYTE pbFileEnd)
{
    // Check the range
    if((pbFilePtr + DlHeader.EntryLength) >= pbFileEnd)
        return ERROR_BAD_FORMAT;
    memset(&DlEntry, 0, sizeof(CASC_DOWNLOAD_ENTRY));

    // Copy the EKey
    memcpy(DlEntry.EKey, pbFilePtr, DlHeader.EKeyLength);
    pbFilePtr += DlHeader.EKeyLength;

    // Convert the file size
    DlEntry.FileSize = ConvertBytesToInteger_5(pbFilePtr);
    pbFilePtr += 5;

    // Copy the file priority
    DlEntry.Priority = pbFilePtr[0];
    pbFilePtr++;

    // Copy the checksum
    if(DlHeader.EntryHasChecksum)
    {
        DlEntry.Checksum = ConvertBytesToInteger_4(pbFilePtr);
        pbFilePtr += 4;
    }

    // Copy the flags
    DlEntry.Flags = ConvertBytesToInteger_X(pbFilePtr, DlHeader.FlagByteSize);
    return ERROR_SUCCESS;
}

int CaptureDownloadTag(CASC_DOWNLOAD_HEADER & DlHeader, CASC_TAG_ENTRY1 & DlTag, LPBYTE pbFilePtr, LPBYTE pbFileEnd)
{
    LPBYTE pbSaveFilePtr = pbFilePtr;

    // Prepare the tag structure
    memset(&DlTag, 0, sizeof(CASC_TAG_ENTRY1));
    DlTag.szTagName = (const char *)pbFilePtr;

    // Skip the tag string
    while(pbFilePtr < pbFileEnd && pbFilePtr[0] != 0)
        pbFilePtr++;
    if(pbFilePtr >= pbFileEnd)
        return ERROR_BAD_FORMAT;
    
    // Save the length of the tag name
    DlTag.NameLength = (pbFilePtr - pbSaveFilePtr);
    pbFilePtr++;

    // Get the tag value
    if((pbFilePtr + sizeof(DWORD)) > pbFileEnd)
        return ERROR_BAD_FORMAT;
    DlTag.TagValue = ConvertBytesToInteger_2(pbFilePtr);
    pbFilePtr += 2;

    // Get the bitmap
    DlTag.Bitmap = pbFilePtr;

    // Get the bitmap length.
    // If the bitmap is last in the list and it's shorter than declared, we make it shorter
    DlTag.BitmapLength = (DlHeader.EntryCount / 8) + ((DlHeader.EntryCount & 0x03) ? 1 : 0);
    if((pbFilePtr + DlTag.BitmapLength) > pbFileEnd)
        DlTag.BitmapLength = (pbFileEnd - pbFilePtr);
    
    // Get the entry length
    DlTag.TagLength = (pbFilePtr - pbSaveFilePtr) + DlTag.BitmapLength;
    return ERROR_SUCCESS;
}

static int LoadDownloadManifest(TCascStorage * hs, CASC_DOWNLOAD_HEADER & DlHeader, LPBYTE pbFileData, LPBYTE pbFileEnd)
{
    PCASC_TAG_ENTRY1 TagArray;
    LPBYTE pbEntries = pbFileData + DlHeader.HeaderLength;
    LPBYTE pbEntry = pbEntries;
    LPBYTE pbTags = pbEntries + DlHeader.EntryLength * DlHeader.EntryCount;
    LPBYTE pbTag = pbTags;
    size_t nMaxNameLength = 0;
    size_t nTagEntryLengh = 0;
    int nError = ERROR_NOT_ENOUGH_MEMORY;

    // Allocate space for the tag array
    TagArray = CASC_ALLOC(CASC_TAG_ENTRY1, DlHeader.TagCount);
    if(TagArray != NULL)
    {
//      FILE * fp;
//      char filename[MAX_PATH];

        // Get the longest tag name
        for(DWORD i = 0; i < DlHeader.TagCount; i++)
        {
            if(CaptureDownloadTag(DlHeader, TagArray[i], pbTag, pbFileEnd) == ERROR_SUCCESS)
                nMaxNameLength = CASCLIB_MAX(nMaxNameLength, TagArray[i].NameLength);
            pbTag = pbTag + TagArray[i].TagLength;

            //sprintf(filename, "E:\\TagBitmap-%s.bin", TagArray[i].szTagName);
            //if((fp = fopen(filename, "wb")) != NULL)
            //{
            //    fwrite(TagArray[i].Bitmap, 1, TagArray[i].BitmapLength, fp);
            //    fclose(fp);
            //}
        }

        // Determine the tag entry length
        nTagEntryLengh = FIELD_OFFSET(CASC_TAG_ENTRY2, szTagName) + nMaxNameLength;
        nTagEntryLengh = ALIGN_TO_SIZE(nTagEntryLengh, 8);

        // Load the tags into array in the storage structure
        nError = hs->TagsArray.Create(nTagEntryLengh, DlHeader.TagCount);
        if(nError == ERROR_SUCCESS)
        {
            // Convert the array of CASC_DOWNLOAD_TAG1 to array of CASC_DOWNLOAD_TAG2
            for(DWORD i = 0; i < DlHeader.TagCount; i++)
            {
                PCASC_TAG_ENTRY1 pSourceTag = &TagArray[i];
                PCASC_TAG_ENTRY2 pTargetTag;

                // Insert the tag to the array
                pTargetTag = (PCASC_TAG_ENTRY2)hs->TagsArray.Insert(NULL, 1);
                if(pTargetTag == NULL)
                {
                    nError = ERROR_NOT_ENOUGH_MEMORY;
                    break;
                }

                // Copy the tag structure
                memset(pTargetTag, 0, nTagEntryLengh);
                memcpy(pTargetTag->szTagName, pSourceTag->szTagName, pSourceTag->NameLength);
                pTargetTag->NameLength = pSourceTag->NameLength;
                pTargetTag->TagValue = pSourceTag->TagValue;
            }

            // Now parse all entries. For each entry, mark the corresponding tag bit in the EKey table
            for(DWORD i = 0; i < DlHeader.EntryCount; i++)
            {
                CASC_DOWNLOAD_ENTRY DlEntry;
                PCASC_EKEY_ENTRY pEKeyEntry;
                size_t BitMaskOffset = (i / 8);
                size_t BitMaskBit = 1 << (i % 8);

                // Capture the download entry
                if(CaptureDownloadEntry(DlHeader, DlEntry, pbEntry, pbFileEnd) != ERROR_SUCCESS)
                    break;

                // Find the EKey in the EKey map. If found, mark all tags that are set for this entry
                pEKeyEntry = (PCASC_EKEY_ENTRY)hs->EKeyMap.FindObject(DlEntry.EKey);
                if(pEKeyEntry != NULL)
                {
                    ULONGLONG TagBit = 1;

                    for(DWORD j = 0; j < DlHeader.TagCount; j++)
                    {
                        // Set the bit in the entry, if the tag for it is present
                        if((BitMaskOffset < TagArray[j].BitmapLength) && (TagArray[j].Bitmap[BitMaskOffset] & BitMaskBit))
                            pEKeyEntry->TagBitMask |= TagBit;
                        
                        // Move to the next bit
                        TagBit <<= 1;
                    }
                }

                // Move to the next entry
                pbEntry += DlHeader.EntryLength;
            }
        }

        CASC_FREE(TagArray);
    }
    return nError;
}

static int LoadDownloadManifest(TCascStorage * hs)
{
    LPBYTE pbDownloadFile = NULL;
    DWORD cbDownloadFile = 0;
    int nError = ERROR_SUCCESS;

    // Load the entire DOWNLOAD file to memory
    pbDownloadFile = LoadInternalFileToMemory(hs, hs->DownloadFile.CKey, CASC_OPEN_BY_CKEY, CASC_INVALID_SIZE, &cbDownloadFile);
    if(pbDownloadFile != NULL && cbDownloadFile != 0)
    {
        CASC_DOWNLOAD_HEADER DlHeader;

        // Store the content of the download manifest
        hs->DownloadManifest.pbData = pbDownloadFile;
        hs->DownloadManifest.cbData = cbDownloadFile;

        // Capture the header of the DOWNLOAD file
        nError = CaptureDownloadHeader(DlHeader, pbDownloadFile, cbDownloadFile);
        if(nError == ERROR_SUCCESS)
        {
            // Parse the entire download manifest
            nError = LoadDownloadManifest(hs, DlHeader, pbDownloadFile, pbDownloadFile + cbDownloadFile); 
        }
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
}

static int LoadBuildManifest(TCascStorage * hs, DWORD dwLocaleMask)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    PDWORD FileSignature;
    LPBYTE pbRootFile = NULL;
    DWORD cbRootFile = 0;
    int nError = ERROR_SUCCESS;

    // Sanity checks
    assert(hs->CKeyMap.IsInitialized() == true);
    assert(hs->pRootHandler == NULL);

    // Locale: The default parameter is 0 - in that case,
    // we assign the default locale, loaded from the .build.info file
    if(dwLocaleMask == 0)
        dwLocaleMask = hs->dwDefaultLocale;

    // Prioritize the VFS root over legacy ROOT file
    pCKeyEntry = hs->VfsRoot.EKeyCount ? &hs->VfsRoot : &hs->RootFile;

    // Load the entire ROOT file to memory
    pbRootFile = LoadInternalFileToMemory(hs, pCKeyEntry->CKey, CASC_OPEN_BY_CKEY, CASC_INVALID_SIZE, &cbRootFile);
    if(pbRootFile != NULL && cbRootFile != 0)
    {
        // Check the type of the ROOT file
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

            case CASC_WOW82_ROOT_SIGNATURE:
                nError = RootHandler_CreateWoW(hs, pbRootFile, cbRootFile, dwLocaleMask);
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
                        nError = RootHandler_CreateWoW(hs, pbRootFile, cbRootFile, dwLocaleMask);
                    }
                }
                break;
        }

        // Insert entries for files with well-known names. Their CKeys are in the BUILD file
        // See https://wowdev.wiki/TACT#Encoding_table for their list
        if(nError == ERROR_SUCCESS)
        {
            InsertNamedInternalFile(hs, "ROOT", hs->RootFile);
            InsertNamedInternalFile(hs, "INSTALL", hs->InstallFile);
            InsertNamedInternalFile(hs, "DOWNLOAD", hs->DownloadFile);
            InsertNamedInternalFile(hs, "SIZE", hs->SizeFile);
            InsertNamedInternalFile(hs, "ENCODING", hs->EncodingFile);
            InsertNamedInternalFile(hs, "PATCH", hs->PatchFile);
        }

        // Free the root file
        CASC_FREE(pbRootFile);
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
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

static bool GetStorageTags(TCascStorage * hs, void * pvStorageInfo, size_t cbStorageInfo, size_t * pcbLengthNeeded)
{
    PCASC_STORAGE_TAGS pTags;
    PCASC_TAG_ENTRY2 pTag;
    char * szNameBuffer;
    size_t cbMinLength;

    // Does the storage support tags?
    if(hs->TagsArray.IsInitialized() == false)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    // Calculate the length of the tags
    cbMinLength = FIELD_OFFSET(CASC_STORAGE_TAGS, Tags) + hs->TagsArray.ItemCount() * sizeof(CASC_STORAGE_TAG);
    szNameBuffer = (char *)pvStorageInfo + cbMinLength;

    // Also include the tag length
    for(size_t i = 0; i < hs->TagsArray.ItemCount(); i++)
    {
        pTag = (PCASC_TAG_ENTRY2)hs->TagsArray.ItemAt(i);
        cbMinLength = cbMinLength + pTag->NameLength + 1;
    }

    // Verify whether we have enough space in the buffer
    pTags = (PCASC_STORAGE_TAGS)ProbeOutputBuffer(pvStorageInfo, cbStorageInfo, cbMinLength, pcbLengthNeeded);
    if(pTags != NULL)
    {
        // Fill the output structure
        pTags->TagCount = hs->TagsArray.ItemCount();
        pTags->Reserved = 0;

        // Copy the tags
        for(size_t i = 0; i < hs->TagsArray.ItemCount(); i++)
        {
            // Get the source tag
            pTag = (PCASC_TAG_ENTRY2)hs->TagsArray.ItemAt(i);

            // Fill the target tag
            pTags->Tags[i].szTagName = szNameBuffer;
            pTags->Tags[i].TagNameLength = (DWORD)pTag->NameLength;
            pTags->Tags[i].TagValue = pTag->TagValue;

            // Copy the tag name
            strcpy(szNameBuffer, pTag->szTagName);
            szNameBuffer = szNameBuffer + pTag->NameLength + 1;
        }
    }

    return (pTags != NULL);
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

        // Free the arrays
        hs->EncryptionKeys.Free();
        hs->ExtraKeysList.Free();
        hs->EKeyArray.Free();
        hs->TagsArray.Free();

        // Free the maps
        hs->CKeyMap.Free();
        hs->EKeyMap.Free();

        // Free the manifest files
        FreeCascBlob(&hs->EncodingManifest);
        FreeCascBlob(&hs->DownloadManifest);

        // Close all data files
        for(i = 0; i < CASC_MAX_DATA_FILES; i++)
        {
            FileStream_Close(hs->DataFiles[i]);
            hs->DataFiles[i] = NULL;
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
    if((hs = CASC_ALLOC(TCascStorage, 1)) == NULL)
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

    // Load the ENCODING manifest
    if(nError == ERROR_SUCCESS)
    {
        nError = LoadEncodingManifest(hs);
    }

    if(nError == ERROR_SUCCESS)
    {
        // Also load the DOWNLOAD manifest. If succeeds, the storage supports tags
        if(LoadDownloadManifest(hs) == ERROR_SUCCESS)
            hs->dwFeatures |= CASC_FEATURE_TAGS;

        // Load the build manifest ("ROOT" file)
        nError = LoadBuildManifest(hs, dwLocaleMask);
    }

    // Load the encryption keys
    if (nError == ERROR_SUCCESS)
    {
        nError = CascLoadEncryptionKeys(hs);
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
    PDWORD PtrOutputValue;
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
            dwInfoValue = (DWORD)hs->EKeyArray.ItemCount();
            break;

        case CascStorageFeatures:
            dwInfoValue = hs->dwFeatures | hs->pRootHandler->GetFeatures();
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

        case CascStorageTags:
            return GetStorageTags(hs, pvStorageInfo, cbStorageInfo, pcbLengthNeeded);

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
    }

    //
    // Default: return a 32-bit unsigned value
    //

    PtrOutputValue = (PDWORD)ProbeOutputBuffer(pvStorageInfo, cbStorageInfo, sizeof(DWORD), pcbLengthNeeded);
    if(PtrOutputValue != NULL)
        PtrOutputValue[0] = dwInfoValue;
    return (PtrOutputValue != NULL);
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
