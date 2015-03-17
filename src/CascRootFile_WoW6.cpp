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


typedef struct _CASC_ROOT_BLOCK
{
    PFILE_LOCALE_BLOCK pLocaleBlockHdr;         // Pointer to the locale block
    PDWORD pInt32Array;                         // Pointer to the array of 32-bit integers
    PFILE_ROOT_ENTRY pRootEntries;

} CASC_ROOT_BLOCK, *PCASC_ROOT_BLOCK;

// Root file entry for CASC storages without MNDX root file (World of Warcraft 6.0+)
// Does not match to the in-file structure of the root entry
typedef struct _CASC_ROOT_ENTRY
{
    ULONGLONG FileNameHash;                         // Jenkins hash of the file name
    DWORD SumValue;                                 // Sum value
    DWORD Locales;                                  // Locale flags of the file
    DWORD EncodingKey[4];                           // File encoding key (MD5)

} CASC_ROOT_ENTRY, *PCASC_ROOT_ENTRY;

struct TRootFile_WoW6 : public TRootFile
{
    PCASC_ROOT_ENTRY pRootEntries;
    PCASC_MAP pRootMap;                             // Pointer to hash table with root entries
    DWORD dwTotalFileCount;
    DWORD dwFileCount;
};

// Prototype for root file parsing routine
typedef int (*PARSE_ROOT)(TRootFile_WoW6 * pRootFile, PCASC_ROOT_BLOCK pBlockInfo);

//-----------------------------------------------------------------------------
// Local functions

// Also used in CascSearchFile
PCASC_ROOT_ENTRY FindRootEntry(PCASC_MAP pRootMap, const char * szFileName, DWORD * PtrTableIndex)
{
    ULONGLONG FileNameHash;
    char szNormName[MAX_PATH + 1];
    size_t nLength;
    uint32_t dwHashHigh = 0;
    uint32_t dwHashLow = 0;

    // Normalize the file name
    nLength = NormalizeFileName_UpperBkSlash(szNormName, szFileName, MAX_PATH);

    // Calculate the HASH value of the normalized file name
    hashlittle2(szNormName, nLength, &dwHashHigh, &dwHashLow);
    FileNameHash = ((ULONGLONG)dwHashHigh << 0x20) | dwHashLow;

    // Perform the hash search
    return (PCASC_ROOT_ENTRY)Map_FindObject(pRootMap, &FileNameHash, PtrTableIndex);
}

LPBYTE VerifyLocaleBlock(PCASC_ROOT_BLOCK pBlockInfo, LPBYTE pbFilePointer, LPBYTE pbFileEnd)
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

static int ParseRoot_CountFiles(
    TRootFile_WoW6 * pRootFile,
    PCASC_ROOT_BLOCK pRootBlock)
{
    // Add the file count to the total file count
    pRootFile->dwTotalFileCount += pRootBlock->pLocaleBlockHdr->NumberOfFiles;
    return ERROR_SUCCESS;
}

static int ParseRoot_AddRootEntries(
    TRootFile_WoW6 * pRootFile,
    PCASC_ROOT_BLOCK pRootBlock)
{
    PCASC_ROOT_ENTRY pRootEntry = pRootFile->pRootEntries + pRootFile->dwFileCount;
    DWORD SumValue = 0;

    // Sanity checks
    assert(pRootFile->pRootEntries != NULL);
    assert(pRootFile->dwTotalFileCount != 0);

    // WoW.exe (build 19116): Blocks with zero files are skipped
    for(DWORD i = 0; i < pRootBlock->pLocaleBlockHdr->NumberOfFiles; i++)
    {
        // (004147A3) Prepare the CASC_ROOT_ENTRY structure
        pRootEntry->FileNameHash = pRootBlock->pRootEntries[i].FileNameHash;
        pRootEntry->SumValue = SumValue + pRootBlock->pInt32Array[i];
        pRootEntry->Locales = pRootBlock->pLocaleBlockHdr->Locales;
        pRootEntry->EncodingKey[0] = pRootBlock->pRootEntries[i].EncodingKey[0];
        pRootEntry->EncodingKey[1] = pRootBlock->pRootEntries[i].EncodingKey[1];
        pRootEntry->EncodingKey[2] = pRootBlock->pRootEntries[i].EncodingKey[2];
        pRootEntry->EncodingKey[3] = pRootBlock->pRootEntries[i].EncodingKey[3];

        // Move to the next root entry
        pRootFile->dwFileCount++;
        pRootEntry++;
        SumValue++;
    }

    return ERROR_SUCCESS;
}

