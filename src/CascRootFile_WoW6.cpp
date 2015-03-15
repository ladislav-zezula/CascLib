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

#define CASC_INITIAL_ROOT_TABLE_SIZE    0x00100000

#define ROOT_SEARCH_PHASE_INITIALIZING  0
#define ROOT_SEARCH_PHASE_LISTFILE      1
#define ROOT_SEARCH_PHASE_NAMELESS      2
#define ROOT_SEARCH_PHASE_FINISHED      2

// On-disk version of locale block
typedef struct _FILE_LOCALE_BLOCK
{
    DWORD NumberOfFiles;                        // Number of entries
    DWORD Flags;
    DWORD Locales;                              // File locale mask (CASC_LOCALE_XXX)

    // Followed by a block of 32-bit integers (count: NumberOfFiles)
    // Followed by the MD5 and file name hash (count: NumberOfFiles)

} FILE_LOCALE_BLOCK, *PFILE_LOCALE_BLOCK;

// On-disk version of root entry
typedef struct _FILE_ROOT_ENTRY
{
    DWORD EncodingKey[4];                       // MD5 of the file
    ULONGLONG FileNameHash;                     // Jenkins hash of the file name

} FILE_ROOT_ENTRY, *PFILE_ROOT_ENTRY;


typedef struct _ROOT_BLOCK_INFO
{
    PFILE_LOCALE_BLOCK pLocaleBlockHdr;         // Pointer to the locale block
    PDWORD pInt32Array;                         // Pointer to the array of 32-bit integers
    PFILE_ROOT_ENTRY pRootEntries;

} ROOT_BLOCK_INFO, *PROOT_BLOCK_INFO;

// Root file entry for CASC storages without MNDX root file (World of Warcraft 6.0+)
// Does not match to the in-file structure of the root entry
typedef struct _CASC_ROOT_ENTRY
{
    ULONGLONG FileNameHash;                         // Jenkins hash of the file name
    DWORD SumValue;                                 // Sum value
    DWORD Locales;                                  // Locale flags of the file
    DWORD EncodingKey[4];                           // File encoding key (MD5)

} CASC_ROOT_ENTRY, *PCASC_ROOT_ENTRY;

// Definition of the hash table for CASC root items
typedef struct _CASC_ROOT_HASH_TABLE
{
    PCASC_ROOT_ENTRY TablePtr;                      // Pointer to the table
    DWORD ItemCount;                                // Number of items currently in the table
    DWORD TableSize;                                // Total size of the root table

} CASC_ROOT_HASH_TABLE, *PCASC_ROOT_HASH_TABLE;

struct TRootFile_WoW6 : public TRootFile
{
    PCASC_ROOT_HASH_TABLE pHashTable;            // Pointer to hash table with root entry
};

//-----------------------------------------------------------------------------
// Local functions

// WoW6: 00413F61
static bool EnlargeHashTableIfMoreThan75PercentUsed(PCASC_ROOT_HASH_TABLE pRootTable, DWORD NewItemCount)
{
    // Don't relocate anything, just check
    assert((double)NewItemCount / (double)pRootTable->TableSize < .75);
    return true;
}

// WOW6: 00414402
// Finds an existing root table entry or a free one
static PCASC_ROOT_HASH_TABLE CascRootTable_FindFreeEntryWithEnlarge(
    PCASC_ROOT_HASH_TABLE pRootTable,
    PCASC_ROOT_ENTRY pNewEntry,
    PCASC_ROOT_ENTRY * ppEntry)
{
    PCASC_ROOT_ENTRY pEntry;
    DWORD TableIndex;

    // The table size must be a power of two
    assert((pRootTable->TableSize & (pRootTable->TableSize - 1)) == 0);

    // Make sure that number of occupied items is never bigger
    // than 75% of the table size
    if(!EnlargeHashTableIfMoreThan75PercentUsed(pRootTable, pRootTable->ItemCount + 1))
        return NULL;

    // Get the start index of the table
    TableIndex = (DWORD)(pNewEntry->FileNameHash) & (pRootTable->TableSize - 1);

    // If that entry is already occupied, move to a next entry
    for(;;)
    {
        // Check that entry if it's free or not
        pEntry = (PCASC_ROOT_ENTRY)pRootTable->TablePtr + TableIndex;
        if(pEntry->SumValue == 0)
            break;

        // Is the found entry equal to the existing one?
        if(pEntry->FileNameHash == pNewEntry->FileNameHash)
            break;

        // Move to the next entry
        TableIndex = (TableIndex + 1) & (pRootTable->TableSize - 1);
    }

    // Give the caller the found entry
    if(ppEntry != NULL)
        ppEntry[0] = pEntry;
    return pRootTable;
}

