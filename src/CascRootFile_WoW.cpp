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
#include "CascRootFile_WoW.h"

//-----------------------------------------------------------------------------
// Local structures

#define CASC_INITIAL_ROOT_TABLE_SIZE    0x00100000

//-----------------------------------------------------------------------------
// Local variables

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
static PCASC_ROOT_ENTRY CascRootTable_FindFreeEntryWithEnlarge(
    PCASC_ROOT_HASH_TABLE pRootTable,
    PCASC_ROOT_ENTRY pNewEntry)
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
        pEntry = pRootTable->TablePtr + TableIndex;
        if(pEntry->SumValue == 0)
            break;

        // Is the found entry equal to the existing one?
        if(pEntry->FileNameHash == pNewEntry->FileNameHash)
            break;

        // Move to the next entry
        TableIndex = (TableIndex + 1) & (pRootTable->TableSize - 1);
    }

    // Either return a free entry or an existing one
    return pEntry;
}

// WOW6: 004145D1
static void CascRootTable_InsertTableEntry(
    PCASC_ROOT_HASH_TABLE pRootTable,
    PCASC_ROOT_ENTRY pNewEntry)
{
    PCASC_ROOT_ENTRY pEntry;

    // Find an existing entry or an empty one
    pEntry = CascRootTable_FindFreeEntryWithEnlarge(pRootTable, pNewEntry);
    assert(pEntry != NULL);

    // If that entry is not used yet, fill it in
    if(pEntry->FileNameHash == 0)
    {
        *pEntry = *pNewEntry;
        pRootTable->ItemCount++;
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
    TCascStorage * hs,
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
            CascRootTable_InsertTableEntry(&hs->RootTable, &NewRootEntry);
            NewRootEntry.SumValue++;
        }
    }

    return 1;
}

// WoW.exe: 004146C7 (BuildManifest::Load)
static int LoadWowRootFileWithParams(
    TCascStorage * hs,
    LPBYTE pbRootFile,
    DWORD cbRootFile,
    DWORD dwLocaleBits,
    BYTE HighestBitValue)
{
    // Load the locale as-is
    LoadWowRootFileLocales(hs, pbRootFile, cbRootFile, dwLocaleBits, false, HighestBitValue);

    // If we wanted enGB, we also load enUS for the missing files
    if(dwLocaleBits == CASC_LOCALE_ENGB)
        LoadWowRootFileLocales(hs, pbRootFile, cbRootFile, CASC_LOCALE_ENUS, false, HighestBitValue);

    if(dwLocaleBits == CASC_LOCALE_PTPT)
        LoadWowRootFileLocales(hs, pbRootFile, cbRootFile, CASC_LOCALE_PTBR, false, HighestBitValue);

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

//-----------------------------------------------------------------------------
// Public functions

// WOW6: 00415000 
int LoadWowRootFile(
    TCascStorage * hs,
    LPBYTE pbRootFile,
    DWORD cbRootFile,
    DWORD dwLocaleMask)
{
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

    // Allocate root table entries. Note that the initial size
    // of the root table is set to 0x00200000 by World of Warcraft 6.x
    hs->RootTable.TablePtr  = CASC_ALLOC(CASC_ROOT_ENTRY, CASC_INITIAL_ROOT_TABLE_SIZE);
    hs->RootTable.TableSize = CASC_INITIAL_ROOT_TABLE_SIZE;
    if(hs->RootTable.TablePtr == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Clear the entire table
    memset(hs->RootTable.TablePtr, 0, CASC_INITIAL_ROOT_TABLE_SIZE * sizeof(CASC_ROOT_ENTRY));

    // Load the root file
    nError = LoadWowRootFileWithParams(hs, pbRootFile, cbRootFile, dwLocaleMask, 0); 
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = LoadWowRootFileWithParams(hs, pbRootFile, cbRootFile, dwLocaleMask, 1); 
    if(nError != ERROR_SUCCESS)
        return nError;

    return ERROR_SUCCESS;
}

