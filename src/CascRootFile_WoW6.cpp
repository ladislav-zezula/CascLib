/*****************************************************************************/
/* CascRootFile_WoW6.cpp                  Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Storage functions for CASC                                                */
/* Note: WoW6 offsets refer to WoW.exe 6.0.3.19116 (32-bit)                  */
/* SHA1: c10e9ffb7d040a37a356b96042657e1a0c95c0dd                            */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascRootFile_WoW6.cpp           */
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

// known dwRegion values returned from sub_661316 (7.0.3.22210 x86 win), also referred by lua GetCurrentRegion
#define WOW_REGION_US              0x01
#define WOW_REGION_KR              0x02
#define WOW_REGION_EU              0x03
#define WOW_REGION_TW              0x04
#define WOW_REGION_CN              0x05

#define WOW_LOCALE_ENUS            0x00
#define WOW_LOCALE_KOKR            0x01
#define WOW_LOCALE_FRFR            0x02
#define WOW_LOCALE_DEDE            0x03
#define WOW_LOCALE_ZHCN            0x04
#define WOW_LOCALE_ZHTW            0x05
#define WOW_LOCALE_ESES            0x06
#define WOW_LOCALE_ESMX            0x07
#define WOW_LOCALE_RURU            0x08
#define WOW_LOCALE_PTBR            0x0A
#define WOW_LOCALE_ITIT            0x0B

// On-disk version of locale block
typedef struct _FILE_LOCALE_BLOCK_HEADER
{
    DWORD NumberOfFiles;                        // Number of entries
    DWORD Flags;
    DWORD Locales;                              // File locale mask (CASC_LOCALE_XXX)

    // Followed by a block of file data IDs (count: NumberOfFiles)
    // Followed by the MD5 and file name hash (count: NumberOfFiles)

} FILE_LOCALE_BLOCK_HEADER, *PFILE_LOCALE_BLOCK_HEADER;

// On-disk version of root entry
typedef struct _FILE_ROOT_ENTRY
{
    CONTENT_KEY CKey;                           // MD5 of the file
    ULONGLONG FileNameHash;                     // Jenkins hash of the file name

} FILE_ROOT_ENTRY, *PFILE_ROOT_ENTRY;


typedef struct _FILE_LOCALE_BLOCK
{
    FILE_LOCALE_BLOCK_HEADER Header;
    PDWORD FileDataIds;                         // Pointer to the array of File Data IDs
    PFILE_ROOT_ENTRY pFileEntries;

} FILE_LOCALE_BLOCK, *PFILE_LOCALE_BLOCK;

//-----------------------------------------------------------------------------
// TRootHandler_WoW6 interface / implementation

struct TRootHandler_WoW6 : public TFileTreeRoot
{
    public:

    TRootHandler_WoW6() : TFileTreeRoot(FTREE_FLAG_USE_DATA_ID | FTREE_FLAG_USE_LOCALE)
    {
        // Turn off the "we know file names" bit 
        dwRootFlags &= ~ROOT_FLAG_HAS_NAMES;
    }

    static LPBYTE CaptureLocaleBlock(FILE_LOCALE_BLOCK & LocaleBlock, LPBYTE pbRootPtr, LPBYTE pbRootEnd)
    {
        // Validate the locale block header
        if((pbRootPtr + sizeof(FILE_LOCALE_BLOCK_HEADER)) >= pbRootEnd)
            return NULL;
        memcpy(&LocaleBlock.Header, pbRootPtr, sizeof(FILE_LOCALE_BLOCK_HEADER));
        pbRootPtr = pbRootPtr + sizeof(FILE_LOCALE_BLOCK_HEADER);

        // Validate the array of file data IDs
        if((pbRootPtr + (sizeof(DWORD) * LocaleBlock.Header.NumberOfFiles)) >= pbRootEnd)
            return NULL;
        LocaleBlock.FileDataIds = (PDWORD)pbRootPtr;
        pbRootPtr = pbRootPtr + (sizeof(DWORD) * LocaleBlock.Header.NumberOfFiles);

        // Validate the array of root entries
        if((pbRootPtr + (sizeof(FILE_ROOT_ENTRY) * LocaleBlock.Header.NumberOfFiles)) >= pbRootEnd)
            return NULL;
        LocaleBlock.pFileEntries = (PFILE_ROOT_ENTRY)pbRootPtr;

        // Return the position of the next block
        return pbRootPtr + (sizeof(FILE_ROOT_ENTRY) * LocaleBlock.Header.NumberOfFiles);
    }