// WOW6: 004145D1
static PCASC_ROOT_HASH_TABLE CascRootTable_InsertTableEntry(
    PCASC_ROOT_HASH_TABLE pRootTable,
    PCASC_ROOT_ENTRY pNewEntry)
{
    PCASC_ROOT_ENTRY pEntry = NULL;

    // Find an existing entry or an empty one
    pRootTable = CascRootTable_FindFreeEntryWithEnlarge(pRootTable, pNewEntry, &pEntry);
    assert(pEntry != NULL);

    // If that entry is not used yet, fill it in
    if(pEntry->FileNameHash == 0)
    {
        *pEntry = *pNewEntry;
        pRootTable->ItemCount++;
    }

    return pRootTable;
}

// Also used in CascSearchFile
PCASC_ROOT_ENTRY FindRootEntry(PCASC_ROOT_HASH_TABLE pRootTable, const char * szFileName, DWORD * PtrTableIndex)
{
    PCASC_ROOT_ENTRY pRootEntry;
    ULONGLONG FileNameHash;
    DWORD TableIndex;
    uint32_t dwHashHigh = 0;
    uint32_t dwHashLow = 0;

    // Calculate the HASH value of the normalized file name
    hashlittle2(szFileName, strlen(szFileName), &dwHashHigh, &dwHashLow);
    FileNameHash = ((ULONGLONG)dwHashHigh << 0x20) | dwHashLow;

    // Get the first table index
    TableIndex = (DWORD)(FileNameHash & (pRootTable->TableSize - 1));
    assert(pRootTable->ItemCount < pRootTable->TableSize);

    // Search the proper entry
    for(;;)
    {
        // Does the has match?
        pRootEntry = (PCASC_ROOT_ENTRY)pRootTable->TablePtr + TableIndex;
        if(pRootEntry->FileNameHash == FileNameHash)
        {
            if(PtrTableIndex != NULL)
                PtrTableIndex[0] = TableIndex;
            return pRootEntry;
        }

        // If the entry is free, the file is not there
        if(pRootEntry->FileNameHash == 0 && pRootEntry->SumValue == 0)
            return NULL;

        // Move to the next entry
        TableIndex = (DWORD)((TableIndex + 1) & (pRootTable->TableSize - 1));
    }
}


LPBYTE VerifyLocaleBlock(PROOT_BLOCK_INFO pBlockInfo, LPBYTE pbFilePointer, LPBYTE pbFileEnd)
{
    // Validate the file locale block
    pBlockInfo->pLocaleBlockHdr = (PFILE_LOCALE_BLOCK)pbFilePointer;
    pbFilePointer = (LPBYTE)(pBlockInfo->pLocaleBlockHdr + 1);
    if(pbFilePointer > pbFileEnd)
        return NULL;

    // Validate the array of 32-bit integers
    pBlockInfo->pInt32Array = (PDWORD)pbFilePointer;
    pbFilePointer = (LPBYTE)(pBlockInfo->pInt32Array + pBlockInfo->pLocaleBlockHdr->NumberOfFiles);
    if(pbFilePointer > pbFileEnd)
        return NULL;

    // Validate the array of root entries
    pBlockInfo->pRootEntries = (PFILE_ROOT_ENTRY)pbFilePointer;
    pbFilePointer = (LPBYTE)(pBlockInfo->pRootEntries + pBlockInfo->pLocaleBlockHdr->NumberOfFiles);
    if(pbFilePointer > pbFileEnd)
        return NULL;

    // Return the position of the next block
    return pbFilePointer;
}

