/*****************************************************************************/
/* CascRootFile_Diablo3.cpp               Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Support for loading Diablo 3 ROOT file                                    */
/* Note: D3 offsets refer to Diablo III.exe 2.2.0.30013 (32-bit)                  */
/* SHA1: e4f17eca8aad8dde70870bf932ac3f5b85f17a1f                            */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 04.03.15  1.00  Lad  The first version of CascRootFile_Diablo3.cpp        */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local structures

#define CASC_DIABLO3_SUBDIR_SIGNATURE   0xEAF1FE87

#define INVALID_SUBDIR (PCASC_DIABLO3_HASH_TABLE)-1

typedef struct _CASC_DIABLO3_ASSET_ENTRY
{
    BYTE EncodingKey[MD5_HASH_SIZE];    // 
    DWORD AssetId;                      // Asset ID
} CASC_DIABLO3_ASSET_ENTRY, *PCASC_DIABLO3_ASSET_ENTRY;

typedef struct _CASC_DIABLO3_ASSET_ENTRY2
{
    BYTE EncodingKey[MD5_HASH_SIZE];    // 
    DWORD AssetId;                      // Asset ID
    DWORD FileNumber;                   //
} CASC_DIABLO3_ASSET_ENTRY2, *PCASC_DIABLO3_ASSET_ENTRY2;

typedef struct _CASC_DIABLO3_NAME_ENTRY
{
    BYTE EncodingKey[MD5_HASH_SIZE];    // 
    char szFileName[1];                 // ASCIIZ file name (variable length)
} CASC_DIABLO3_NAME_ENTRY, *PCASC_DIABLO3_NAME_ENTRY;

// Root file entry for CASC storages without MNDX root file (World of Warcraft 6.0+)
// Does not match to the in-file structure of the root entry
typedef struct _CASC_DIABLO3_ROOT_ENTRY
{
    LPBYTE EncodingKey;                         // Pointer to the encoding key
    char * szFileName;                          // Pointer to the file name
    struct _CASC_DIABLO3_HASH_TABLE * pSubDir;  // Pointer to subdirectory (NULL = none, INVALID_SUBDIR = invalid)

} CASC_DIABLO3_ROOT_ENTRY, *PCASC_DIABLO3_ROOT_ENTRY;

// Macro for testing whether there is enough data available
#define CHECK_DATA_AVAILABLE(ptr, endptr, size)     \
    if((ptr + size) > endptr)                       \
        return ERROR_BAD_FORMAT

//-----------------------------------------------------------------------------
// Structure definitions for Diablo3 root file

// Definition of the hash table for CASC root items
typedef struct _CASC_DIABLO3_HASH_TABLE
{
    PCASC_DIABLO3_ROOT_ENTRY TablePtr;              // Pointer to the table
    LPBYTE Entries;                                 // Pointer to the named entries
    size_t ItemCount;                               // Current number of items
    size_t TableSize;                               // Total number of items

} CASC_DIABLO3_HASH_TABLE, *PCASC_DIABLO3_HASH_TABLE;

struct TRootFile_Diablo3 : public TRootFile
{
    PCASC_DIABLO3_HASH_TABLE pHashTable;            // Pointer to hash table with root entry
};

//-----------------------------------------------------------------------------
// Local functions

static DWORD VerifyNamedFileEntry(LPBYTE pbRootEntry, LPBYTE pbRootFileEnd)
{
    LPBYTE pbFileName = pbRootEntry + MD5_HASH_SIZE;

    // Find the end of the name
    while(pbFileName < pbRootFileEnd && pbFileName[0] != 0)
        pbFileName++;

    // Did we get past the end of the root file?
    if(pbFileName > pbRootFileEnd)
        return 0;
    pbFileName++;

    // Return the length of the structure
    return (DWORD)(pbFileName - pbRootEntry);
}