    int ParseWowRootFile_AddFiles(FILE_LOCALE_BLOCK & LocaleBlock)
    {
        PFILE_ROOT_ENTRY pFileEntry = LocaleBlock.pFileEntries;
        DWORD dwDataId = 0;

        // WoW.exe (build 19116): Blocks with zero files are skipped
        for(DWORD i = 0; i < LocaleBlock.Header.NumberOfFiles; i++, pFileEntry++)
        {
            // Set the file data ID
            dwDataId = dwDataId + LocaleBlock.FileDataIds[i];

            // Insert the file node to the tree
            FileTree.Insert(&pFileEntry->CKey, pFileEntry->FileNameHash, dwDataId, CASC_INVALID_SIZE, LocaleBlock.Header.Locales);

            // Update the file data ID
            assert((dwDataId + 1) > dwDataId);
            dwDataId++;
        }

        return ERROR_SUCCESS;
    }

    int ParseWowRootFile_Level2(
        LPBYTE pbRootPtr,
        LPBYTE pbRootEnd,
        DWORD dwLocaleMask,
        BYTE bOverrideArchive,
        BYTE bAudioLocale)
    {
        FILE_LOCALE_BLOCK RootBlock;

        // Now parse the root file
        while(pbRootPtr < pbRootEnd)
        {
            // Validate the file locale block
            pbRootPtr = CaptureLocaleBlock(RootBlock, pbRootPtr, pbRootEnd);
            if(pbRootPtr == NULL)
                break;

            // WoW.exe (build 19116): Entries with flag 0x100 set are skipped
            if(RootBlock.Header.Flags & 0x100)
                continue;

            // WoW.exe (build 19116): Entries with flag 0x80 set are skipped if overrideArchive CVAR is set to FALSE (which is by default in non-chinese clients)
            if((RootBlock.Header.Flags & 0x80) && bOverrideArchive == 0)
                continue;

            // WoW.exe (build 19116): Entries with (flags >> 0x1F) not equal to bAudioLocale are skipped
            if((RootBlock.Header.Flags >> 0x1F) != bAudioLocale)
                continue;

            // WoW.exe (build 19116): Locales other than defined mask are skipped too
            if((RootBlock.Header.Locales & dwLocaleMask) == 0)
                continue;

            // Now call the custom function
            ParseWowRootFile_AddFiles(RootBlock);
        }

        return ERROR_SUCCESS;
    }

    /*

        // dwLocale is obtained from a WOW_LOCALE_* to CASC_LOCALE_BIT_* mapping (sub_6615D0 in 7.0.3.22210 x86 win)
        // because (ENUS, ENGB) and (PTBR, PTPT) pairs share the same value on WOW_LOCALE_* enum
        // dwRegion is used to distinguish them
        if(dwRegion == WOW_REGION_EU)
        {
            // Is this english version of WoW?
            if(dwLocale == CASC_LOCALE_BIT_ENUS)
            {
                LoadWowRootFileLocales(hs, pbRootPtr, cbRootFile, CASC_LOCALE_ENGB, bOverrideArchive, bAudioLocale);
                LoadWowRootFileLocales(hs, pbRootPtr, cbRootFile, CASC_LOCALE_ENUS, bOverrideArchive, bAudioLocale);
                return ERROR_SUCCESS;
            }

            // Is this portuguese version of WoW?
            if(dwLocale == CASC_LOCALE_BIT_PTBR)
            {
                LoadWowRootFileLocales(hs, pbRootPtr, cbRootFile, CASC_LOCALE_PTPT, bOverrideArchive, bAudioLocale);
                LoadWowRootFileLocales(hs, pbRootPtr, cbRootFile, CASC_LOCALE_PTBR, bOverrideArchive, bAudioLocale);
            }
        }
        else
            LoadWowRootFileLocales(hs, pbRootPtr, cbRootFile, (1 << dwLocale), bOverrideArchive, bAudioLocale);
    */

