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

static int CreateCKeyMaps(TCascStorage * hs, CASC_ENCODING_HEADER & EnHeader)
{
    size_t EstimatedEntryCount = (EnHeader.CKeyPageCount * EnHeader.CKeyPageSize) / sizeof(FILE_CKEY_ENTRY);
    int nError;

    // Create the array of CKey items
    nError = hs->CKeyArray.Create(sizeof(CASC_CKEY_ENTRY), EstimatedEntryCount);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Create the map CKey -> CASC_CKEY_ENTRY
    nError = hs->CKeyMap.Create(EstimatedEntryCount, EnHeader.CKeyLength, FIELD_OFFSET(CASC_CKEY_ENTRY, CKey));
    if(nError != ERROR_SUCCESS)
        return nError;

    // Create the map EKey -> CASC_CKEY_ENTRY
    nError = hs->EKeyMap.Create(EstimatedEntryCount, hs->InHeader.EKeyLength, FIELD_OFFSET(CASC_CKEY_ENTRY, EKey));
    if(nError != ERROR_SUCCESS)
        return nError;

    return ERROR_SUCCESS;
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

static bool CaptureEKeyEntry(CASC_INDEX_HEADER & InHeader, PCASC_CKEY_ENTRY pCKeyEntry, LPBYTE pbEKeyEntry)
{
    // Zero the CKey item
    ZeroMemory16(pCKeyEntry->CKey);

    // Copy the EKey. We assume 9 bytes
    pCKeyEntry->EKey[0x00] = pbEKeyEntry[0];
    pCKeyEntry->EKey[0x01] = pbEKeyEntry[1];
    pCKeyEntry->EKey[0x02] = pbEKeyEntry[2];
    pCKeyEntry->EKey[0x03] = pbEKeyEntry[3];
    pCKeyEntry->EKey[0x04] = pbEKeyEntry[4];
    pCKeyEntry->EKey[0x05] = pbEKeyEntry[5];
    pCKeyEntry->EKey[0x06] = pbEKeyEntry[6];
    pCKeyEntry->EKey[0x07] = pbEKeyEntry[7];
    pCKeyEntry->EKey[0x08] = pbEKeyEntry[8];
    pCKeyEntry->EKey[0x09] = pbEKeyEntry[9];
    pCKeyEntry->EKey[0x0A] = 0;
    pCKeyEntry->EKey[0x0B] = 0;
    pCKeyEntry->EKey[0x0C] = 0;
    pCKeyEntry->EKey[0x0D] = 0;
    pCKeyEntry->EKey[0x0E] = 0;
    pCKeyEntry->EKey[0x0F] = 0;
    pbEKeyEntry += InHeader.EKeyLength;

    // Copy the storage offset
    pCKeyEntry->StorageOffset = ConvertBytesToInteger_5(pbEKeyEntry);
    pbEKeyEntry += InHeader.StorageOffsetLength;

    // Clear the tag bit mask
    pCKeyEntry->TagBitMask = 0;

    // Copy the encoded length
    pCKeyEntry->EncodedSize = ConvertBytesToInteger_4_LE(pbEKeyEntry);
    pCKeyEntry->ContentSize = CASC_INVALID_SIZE;
    pCKeyEntry->RefCount = 0;
    pCKeyEntry->Priority = 0;
    pCKeyEntry->Flags = CASC_CE_FILE_IS_LOCAL | CASC_CE_HAS_EKEY;
    return true;
}

static void CheckForEncodingManifestCKey(TCascStorage * hs, PCASC_CKEY_ENTRY pCKeyEntry)
{
    // If the encoding file was not found yet
    if((hs->EncodingCKey.Flags & CASC_CE_FILE_IS_LOCAL) == 0)
    {
        if(!memcmp(pCKeyEntry->EKey, hs->EncodingCKey.EKey, hs->InHeader.EKeyLength))
        {
            hs->EncodingCKey.StorageOffset = pCKeyEntry->StorageOffset;
            hs->EncodingCKey.EncodedSize = pCKeyEntry->EncodedSize;
            hs->EncodingCKey.Flags |= CASC_CE_FILE_IS_LOCAL;
        }
    }
}

static int LoadEKeyItems(TCascStorage * hs, LPBYTE pbEKeyEntry, LPBYTE pbEKeyEnd)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    size_t EntryLength = hs->InHeader.EntryLength;

    while((pbEKeyEntry + EntryLength) <= pbEKeyEnd)
    {
        // Insert new entry to the array of CKey entries
        pCKeyEntry = (PCASC_CKEY_ENTRY)hs->IndexArray.Insert(NULL, 1);
        if(pCKeyEntry == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Capture the EKey entry
        if(!CaptureEKeyEntry(hs->InHeader, pCKeyEntry, pbEKeyEntry))
            break;

        // Verify whether the key is not a CKEy entry for ENCODING file
        CheckForEncodingManifestCKey(hs, pCKeyEntry);
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
        PCASC_CKEY_ENTRY pCKeyEntry;
        LPBYTE pbStartPage = pbFileData + 0x1000;
        LPBYTE pbEndPage = pbStartPage + FILE_INDEX_PAGE_SIZE;
        size_t AlignedLength = ALIGN_TO_SIZE(InHeader.EntryLength, 4);

        // Parse the chunks with the EKey entries
        while(pbStartPage < pbFileEnd)
        {
            pbEKeyEntry = pbStartPage;
            
            while(pbEKeyEntry < pbEndPage)
            {
                // Check the EKey entry protected by 32-bit hash
                if((pbEKeyEntry = CaptureGuardedBlock3(pbEKeyEntry, pbEndPage, InHeader.EntryLength)) == NULL)
                    break;

                // Insert the EKey entry to the array
                pCKeyEntry = (PCASC_CKEY_ENTRY)hs->IndexArray.Insert(NULL, 1);
                if(pCKeyEntry == NULL)
                    return ERROR_NOT_ENOUGH_MEMORY;

                // Capture the EKey entry
                if(!CaptureEKeyEntry(InHeader, pCKeyEntry, pbEKeyEntry))
                    break;

                // Check whether the CKey entry is an encoding entry
                CheckForEncodingManifestCKey(hs, pCKeyEntry);
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
    int nError = ERROR_SUCCESS;

    // WoW6 actually reads THE ENTIRE file to memory. Verified on Mac build (x64).
    pbFileData = LoadExternalFileToMemory(szFileName, &cbFileData);
    if(pbFileData && cbFileData)
    {
        // Parse and load the index file
        nError = LoadIndexFile(hs, pbFileData, cbFileData, BucketIndex);
        CASC_FREE(pbFileData);
    }
    else
    {
        nError = GetLastError();
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
        // Initialize the array of index files
        if((nError = hs->IndexArray.Create(sizeof(CASC_CKEY_ENTRY), 0x200000)) == ERROR_SUCCESS)
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

            // Remember the number of files that are present locally
            hs->LocalFiles = hs->IndexArray.ItemCount();
        }
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

static int LoadCKeyPage(TCascStorage * hs, CASC_ENCODING_HEADER & EnHeader, LPBYTE pbPageBegin, LPBYTE pbEndOfPage)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    PFILE_CKEY_ENTRY pFileEntry;
    LPBYTE pbFileEntry = pbPageBegin;

    // Sanity checks
    assert(hs->CKeyMap.IsInitialized());
    assert(hs->EKeyMap.IsInitialized());

    // Parse all encoding entries
    while(pbFileEntry < pbEndOfPage)
    {
        // Get pointer to the encoding entry
        pFileEntry = (PFILE_CKEY_ENTRY)pbFileEntry;
        if(pFileEntry->EKeyCount == 0)
            break;

        // Example of a file entry with multiple EKeys: 
        // Overwatch build 24919, CKey: 0e 90 94 fa d2 cb 85 ac d0 7c ea 09 f9 c5 ba 00 
//      if(pFileEntry->EKeyCount > 1)
//          __debugbreak();

        // Insert the CKey entry into the array
        pCKeyEntry = (PCASC_CKEY_ENTRY)hs->CKeyArray.Insert(NULL, 1);
        if(pCKeyEntry != NULL)
        {
            // Supply both CKey and EKey. Rewrite EKey regardless, because ENCODING manifest contains a full one
            CopyMemory16(pCKeyEntry->CKey, pFileEntry->CKey);
            CopyMemory16(pCKeyEntry->EKey, pFileEntry->EKey);
            pCKeyEntry->StorageOffset = 0;
            pCKeyEntry->TagBitMask = 0;
            pCKeyEntry->EncodedSize = CASC_INVALID_SIZE;
            pCKeyEntry->ContentSize = ConvertBytesToInteger_4(pFileEntry->ContentSize);
            pCKeyEntry->RefCount = 0;
            pCKeyEntry->Priority = 0;
            pCKeyEntry->Flags = (CASC_CE_HAS_CKEY | CASC_CE_HAS_EKEY | CASC_CE_IN_ENCODING);

            // Insert the item into both maps
            hs->CKeyMap.InsertObject(pCKeyEntry, pCKeyEntry->CKey);
            hs->EKeyMap.InsertObject(pCKeyEntry, pCKeyEntry->EKey);
        }

        // Move to the next encoding entry
        pbFileEntry = pbFileEntry + 2 + 4 + EnHeader.CKeyLength + (pFileEntry->EKeyCount * EnHeader.EKeyLength);
    }
    return ERROR_SUCCESS;
}

static int CopyIndexItemsToCKeyArray(TCascStorage * hs)
{
    PCASC_CKEY_ENTRY pCKeyEntry1;
    PCASC_CKEY_ENTRY pCKeyEntry2;
    size_t nItemCount = hs->IndexArray.ItemCount();

    // Iterate over all index items
    for(size_t i = 0; i < nItemCount; i++)
    {
        // Get both items
        pCKeyEntry1 = (PCASC_CKEY_ENTRY)hs->IndexArray.ItemAt(i);
        pCKeyEntry2 = FindCKeyEntry_EKey(hs, pCKeyEntry1->EKey);

        // If the item is in the map, we supply the stuff from the index entry
        if(pCKeyEntry2 != NULL)
        {
            pCKeyEntry2->StorageOffset = pCKeyEntry1->StorageOffset;
            pCKeyEntry2->EncodedSize = pCKeyEntry1->EncodedSize;
            pCKeyEntry2->Flags |= CASC_CE_FILE_IS_LOCAL;
        }
    }

    // We free the index array at this point
    hs->IndexArray.Free();
    return ERROR_SUCCESS;
}

static int LoadEncodingManifest(TCascStorage * hs)
{
    LPBYTE pbEncodingFile;
    DWORD cbEncodingFile = 0;
    int nError = ERROR_SUCCESS;

    // Load the entire encoding file to memory
    pbEncodingFile = LoadInternalFileToMemory(hs, &hs->EncodingCKey, &cbEncodingFile);
    if(pbEncodingFile != NULL && cbEncodingFile != 0)
    {
        CASC_ENCODING_HEADER EnHeader;

        // Capture the header of the ENCODING file
        nError = CaptureEncodingHeader(EnHeader, pbEncodingFile, cbEncodingFile);
        if(nError == ERROR_SUCCESS)
        {
            // Get the CKey page header and the first page
            PFILE_CKEY_PAGE pPageHeader = (PFILE_CKEY_PAGE)(pbEncodingFile + sizeof(FILE_ENCODING_HEADER) + EnHeader.ESpecBlockSize);
            LPBYTE pbCKeyPage = (LPBYTE)(pPageHeader + EnHeader.CKeyPageCount);

            // Since ENCODING contains the full list of all files (even those not downloaded),
            // we can now make a fair estimate about how large maps shall we create.
            // So, we can build the maps CKey and EKey map.
            if((nError = CreateCKeyMaps(hs, EnHeader)) == ERROR_SUCCESS)
            {
                // Copy the encoding header to the storage structure
                hs->EnHeader = EnHeader;

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
                    nError = LoadCKeyPage(hs, EnHeader, pbCKeyPage, pbCKeyPage + EnHeader.CKeyPageSize);
                    if(nError != ERROR_SUCCESS)
                        break;

                    // Move to the next CKey page
                    pbCKeyPage += EnHeader.CKeyPageSize;
                }
            }
        }

        // Now supply all the entries from the index files
        if(nError == ERROR_SUCCESS)
        {
            nError = CopyIndexItemsToCKeyArray(hs);
        }

        // Free the loaded ENCODING file
        CASC_FREE(pbEncodingFile);
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
}

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
    DlEntry.EncodedSize = ConvertBytesToInteger_5(pbFilePtr);
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
    PCASC_TAG_ENTRY1 TagArray = NULL;
    LPBYTE pbEntries = pbFileData + DlHeader.HeaderLength;
    LPBYTE pbEntry = pbEntries;
    LPBYTE pbTags = pbEntries + DlHeader.EntryLength * DlHeader.EntryCount;
    LPBYTE pbTag = pbTags;
    size_t nMaxNameLength = 0;
    size_t nTagEntryLengh = 0;
    int nError = ERROR_NOT_ENOUGH_MEMORY;

    // Does the storage support tags?
    if(DlHeader.TagCount != 0)
    {
        // Remember that we support tags
        hs->dwFeatures |= CASC_FEATURE_TAGS;

        // Allocate space for the tag array
        TagArray = CASC_ALLOC(CASC_TAG_ENTRY1, DlHeader.TagCount);
        if(TagArray != NULL)
        {
//          FILE * fp;
//          char filename[MAX_PATH];

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
            }
        }
    }

    // Now parse all entries. For each entry, mark the corresponding tag bit in the EKey table
    for(DWORD i = 0; i < DlHeader.EntryCount; i++)
    {
        CASC_DOWNLOAD_ENTRY DlEntry;
        PCASC_CKEY_ENTRY pCKeyEntry;
        size_t BitMaskOffset = (i / 8);
        BYTE BitMaskBit = 0x80 >> (i % 8);

        // Capture the download entry
        if(CaptureDownloadEntry(DlHeader, DlEntry, pbEntry, pbFileEnd) != ERROR_SUCCESS)
            break;

        // Make sure we have the entry in CKey table
        pCKeyEntry = FindCKeyEntry_EKey(hs, DlEntry.EKey);
        if(pCKeyEntry != NULL)
        {
            ULONGLONG TagBit = 1;
            size_t TagItemCount = hs->TagsArray.ItemCount();

            // Supply the tag bits
            for(size_t j = 0; j < TagItemCount; j++)
            {
                // Set the bit in the entry, if the tag for it is present
                if((BitMaskOffset < TagArray[j].BitmapLength) && (TagArray[j].Bitmap[BitMaskOffset] & BitMaskBit))
                    pCKeyEntry->TagBitMask |= TagBit;

                // Move to the next bit
                TagBit <<= 1;
            }

            // Supply the priority
            pCKeyEntry->Priority = DlEntry.Priority;
            pCKeyEntry->Flags |= CASC_CE_IN_DOWNLOAD;
        }

        // Move to the next entry
        pbEntry += DlHeader.EntryLength;
    }

    // Free the tag array, if any
    if(TagArray != NULL)
        CASC_FREE(TagArray);
    TagArray = NULL;

    // Remember the total file count
    hs->TotalFiles = hs->CKeyArray.ItemCount();
    return nError;
}

static int LoadDownloadManifest(TCascStorage * hs)
{
    PCASC_CKEY_ENTRY pCKeyEntry = FindCKeyEntry_CKey(hs, hs->DownloadCKey.CKey);
    LPBYTE pbDownloadFile = NULL;
    DWORD cbDownloadFile = 0;
    int nError = ERROR_SUCCESS;

    // Load the entire DOWNLOAD file to memory
    pbDownloadFile = LoadInternalFileToMemory(hs, pCKeyEntry, &cbDownloadFile);
    if(pbDownloadFile != NULL && cbDownloadFile != 0)
    {
        CASC_DOWNLOAD_HEADER DlHeader;

        // Capture the header of the DOWNLOAD file
        nError = CaptureDownloadHeader(DlHeader, pbDownloadFile, cbDownloadFile);
        if(nError == ERROR_SUCCESS)
        {
            // Remember the DOWNLOAD header in the storage structure
            hs->DlHeader = DlHeader;

            // Parse the entire download manifest
            nError = LoadDownloadManifest(hs, DlHeader, pbDownloadFile, pbDownloadFile + cbDownloadFile); 
        }

        // Free the loaded manifest
        CASC_FREE(pbDownloadFile);
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
}

/*
int CaptureInstallHeader(CASC_INSTALL_HEADER & InHeader, LPBYTE pbFileData, size_t cbFileData)
{
    PFILE_INSTALL_HEADER pFileHeader = (PFILE_INSTALL_HEADER)pbFileData;

    // Check the signature ('DL') and version
    if(cbFileData < sizeof(FILE_INSTALL_HEADER) || pFileHeader->Magic != FILE_MAGIC_INSTALL || pFileHeader->Version != 1)
        return ERROR_BAD_FORMAT;

    // Note that we don't support CKey sizes greater than 0x10 in the INSTALL file
    if(pFileHeader->EKeyLength > MD5_HASH_SIZE)
        return ERROR_BAD_FORMAT;

    // Capture the header version 1
    memset(&InHeader, 0, sizeof(CASC_INSTALL_HEADER));
    InHeader.Magic = pFileHeader->Magic;
    InHeader.Version = pFileHeader->Version;
    InHeader.EKeyLength = pFileHeader->EKeyLength;
    InHeader.TagCount = ConvertBytesToInteger_2(pFileHeader->TagCount);
    InHeader.EntryCount = ConvertBytesToInteger_4(pFileHeader->EntryCount);
    InHeader.HeaderLength = sizeof(FILE_INSTALL_HEADER);
    return ERROR_SUCCESS;
}

static int LoadInstallManifest(TCascStorage * hs)
{
    LPBYTE pbInstallFile = NULL;
    DWORD cbInstallFile = 0;
    int nError = ERROR_SUCCESS;

    // Load the entire INSTALL file to memory
    pbInstallFile = LoadInternalFileToMemory(hs, hs->InstallFile.CKey, CASC_OPEN_BY_CKEY, CASC_INVALID_SIZE, &cbInstallFile);
    if(pbInstallFile != NULL && cbInstallFile != 0)
    {
        CASC_INSTALL_HEADER InHeader;

        // Capture the header of the DOWNLOAD file
        nError = CaptureInstallHeader(InHeader, pbInstallFile, cbInstallFile);
        if(nError == ERROR_SUCCESS)
        {
            // Parse the entire install manifest
//          nError = LoadInstallManifest(hs, InHeader, pbInstallFile, pbInstallFile + cbInstallFile); 
        }
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
}
*/

static bool InsertWellKnownFile(TCascStorage * hs, const char * szFileName, CASC_CKEY_ENTRY & FakeCKeyEntry)
{
    PCASC_CKEY_ENTRY pCKeyEntry = NULL;

    // At least something must be there
    if(FakeCKeyEntry.Flags & (CASC_CE_HAS_CKEY | CASC_CE_HAS_EKEY))
    {
        // We need to find the EKey entry in the central array
        if((pCKeyEntry == NULL) && (FakeCKeyEntry.Flags & CASC_CE_HAS_CKEY))
            pCKeyEntry = FindCKeyEntry_CKey(hs, FakeCKeyEntry.CKey);
        if((pCKeyEntry == NULL) && (FakeCKeyEntry.Flags & CASC_CE_HAS_EKEY))
            pCKeyEntry = FindCKeyEntry_EKey(hs, FakeCKeyEntry.EKey);

        // Did we find anything?
        if(pCKeyEntry != NULL)
        {
            // Supply the CKey, if not present in the central storage entry
            if((pCKeyEntry->Flags & CASC_CE_HAS_CKEY) == 0 && (FakeCKeyEntry.Flags & CASC_CE_HAS_CKEY) != 0)
            {
                CopyMemory16(pCKeyEntry->CKey, FakeCKeyEntry.CKey);
                pCKeyEntry->Flags |= CASC_CE_HAS_CKEY;
            }

            // Supply the encoded size
            if(pCKeyEntry->EncodedSize == CASC_INVALID_SIZE)
                pCKeyEntry->EncodedSize = FakeCKeyEntry.EncodedSize;

            // Insert the key to the root handler, unless it's already referenced by name
            if(pCKeyEntry->RefCount == 0)
                hs->pRootHandler->Insert(szFileName, pCKeyEntry);
            return true;
        }
    }

    return false;
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
    pCKeyEntry = (hs->VfsRoot.ContentSize != CASC_INVALID_SIZE) ? &hs->VfsRoot : &hs->RootFile;
    pCKeyEntry = FindCKeyEntry_CKey(hs, pCKeyEntry->CKey);

    // Load the entire ROOT file to memory
    pbRootFile = LoadInternalFileToMemory(hs, pCKeyEntry, &cbRootFile);
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
            InsertWellKnownFile(hs, "ENCODING", hs->EncodingCKey);
            InsertWellKnownFile(hs, "DOWNLOAD", hs->DownloadCKey);
            InsertWellKnownFile(hs, "INSTALL", hs->InstallFile);
            InsertWellKnownFile(hs, "PATCH", hs->PatchFile);
            InsertWellKnownFile(hs, "ROOT", hs->RootFile);
            InsertWellKnownFile(hs, "SIZE", hs->SizeFile);
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
        hs->IndexArray.Free();
        hs->TagsArray.Free();
        hs->CKeyArray.Free();

        // Free the maps
        hs->CKeyMap.Free();
        hs->EKeyMap.Free();

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

    // Load the index files. Store information from the index files to the CKeyArray.
    if(nError == ERROR_SUCCESS)
    {
        nError = LoadIndexFiles(hs);
    }

    // Load the ENCODING manifest
    if(nError == ERROR_SUCCESS)
    {
        nError = LoadEncodingManifest(hs);
    }

    // We need to load the DOWNLOAD manifest. This will give us the information about
    // how many physical files are in the storage, so we can start building file tables
    if(nError == ERROR_SUCCESS)
    {
        nError = LoadDownloadManifest(hs);
    }

    // Load the build manifest ("ROOT" file)
    if(nError == ERROR_SUCCESS)
    {
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
        case CascStorageLocalFileCount:
            dwInfoValue = (DWORD)hs->LocalFiles;
            break;

        case CascStorageTotalFileCount:
            dwInfoValue = (DWORD)hs->TotalFiles;
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