static PCASC_DIABLO3_ROOT_ENTRY FindHashTableEntry(
    PCASC_DIABLO3_HASH_TABLE pHashTable,
    const char * szFileName,
    size_t cbFileName)
{
    PCASC_DIABLO3_ROOT_ENTRY pRootEntry;
    ULONGLONG FileNameHash;
    uint32_t dwHashHigh = 0;
    uint32_t dwHashLow = 0;
    DWORD StartIndex;
    DWORD TableIndex;
    char szNormName[MAX_PATH+1];

    // Calculate the file name hash for the named entry
    NormalizeFileName_LowerSlash(szNormName, szFileName, MAX_PATH);
    hashlittle2(szFileName, cbFileName, &dwHashHigh, &dwHashLow);
    FileNameHash = ((ULONGLONG)dwHashHigh << 0x20) | dwHashLow;

    // Get the initial index
    TableIndex = StartIndex = (DWORD)(FileNameHash % pHashTable->TableSize);
    assert(pHashTable->ItemCount < pHashTable->TableSize);

    // Search the proper entry
    for(;;)
    {
        // Find the appropriate item
        pRootEntry = pHashTable->TablePtr + TableIndex;
        
        // Is that table item empty?        
        if(pRootEntry->szFileName == NULL)
            return pRootEntry;

        // Compare the name
        if(!_strnicmp(pRootEntry->szFileName, szFileName, cbFileName))
            return pRootEntry;

        // Move to the next entry
        TableIndex = (DWORD)((TableIndex + 1) % pHashTable->TableSize);
        if(TableIndex == StartIndex)
            return NULL;
    }
}