static int LoadWowRootFileLocales(
    TRootFile_WoW6 * pRootFile,
    LPBYTE pbRootFile,
    DWORD cbRootFile,
    DWORD dwLocaleMask,
    bool bLoadBlocksWithFlags80,
    BYTE HighestBitValue)
{
    CASC_ROOT_ENTRY NewRootEntry;
    ROOT_BLOCK_INFO BlockInfo;
    LPBYTE pbRootFileEnd = pbRootFile + cbRootFile;
    LPBYTE pbFilePointer;

    // Sanity check
    assert(pRootFile->pHashTable != NULL);

    // Now parse the root file
    for(pbFilePointer = pbRootFile; pbFilePointer <= pbRootFileEnd; )
    {
        // Validate the file locale block
        pbFilePointer = VerifyLocaleBlock(&BlockInfo, pbFilePointer, pbRootFileEnd);
        if(pbFilePointer == NULL)
            break;

        // WoW.exe (build 19116): Entries with flag 0x100 set are skipped
        if(BlockInfo.pLocaleBlockHdr->Flags & 0x100)
            continue;

        // WoW.exe (build 19116): Entries with flag 0x80 set are skipped if arg_4 is set to FALSE (which is by default)
        if(bLoadBlocksWithFlags80 == 0 && (BlockInfo.pLocaleBlockHdr->Flags & 0x80))
            continue;

        // WoW.exe (build 19116): Entries with (flags >> 0x1F) not equal to arg_8 are skipped
        if((BYTE)(BlockInfo.pLocaleBlockHdr->Flags >> 0x1F) != HighestBitValue)
            continue;

        // WoW.exe (build 19116): Locales other than defined mask are skipped too
        if((BlockInfo.pLocaleBlockHdr->Locales & dwLocaleMask) == 0)
            continue;

        // Reset the sum value
        NewRootEntry.SumValue = 0;

        // WoW.exe (build 19116): Blocks with zero files are skipped
        for(DWORD i = 0; i < BlockInfo.pLocaleBlockHdr->NumberOfFiles; i++)
        {
            // (004147A3) Prepare the CASC_ROOT_ENTRY structure
            NewRootEntry.FileNameHash = BlockInfo.pRootEntries[i].FileNameHash;
            NewRootEntry.SumValue = NewRootEntry.SumValue + BlockInfo.pInt32Array[i];
            NewRootEntry.Locales = BlockInfo.pLocaleBlockHdr->Locales;
            NewRootEntry.EncodingKey[0] = BlockInfo.pRootEntries[i].EncodingKey[0];
            NewRootEntry.EncodingKey[1] = BlockInfo.pRootEntries[i].EncodingKey[1];
            NewRootEntry.EncodingKey[2] = BlockInfo.pRootEntries[i].EncodingKey[2];
            NewRootEntry.EncodingKey[3] = BlockInfo.pRootEntries[i].EncodingKey[3];

            // Insert the root table item to the hash table
            pRootFile->pHashTable = CascRootTable_InsertTableEntry(pRootFile->pHashTable, &NewRootEntry);
            NewRootEntry.SumValue++;
        }
    }

    return 1;
}

// WoW.exe: 004146C7 (BuildManifest::Load)
static int LoadWowRootFileWithParams(
    TRootFile_WoW6 * pRootFile,
    LPBYTE pbRootFile,
    DWORD cbRootFile,
    DWORD dwLocaleBits,
    BYTE HighestBitValue)
{
    // Load the locale as-is
    LoadWowRootFileLocales(pRootFile, pbRootFile, cbRootFile, dwLocaleBits, false, HighestBitValue);

    // If we wanted enGB, we also load enUS for the missing files
    if(dwLocaleBits == CASC_LOCALE_ENGB)
        LoadWowRootFileLocales(pRootFile, pbRootFile, cbRootFile, CASC_LOCALE_ENUS, false, HighestBitValue);

    if(dwLocaleBits == CASC_LOCALE_PTPT)
        LoadWowRootFileLocales(pRootFile, pbRootFile, cbRootFile, CASC_LOCALE_PTBR, false, HighestBitValue);

    return ERROR_SUCCESS;
}

/*
    // Code from WoW.exe
    if(dwLocaleBits == CASC_LOCALE_DUAL_LANG)
    {
        // Is this english version of WoW?
        if(arg_4 == CASC_LOCALE_BIT_ENUS)
        {
            LoadWowRootFileLocales(hs, pbRootFile, cbRootFile, CASC_LOCALE_ENGB, false, HighestBitValue);
            LoadWowRootFileLocales(hs, pbRootFile, cbRootFile, CASC_LOCALE_ENUS, false, HighestBitValue);
            return ERROR_SUCCESS;
        }

        // Is this portuguese version of WoW?
        if(arg_4 == CASC_LOCALE_BIT_PTBR)
        {
            LoadWowRootFileLocales(hs, pbRootFile, cbRootFile, CASC_LOCALE_PTPT, false, HighestBitValue);
            LoadWowRootFileLocales(hs, pbRootFile, cbRootFile, CASC_LOCALE_PTBR, false, HighestBitValue);
        }
    }

    LoadWowRootFileLocales(hs, pbRootFile, cbRootFile, (1 << arg_4), false, HighestBitValue);
*/
/*
static bool VerifyRootEntry(TCascSearch * pSearch, PCASC_ROOT_ENTRY pRootEntry)
{
    PCASC_ENCODING_ENTRY pEncodingEntry;
    PCASC_INDEX_ENTRY pIndexEntry;
    TCascStorage * hs = pSearch->hs;
    QUERY_KEY QueryKey;

    // Now try to find that encoding key in the array of encoding keys
    QueryKey.pbData = (LPBYTE)pRootEntry->EncodingKey;
    QueryKey.cbData = MD5_HASH_SIZE;
    pEncodingEntry = FindEncodingEntry(hs, &QueryKey, NULL);
    if(pEncodingEntry == NULL)
        return false;

    // Now try to find the index entry. Note that we take the first key
    QueryKey.pbData = GET_INDEX_KEY(pEncodingEntry);
    QueryKey.cbData = MD5_HASH_SIZE;
    pIndexEntry = FindIndexEntry(hs, &QueryKey);
    if(pIndexEntry == NULL)
        return false;

    return true;
}
*/
//-----------------------------------------------------------------------------
// Implementation of WoW6 root file