static int ParseWowRootFileInternal(
    TRootFile_WoW6 * pRootFile,
    PARSE_ROOT pfnParseRoot,
    LPBYTE pbRootFile,
    LPBYTE pbRootFileEnd,
    DWORD dwLocaleMask,
    bool bLoadBlocksWithFlags80,
    BYTE HighestBitValue)
{
    CASC_ROOT_BLOCK RootBlock;

    // Now parse the root file
    while(pbRootFile < pbRootFileEnd)
    {
        // Validate the file locale block
        pbRootFile = VerifyLocaleBlock(&RootBlock, pbRootFile, pbRootFileEnd);
        if(pbRootFile == NULL)
            break;

        // WoW.exe (build 19116): Entries with flag 0x100 set are skipped
        if(RootBlock.pLocaleBlockHdr->Flags & 0x100)
            continue;

        // WoW.exe (build 19116): Entries with flag 0x80 set are skipped if arg_4 is set to FALSE (which is by default)
        if((RootBlock.pLocaleBlockHdr->Flags & 0x80) && bLoadBlocksWithFlags80 == 0)
            continue;

        // WoW.exe (build 19116): Entries with (flags >> 0x1F) not equal to arg_8 are skipped
        if((RootBlock.pLocaleBlockHdr->Flags >> 0x1F) != HighestBitValue)
            continue;

        // WoW.exe (build 19116): Locales other than defined mask are skipped too
        if((RootBlock.pLocaleBlockHdr->Locales & dwLocaleMask) == 0)
            continue;

        // Now call the custom function
        pfnParseRoot(pRootFile, &RootBlock);
    }

    return ERROR_SUCCESS;
}

/*
    // Code from WoW.exe
    if(dwLocaleMask == CASC_LOCALE_DUAL_LANG)
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

static int ParseWowRootFile2(
    TRootFile_WoW6 * pRootFile,
    PARSE_ROOT pfnParseRoot,
    LPBYTE pbRootFile,
    LPBYTE pbRootFileEnd,
    DWORD dwLocaleMask,
    BYTE HighestBitValue)
{
    // Load the locale as-is
    ParseWowRootFileInternal(pRootFile, pfnParseRoot, pbRootFile, pbRootFileEnd, dwLocaleMask, false, HighestBitValue);

    // If we wanted enGB, we also load enUS for the missing files
    if(dwLocaleMask == CASC_LOCALE_ENGB)
        ParseWowRootFileInternal(pRootFile, pfnParseRoot, pbRootFile, pbRootFileEnd, CASC_LOCALE_ENUS, false, HighestBitValue);

    if(dwLocaleMask == CASC_LOCALE_PTPT)
        ParseWowRootFileInternal(pRootFile, pfnParseRoot, pbRootFile, pbRootFileEnd, CASC_LOCALE_PTBR, false, HighestBitValue);

    return ERROR_SUCCESS;
}

// WoW.exe: 004146C7 (BuildManifest::Load)
static int ParseWowRootFile(
    TRootFile_WoW6 * pRootFile,
    PARSE_ROOT pfnParseRoot,
    LPBYTE pbRootFile,
    LPBYTE pbRootFileEnd,
    DWORD dwLocaleMask)
{
    ParseWowRootFile2(pRootFile, pfnParseRoot, pbRootFile, pbRootFileEnd, dwLocaleMask, 0);
    ParseWowRootFile2(pRootFile, pfnParseRoot, pbRootFile, pbRootFileEnd, dwLocaleMask, 1);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// Implementation of WoW6 root file

LPBYTE TRootFileWoW6_Search(TRootFile_WoW6 * pRootFile, TCascSearch * pSearch, PDWORD /* PtrFileSize */, PDWORD PtrLocaleFlags)
{
    PCASC_ROOT_ENTRY pRootEntry;
    LPBYTE RootBitArray = (LPBYTE)pSearch->pRootContext;
    LPBYTE pbRootEntry;
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
            cbToAllocate = ((pRootFile->pRootMap->TableSize + 7) / 8);
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
            // Find the root entry
            pRootEntry = FindRootEntry(pRootFile->pRootMap, pSearch->szFileName, &TableIndex);
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
        while(pSearch->IndexLevel1 < pRootFile->pRootMap->TableSize)
        {
            // Is that entry valid?
            pbRootEntry = (LPBYTE)(pRootFile->pRootMap->HashTable[pSearch->IndexLevel1]);
            pRootEntry  = (PCASC_ROOT_ENTRY)(pbRootEntry - pRootFile->pRootMap->MemberOffset);
            if(pbRootEntry != NULL && pRootEntry->FileNameHash != 0)
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

    // Check the root directory for that hash
    pRootEntry = FindRootEntry(pRootFile->pRootMap, szFileName, NULL);
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
        if(pRootFile->pRootMap != NULL)
            Map_Free(pRootFile->pRootMap);
        pRootFile->pRootMap = NULL;

        // Free the array of entries
        if(pRootFile->pRootEntries != NULL)
            CASC_FREE(pRootFile->pRootEntries);
        pRootFile->pRootEntries = NULL;

        // Free the root file itself
        CASC_FREE(pRootFile);
    }
}