static int CreateHashTableInternal(PCASC_DIABLO3_HASH_TABLE * ppHashTable, LPBYTE pbRootFile, LPBYTE pbRootFileEnd, bool bCanHaveAssetId)
{
    PCASC_DIABLO3_NAME_ENTRY pNamedEntry;
    PCASC_DIABLO3_HASH_TABLE pHashTable;
    PCASC_DIABLO3_ROOT_ENTRY pRootEntry;
    DWORD NumberOfItems;
    DWORD TableSize;
    DWORD cbNamedEntry;
    DWORD cbToAllocate;
    DWORD cbRootEntries;

    // For subdir tables, there will be two more table.
    // We skip them and go to the named entries
    if(bCanHaveAssetId)
    {
        // Make sure we have enough data there
        if((pbRootFile + sizeof(DWORD)) > pbRootFileEnd)
            return ERROR_BAD_FORMAT;
        
        // Get the number of items
        NumberOfItems = *(PDWORD)pbRootFile;
        TableSize = NumberOfItems * (MD5_HASH_SIZE + sizeof(DWORD));
        pbRootFile += sizeof(DWORD);

        // Skip the table
        if((pbRootFile + TableSize) > pbRootFileEnd)
            return ERROR_BAD_FORMAT;
        pbRootFile += TableSize;

        // Make sure we have enough data there
        if((pbRootFile + sizeof(DWORD)) > pbRootFileEnd)
            return ERROR_BAD_FORMAT;
        
        // Get the number of items
        NumberOfItems = *(PDWORD)pbRootFile;
        TableSize = NumberOfItems * (MD5_HASH_SIZE + sizeof(DWORD) + sizeof(DWORD));
        pbRootFile += sizeof(DWORD);

        // Skip the table
        if((pbRootFile + TableSize) > pbRootFileEnd)
            return ERROR_BAD_FORMAT;
        pbRootFile += TableSize;
    }

    // Is there space for number of entries?
    if((pbRootFile + sizeof(DWORD)) > pbRootFileEnd)
        return ERROR_BAD_FORMAT;

    // Get the number of named entries
    NumberOfItems = *(PDWORD)pbRootFile;
    pbRootFile += sizeof(DWORD);

    // Allocate the hash table
    cbRootEntries = (DWORD)(pbRootFileEnd - pbRootFile);
    cbToAllocate = sizeof(CASC_DIABLO3_HASH_TABLE) +
                   (NumberOfItems * 3 / 2) * sizeof(CASC_DIABLO3_ROOT_ENTRY) +
                   cbRootEntries;

    pHashTable = (PCASC_DIABLO3_HASH_TABLE)CASC_ALLOC(BYTE, cbToAllocate);
    if(pHashTable == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Initialize the hash table
    memset(pHashTable, 0, cbToAllocate - cbRootEntries);
    pHashTable->TablePtr = (PCASC_DIABLO3_ROOT_ENTRY)(pHashTable + 1);
    pHashTable->TableSize = (NumberOfItems * 3 / 2);
    pHashTable->Entries = (LPBYTE)(pHashTable->TablePtr + pHashTable->TableSize);
    memcpy(pHashTable->Entries, pbRootFile, cbRootEntries);
    
    // We work on the entry buffer now
    pbRootFileEnd = pHashTable->Entries + cbRootEntries;
    pbRootFile = pHashTable->Entries;

    // Build the hash table from the linear entry buffer
    for(DWORD i = 0; i < NumberOfItems; i++)
    {
        // Verify the entry size
        cbNamedEntry = VerifyNamedFileEntry(pbRootFile, pbRootFileEnd);
        if(cbNamedEntry == 0)
            return ERROR_BAD_FORMAT;

        // Get the root file entry
        pNamedEntry = (PCASC_DIABLO3_NAME_ENTRY)pbRootFile;
        pbRootFile += cbNamedEntry;

        // Find the root entry which will contain the pointers
        pRootEntry = FindHashTableEntry(pHashTable, pNamedEntry->szFileName, cbNamedEntry - MD5_HASH_SIZE - 1);
        assert(pRootEntry != NULL);

        // If the entry is free, put the pointers there
        if(pRootEntry->szFileName == NULL)
        {
            pRootEntry->EncodingKey = pNamedEntry->EncodingKey;
            pRootEntry->szFileName = pNamedEntry->szFileName;
            pRootEntry->pSubDir = INVALID_SUBDIR;
        }
    }

    // Give the hash table to the caller
    if(ppHashTable != NULL)
        ppHashTable[0] = pHashTable;
    return ERROR_SUCCESS;
}

// D3: 01268430
static int CreateHashTable_Diablo3(PCASC_DIABLO3_HASH_TABLE * ppHashTable, LPBYTE pbRootFile, LPBYTE pbRootFileEnd)
{
    DWORD HeaderMagic;
    int nError;

    // Is there at least 4 bytes available?
    if((pbRootFile + sizeof(DWORD)) > pbRootFileEnd)
        return ERROR_BAD_FORMAT;
    
    // Get the signature
    HeaderMagic = *(PDWORD)pbRootFile;
    pbRootFile += sizeof(DWORD);

    // According to the signature, load the hash table
    switch(HeaderMagic)
    {
        case CASC_DIABLO3_ROOT_SIGNATURE:
            nError = CreateHashTableInternal(ppHashTable, pbRootFile, pbRootFileEnd, false);
            break;

        case CASC_DIABLO3_SUBDIR_SIGNATURE:
            nError = CreateHashTableInternal(ppHashTable, pbRootFile, pbRootFileEnd, true);
            break;

        default:
            nError = ERROR_NOT_SUPPORTED;
            break;
    }

    return nError;
}

static void FreeHashTable_Diablo3(PCASC_DIABLO3_HASH_TABLE pHashTable)
{
    PCASC_DIABLO3_ROOT_ENTRY pRootEntry;

    // Get the first root table entry
    if(pHashTable != NULL)
    {
        pRootEntry = pHashTable->TablePtr;

        // First, we need to free all sub-entries
        for(size_t i = 0; i < pHashTable->TableSize; i++, pRootEntry++)
        {
            if(pRootEntry->pSubDir != NULL && pRootEntry->pSubDir != INVALID_SUBDIR)
                FreeHashTable_Diablo3(pRootEntry->pSubDir);
        }

        // Free the hash table itself
        CASC_FREE(pHashTable);
    }
}

static LPBYTE LoadFileToMemory(TCascStorage * hs, LPBYTE pbEncodingKey, DWORD * pcbFileData)
{
    QUERY_KEY EncodingKey;
    LPBYTE pbFileData = NULL;
    HANDLE hFile;
    DWORD cbBytesRead = 0;
    DWORD cbFileData = 0;

    // Open the file by encoding key
    EncodingKey.pbData = pbEncodingKey;
    EncodingKey.cbData = MD5_HASH_SIZE;
    if(CascOpenFileByEncodingKey((HANDLE)hs, &EncodingKey, 0, &hFile))
    {
        // Retrieve the file size
        cbFileData = CascGetFileSize(hFile, NULL);
        if(cbFileData > 0)
        {
            pbFileData = CASC_ALLOC(BYTE, cbFileData);
            if(pbFileData != NULL)
            {
                CascReadFile(hFile, pbFileData, cbFileData, &cbBytesRead);
            }
        }

        // Close the file
        CascCloseFile(hFile);
    }

    // Give the file to the caller
    if(pcbFileData != NULL)
        pcbFileData[0] = cbBytesRead;
    return pbFileData;
}

static LPBYTE FillSearchStruct(
    TCascSearch * pSearch,
    PCASC_DIABLO3_ROOT_ENTRY pEntry1,
    PCASC_DIABLO3_ROOT_ENTRY pEntry2)
{
    size_t nLength1;
    size_t nLength2;

    // Check if we have enough space there
    nLength1 = strlen(pEntry1->szFileName);
    if((nLength1 + 1) >= sizeof(pSearch->szFileName))
        return NULL;

    // Fill the package name
    memcpy(pSearch->szFileName, pEntry1->szFileName, nLength1);
    pSearch->szFileName[nLength1++] = '\\';

    // Fill the file name
    nLength2 = strlen(pEntry2->szFileName);
    if((nLength1 + nLength2 + 1) >= sizeof(pSearch->szFileName))
        return NULL;
    
    // Copy the file name
    memcpy(pSearch->szFileName + nLength1, pEntry2->szFileName, nLength2);
    pSearch->szFileName[nLength1 + nLength2] = 0;

    // Return the encoding key
    return pEntry2->EncodingKey;
}

//-----------------------------------------------------------------------------
// Implementation of Diablo III root file

LPBYTE TRootFileD3_Search(TRootFile_Diablo3 * pRootFile, TCascSearch * pSearch, PDWORD /* PtrFileSize */, PDWORD /* PtrLocaleFlags */)
{
    PCASC_DIABLO3_HASH_TABLE pHashTable1 = pRootFile->pHashTable;
    PCASC_DIABLO3_HASH_TABLE pHashTable2;
    PCASC_DIABLO3_ROOT_ENTRY pEndEntry1 = pHashTable1->TablePtr + pHashTable1->TableSize;
    PCASC_DIABLO3_ROOT_ENTRY pEndEntry2;
    PCASC_DIABLO3_ROOT_ENTRY pEntry1 = pHashTable1->TablePtr + pSearch->IndexLevel1;
    PCASC_DIABLO3_ROOT_ENTRY pEntry2;

    // Keep searching as long as we find something
    while(pEntry1 < pEndEntry1)
    {
        // Is the subdirectory valid?
        pHashTable2 = pEntry1->pSubDir;
        if(pHashTable2 != NULL && pHashTable2 != INVALID_SUBDIR)
        {
            // Keep searching as long as we find something in table level 2
            pEndEntry2 = pHashTable2->TablePtr + pHashTable2->TableSize;
            pEntry2 = pHashTable2->TablePtr + pSearch->IndexLevel2;
            while(pEntry2 < pEndEntry2)
            {
                // If the subitem is valid, fill-in the search structure and return 
                if(pEntry2->szFileName != NULL)
                {
                    // Get the next search point
                    pSearch->IndexLevel1 = (size_t)(pEntry1 - pHashTable1->TablePtr);
                    pSearch->IndexLevel2 = (size_t)(pEntry2 - pHashTable2->TablePtr) + 1;

                    // Fill the search structure
                    return FillSearchStruct(pSearch, pEntry1, pEntry2);
                }

                // Move to the next item
                pEntry2++;
            }
        }

        // Move to the next level-1 item 
        pSearch->IndexLevel2 = 0;
        pEntry1++;
    }

    return NULL;
}

void TRootFileD3_EndSearch(TRootFile_Diablo3 * /* pRootFile */, TCascSearch * /* pSearch */)
{
    // Do nothing
}

LPBYTE TRootFileD3_GetKey(TRootFile_Diablo3 * pRootFile, const char * szFileName)
{
    PCASC_DIABLO3_ROOT_ENTRY pRootEntry1;
    PCASC_DIABLO3_ROOT_ENTRY pRootEntry2;
    const char * szAssetName = szFileName;
    const char * szAssetEnd;
    size_t cbSubDir;

    // Skip the first segment of the name
    szAssetEnd = szFileName;
    while(szAssetEnd[0] != 0 && szAssetEnd[0] != '\\' && szAssetEnd[0] != '/')
        szAssetEnd++;

    // The string must not end yet
    if(szAssetEnd[0] == 0)
        return NULL;

    // Find the root entry for the asset
    pRootEntry1 = FindHashTableEntry(pRootFile->pHashTable, szAssetName, (DWORD)(szAssetEnd - szAssetName));
    if(pRootEntry1 == NULL)
        return NULL;

    // Is there a sub-entry?
    if(pRootEntry1->pSubDir == NULL || pRootEntry1->pSubDir == INVALID_SUBDIR)
        return NULL;

    // Skip the slash
    szAssetEnd++;

    // Find the subkey in the subdir
    cbSubDir = strlen(szAssetEnd);
    pRootEntry2 = FindHashTableEntry(pRootEntry1->pSubDir, szAssetEnd, cbSubDir);
    if(pRootEntry2 == NULL)
        return NULL;

    // Return the encoding key
    return pRootEntry2->EncodingKey;
}

void TRootFileD3_Close(TRootFile_Diablo3 * pRootFile)
{
    if(pRootFile != NULL)
    {
        // Free the hash table, if any
        if(pRootFile->pHashTable != NULL)
            FreeHashTable_Diablo3(pRootFile->pHashTable);
        pRootFile->pHashTable = NULL;

        // Free the root file itself
        CASC_FREE(pRootFile);
    }
}

//-----------------------------------------------------------------------------
// Public functions

int RootFile_CreateDiablo3(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootFile_Diablo3 * pRootFile;
    LPBYTE pbRootFileEnd = pbRootFile + cbRootFile;
    int nError;

    // Allocate the root handler object
    pRootFile = CASC_ALLOC(TRootFile_Diablo3, 1);
    if(pRootFile == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill-in the handler functions
    memset(pRootFile, 0, sizeof(TRootFile_Diablo3));
    pRootFile->Search      = (ROOT_SEARCH)TRootFileD3_Search;
    pRootFile->EndSearch   = (ROOT_ENDSEARCH)TRootFileD3_EndSearch;
    pRootFile->GetKey      = (ROOT_GETKEY)TRootFileD3_GetKey;
    pRootFile->Close       = (ROOT_CLOSE)TRootFileD3_Close;

    // Fill-in the flags
    pRootFile->dwRootFlags |= ROOT_FLAG_HAS_NAMES;

    // Create the hash table with root directory
    nError = CreateHashTable_Diablo3(&pRootFile->pHashTable, pbRootFile, pbRootFileEnd);
    if(nError == ERROR_SUCCESS)
    {
        PCASC_DIABLO3_ROOT_ENTRY pRootEntry = pRootFile->pHashTable->TablePtr;
        LPBYTE pbFileData;
        DWORD cbFileData;

        // Parse all valid hash table items and load the appropriate subdirectory
        for(size_t i = 0; i < pRootFile->pHashTable->TableSize; i++, pRootEntry++)
        {
            // Is that hash table entry valid?
            if(pRootEntry->szFileName != NULL)
            {
                pbFileData = LoadFileToMemory(hs, pRootEntry->EncodingKey, &cbFileData);
                if(pbFileData != NULL)
                {
                    CreateHashTable_Diablo3(&pRootEntry->pSubDir, pbFileData, pbFileData + cbFileData);
                    CASC_FREE(pbFileData);
                }
            }
        }

        // Give the root file handler to the storage
        hs->pRootFile = pRootFile;
    }

    return nError;
}