LPBYTE TRootFileWoW6_Search(TRootFile_WoW6 * pRootFile, TCascSearch * pSearch, PDWORD /* PtrFileSize */, PDWORD PtrLocaleFlags)
{
    PCASC_ROOT_ENTRY pRootEntry;
    LPBYTE RootBitArray = (LPBYTE)pSearch->pRootContext;
    char szNormName[MAX_PATH + 1];
    size_t cbToAllocate;
    DWORD TableIndex = 0;
    DWORD ByteIndex;
    DWORD BitMask;

    // Phase 0: Initializing
    if(pSearch->RootSearchPhase == ROOT_SEARCH_PHASE_INITIALIZING)
    {
        // Allocate byte array for checking which root entries were already reported
        if(RootBitArray == NULL)
        {
            // Allocate the root array
            cbToAllocate = (pRootFile->pHashTable->TableSize / 8);
            RootBitArray = CASC_ALLOC(BYTE, cbToAllocate);
            if(RootBitArray == NULL)
                return NULL;

            // Fill the root array with zeros
            memset(RootBitArray, 0, cbToAllocate);
            pSearch->pRootContext = RootBitArray;
        }

        // Move to the next phase
        assert(pSearch->IndexLevel1 == 0);
        pSearch->RootSearchPhase++;
    }

    // Phase 1: File name searching from listfile
    if(pSearch->RootSearchPhase == ROOT_SEARCH_PHASE_LISTFILE)
    {
        // Keep going through the listfile
        while(ListFile_GetNext(pSearch->pCache, pSearch->szMask, pSearch->szFileName, MAX_PATH))
        {
            // Normalize the file name found in the list file
            NormalizeFileName_UpperBkSlash(szNormName, pSearch->szFileName, MAX_PATH);

            // Find the root entry
            pRootEntry = FindRootEntry(pRootFile->pHashTable, szNormName, &TableIndex);
            if(pRootEntry != NULL)
            {
                // Remember that we already reported this root item
                ByteIndex = (DWORD)(TableIndex / 8);
                BitMask   = 1 << (TableIndex & 0x07);
                RootBitArray[ByteIndex] |= BitMask;
               
                // Give the caller the locale mask
                if(PtrLocaleFlags != NULL)
                    PtrLocaleFlags[0] = pRootEntry->Locales;
                return (LPBYTE)pRootEntry->EncodingKey;
            }
        }

        // Go to the next phase
        assert(pSearch->IndexLevel1 == 0);
        pSearch->RootSearchPhase++;
    }

    // Phase 2: Go through the root file again
    // and report all files that were not reported before
    if(pSearch->RootSearchPhase == ROOT_SEARCH_PHASE_NAMELESS)
    {
        // Go through the hash table again
        while(pSearch->IndexLevel1 < pRootFile->pHashTable->TableSize)
        {
            // Is that entry valid?
            pRootEntry = pRootFile->pHashTable->TablePtr + pSearch->IndexLevel1;
            if(pRootEntry->FileNameHash != 0)
            {
                // Was this root item already reported?
                ByteIndex = (DWORD)(pSearch->IndexLevel1 / 8);
                BitMask   = 1 << (pSearch->IndexLevel1 & 0x07);
                if((RootBitArray[ByteIndex] & BitMask) == 0)
                {
                    // Mark the entry as reported
                    RootBitArray[ByteIndex] |= BitMask;

                    // Give the values to the caller
                    if(PtrLocaleFlags != NULL)
                        PtrLocaleFlags[0] = pRootEntry->Locales;
                    return (LPBYTE)pRootEntry->EncodingKey;
                }
            }

            // Move to the next root entry
            pSearch->IndexLevel1++;
        }

        // Move to the next phase
        pSearch->RootSearchPhase++;
    }

    return NULL;
}