#ifdef _DEBUG
void TRootFileWoW6_Dump(
    TCascStorage * hs,
    TDumpContext * dc,                                      // Pointer to an opened file
    LPBYTE pbRootFile,
    DWORD cbRootFile,
    const TCHAR * szListFile,
    int nDumpLevel)
{
    PCASC_ENCODING_ENTRY pEncodingEntry;
    CASC_ROOT_BLOCK BlockInfo;
    PLISTFILE_MAP pListMap;
    QUERY_KEY EncodingKey;
    LPBYTE pbRootFileEnd = pbRootFile + cbRootFile;
    LPBYTE pbFilePointer;
    char szOneLine[0x100];
    DWORD i;

    // Create the listfile map
    pListMap = ListFile_CreateMap(szListFile);

    // Dump the root entries
    for(pbFilePointer = pbRootFile; pbFilePointer <= pbRootFileEnd; )
    {
        // Validate the root block
        pbFilePointer = VerifyLocaleBlock(&BlockInfo, pbFilePointer, pbRootFileEnd);
        if(pbFilePointer == NULL)
            break;

        // Dump the locale block
        dump_print(dc, "Flags: %08X  Locales: %08X  NumberOfFiles: %u\n"
                       "=========================================================\n",
                       BlockInfo.pLocaleBlockHdr->Flags, 
                       BlockInfo.pLocaleBlockHdr->Locales,
                       BlockInfo.pLocaleBlockHdr->NumberOfFiles);

        // Dump the hashes and encoding keys
        for(i = 0; i < BlockInfo.pLocaleBlockHdr->NumberOfFiles; i++)
        {
            // Dump the entry
            dump_print(dc, "%08X %08X-%08X %s %s\n",
                           (DWORD)(BlockInfo.pInt32Array[i]),
                           (DWORD)(BlockInfo.pRootEntries[i].FileNameHash >> 0x20),
                           (DWORD)(BlockInfo.pRootEntries[i].FileNameHash),
                           StringFromMD5((LPBYTE)BlockInfo.pRootEntries[i].EncodingKey, szOneLine),
                           ListFile_FindName(pListMap, BlockInfo.pRootEntries[i].FileNameHash));

            // Find the encoding entry in the encoding table
            if(nDumpLevel >= DUMP_LEVEL_ENCODING_FILE)
            {
                EncodingKey.pbData = (LPBYTE)BlockInfo.pRootEntries[i].EncodingKey;
                EncodingKey.cbData = MD5_HASH_SIZE;
                pEncodingEntry = FindEncodingEntry(hs, &EncodingKey, NULL);
                CascDumpEncodingEntry(hs, dc, pEncodingEntry, nDumpLevel);
            }
        }

        // Put extra newline
        dump_print(dc, "\n");
    }
    
    ListFile_FreeMap(pListMap);
}
#endif

//-----------------------------------------------------------------------------
// Public functions

int RootFile_CreateWoW6(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask)
{
    PCASC_ROOT_ENTRY pRootEntry;
    TRootFile_WoW6 * pRootFile;
    LPBYTE pbRootFileEnd = pbRootFile + cbRootFile;
    int nError;

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

#ifdef _DEBUG
    pRootFile->Dump = TRootFileWoW6_Dump;    // Support for ROOT file dump
#endif  // _DEBUG

    // Give the root file to the storage
    hs->pRootFile = pRootFile;

    //
    // Phase 1: Count the files that are going to be loaded
    //

    ParseWowRootFile(pRootFile, ParseRoot_CountFiles, pbRootFile, pbRootFileEnd, dwLocaleMask);

    //
    // Phase 2: Create linear table that will contain all root items
    //

    pRootFile->pRootEntries = CASC_ALLOC(CASC_ROOT_ENTRY, pRootFile->dwTotalFileCount);
    if(pRootFile->pRootEntries == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    ParseWowRootFile(pRootFile, ParseRoot_AddRootEntries, pbRootFile, pbRootFileEnd, dwLocaleMask);

    //
    // Phase 3: Create map for fast searching
    //

    pRootFile->pRootMap = Map_Create(pRootFile->dwTotalFileCount, sizeof(ULONGLONG), FIELD_OFFSET(CASC_ROOT_ENTRY, FileNameHash));
    if(pRootFile->pRootMap == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    for(DWORD i = 0; i < pRootFile->dwTotalFileCount; i++)
    {
        pRootEntry = pRootFile->pRootEntries + i;
        Map_InsertObject(pRootFile->pRootMap, &pRootEntry->FileNameHash);
    }

    return ERROR_SUCCESS;
}