    int ParseWowRootFile_Level1(
        LPBYTE pbRootPtr,
        LPBYTE pbRootEnd,
        DWORD dwLocaleMask,
        BYTE bAudioLocale)
    {
        // Load the locale as-is
        ParseWowRootFile_Level2(pbRootPtr, pbRootEnd, dwLocaleMask, false, bAudioLocale);

        // If we wanted enGB, we also load enUS for the missing files
        if(dwLocaleMask == CASC_LOCALE_ENGB)
            ParseWowRootFile_Level2(pbRootPtr, pbRootEnd, CASC_LOCALE_ENUS, false, bAudioLocale);

        if(dwLocaleMask == CASC_LOCALE_PTPT)
            ParseWowRootFile_Level2(pbRootPtr, pbRootEnd, CASC_LOCALE_PTBR, false, bAudioLocale);

        return ERROR_SUCCESS;
    }

    // WoW.exe: 004146C7 (BuildManifest::Load)
    int Load(LPBYTE pbRootPtr, LPBYTE pbRootEnd, DWORD dwLocaleMask)
    {
        ParseWowRootFile_Level1(pbRootPtr, pbRootEnd, dwLocaleMask, 0);
        ParseWowRootFile_Level1(pbRootPtr, pbRootEnd, dwLocaleMask, 1);
        return ERROR_SUCCESS;
    }

    // Search for files
    LPBYTE Search(TCascSearch * pSearch)
    {
        PCASC_FILE_NODE pFileNode;
        LPBYTE pbEKey;

        // Try to find named items
        pbEKey = TFileTreeRoot::Search(pSearch);
        if(pbEKey != NULL)
            return pbEKey;

        // Only if we have a listfile
        if(pSearch->pCache != NULL)
        {
            // Keep going through the listfile
            while(ListFile_GetNext(pSearch->pCache, pSearch->szMask, pSearch->szFileName, MAX_PATH))
            {
                // Check the wildcard first
                if (CheckWildCard(pSearch->szFileName, pSearch->szMask))
                {
                    // Retrieve the file item
                    pFileNode = FileTree.Find(pSearch->szFileName, NULL);
                    if(pFileNode != NULL)
                    {
                        FileTree.GetExtras(pFileNode, &pSearch->dwFileDataId, NULL, &pSearch->dwLocaleFlags);
                        return pFileNode->CKey.Value;
                    }
                }
            }
        }

        // No more files
        return NULL;
    }
};

//-----------------------------------------------------------------------------
// Public functions

int RootHandler_CreateWoW6(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask)
{
    TRootHandler_WoW6 * pRootHandler = NULL;
    FILE_LOCALE_BLOCK LocaleBlock;
    int nError = ERROR_BAD_FORMAT;

    // Verify whether this is really a WoW block
    if(TRootHandler_WoW6::CaptureLocaleBlock(LocaleBlock, pbRootFile, pbRootFile + cbRootFile))
    {
        pRootHandler = new TRootHandler_WoW6();
        if(pRootHandler != NULL)
        {
            // Load the root directory. If load failed, we free the object
            nError = pRootHandler->Load(pbRootFile, pbRootFile + cbRootFile, dwLocaleMask);
            if(nError != ERROR_SUCCESS)
            {
                delete pRootHandler;
                pRootHandler = NULL;
            }
        }
    }

    // Assign the root directory (or NULL) and return error
    hs->pRootHandler = pRootHandler;
    return nError;
}