LPBYTE TRootFileWoW6_GetKey(TRootFile_WoW6 * pRootFile, const char * szFileName)
{
    PCASC_ROOT_ENTRY pRootEntry;
    char szNormName[MAX_PATH + 1];

    // Convert the file name to lowercase + slashes
    NormalizeFileName_UpperBkSlash(szNormName, szFileName, MAX_PATH);

    // Check the root directory for that hash
    pRootEntry = FindRootEntry(pRootFile->pHashTable, szNormName, NULL);
    if(pRootEntry == NULL)
        return NULL;

    return (LPBYTE)pRootEntry->EncodingKey;
}

void TRootFileWoW6_EndSearch(TRootFile_WoW6 * /* pRootFile */, TCascSearch * pSearch)
{
    if(pSearch->pRootContext != NULL)
        CASC_FREE(pSearch->pRootContext);
    pSearch->pRootContext = NULL;
}

void TRootFileWoW6_Close(TRootFile_WoW6 * pRootFile)
{
    if(pRootFile != NULL)
    {
        // Free the hash table, if any
        if(pRootFile->pHashTable != NULL)
            CASC_FREE(pRootFile->pHashTable);
        pRootFile->pHashTable = NULL;

        // Free the root file itself
        CASC_FREE(pRootFile);
    }
}


//-----------------------------------------------------------------------------
// Public functions

int RootFile_CreateWoW6(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask)
{
    PCASC_ROOT_HASH_TABLE pHashTable;
    TRootFile_WoW6 * pRootFile;
    size_t cbToAllocate;
    int nError;

    // Dump the root file, if needed
#ifdef CASC_DUMP_ROOT_FILE
    //CascDumpRootFile(hs,
    //                 pbRootFile,
    //                 cbRootFile,
    //                 "\\casc_root_%build%.txt",
    //                 _T("\\Ladik\\Appdir\\CascLib\\listfile\\listfile-wow6.txt"),
    //                 CASC_DUMP_ROOT_FILE);
#endif

    // Verify the size
    if(pbRootFile == NULL || cbRootFile <= sizeof(PFILE_LOCALE_BLOCK))
        nError = ERROR_FILE_CORRUPT;

    // Allocate the root handler object
    pRootFile = CASC_ALLOC(TRootFile_WoW6, 1);
    if(pRootFile == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill-in the handler functions
    memset(pRootFile, 0, sizeof(TRootFile_WoW6));
    pRootFile->Search      = (ROOT_SEARCH)TRootFileWoW6_Search;
    pRootFile->EndSearch   = (ROOT_ENDSEARCH)TRootFileWoW6_EndSearch;
    pRootFile->GetKey      = (ROOT_GETKEY)TRootFileWoW6_GetKey;
    pRootFile->Close       = (ROOT_CLOSE)TRootFileWoW6_Close;

    // Allocate root table entries. Note that the initial size
    // of the root table is set to 0x00200000 by World of Warcraft 6.x
    cbToAllocate = sizeof(CASC_ROOT_HASH_TABLE) + (CASC_INITIAL_ROOT_TABLE_SIZE * sizeof(CASC_ROOT_ENTRY));
    pHashTable = (PCASC_ROOT_HASH_TABLE)CASC_ALLOC(BYTE, cbToAllocate);
    if(pHashTable != NULL)
    {
        // Initialize the root table
        memset(pHashTable, 0, cbToAllocate);
        pHashTable->TableSize = CASC_INITIAL_ROOT_TABLE_SIZE;
        pHashTable->TablePtr = (PCASC_ROOT_ENTRY)(pHashTable + 1);
        pRootFile->pHashTable = pHashTable;

        // Load the root file
        nError = LoadWowRootFileWithParams(pRootFile, pbRootFile, cbRootFile, dwLocaleMask, 0); 
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = LoadWowRootFileWithParams(pRootFile, pbRootFile, cbRootFile, dwLocaleMask, 1); 
        if(nError != ERROR_SUCCESS)
            return nError;

        // Give the root file handler to the storage
        hs->pRootFile = pRootFile;
        return ERROR_SUCCESS;
    }

    return ERROR_NOT_ENOUGH_MEMORY;
}
