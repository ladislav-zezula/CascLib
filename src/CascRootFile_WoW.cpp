/*****************************************************************************/
/* CascRootFile_WoW.cpp                   Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Storage functions for CASC                                                */
/* Note: WoW offsets refer to WoW.exe 6.0.3.19116 (32-bit)                   */
/* SHA1: c10e9ffb7d040a37a356b96042657e1a0c95c0dd                            */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascRootFile_WoW.cpp            */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local structures

#define ROOT_SEARCH_PHASE_INITIALIZING  0
#define ROOT_SEARCH_PHASE_LISTFILE      1
#define ROOT_SEARCH_PHASE_NAMELESS      2
#define ROOT_SEARCH_PHASE_FINISHED      3

// Known dwRegion values returned from sub_661316 (7.0.3.22210 x86 win), also referred by lua GetCurrentRegion
#define WOW_REGION_US              0x01
#define WOW_REGION_KR              0x02
#define WOW_REGION_EU              0x03
#define WOW_REGION_TW              0x04
#define WOW_REGION_CN              0x05

typedef enum _ROOT_FORMAT
{
    RootFormatWoW6x,                            // WoW 6.x - 8.1.x
    RootFormatWoW82                             // WoW 8.2 or newer
} ROOT_FORMAT, *PROOT_FORMAT;

// ROOT file header, since 8.2
typedef struct _FILE_ROOT_HEADER_82
{
    DWORD Signature;                            // Must be CASC_WOW82_ROOT_SIGNATURE
    DWORD TotalFiles;
    DWORD FilesWithNameHash;
} FILE_ROOT_HEADER_82, *PFILE_ROOT_HEADER_82;

// On-disk version of root group. A root group contains a group of file
// with the same locale and file flags
typedef struct _FILE_ROOT_GROUP_HEADER
{
    DWORD NumberOfFiles;                        // Number of entries
    DWORD ContentFlags;
    DWORD LocaleFlags;                          // File locale mask (CASC_LOCALE_XXX)

    // Followed by a block of file data IDs (count: NumberOfFiles)
    // Followed by the MD5 and file name hash (count: NumberOfFiles)

} FILE_ROOT_GROUP_HEADER, *PFILE_ROOT_GROUP_HEADER;

// On-disk version of root entry. Only present in versions 6.x - 8.1.xx
// Each root entry represents one file in the CASC storage
// In WoW 8.2 and newer, CKey and FileNameHash are split into separate arrays
// and FileNameHash is optional
typedef struct _FILE_ROOT_ENTRY
{
    CONTENT_KEY CKey;                           // MD5 of the file
    ULONGLONG FileNameHash;                     // Jenkins hash of the file name

} FILE_ROOT_ENTRY, *PFILE_ROOT_ENTRY;

typedef struct _FILE_ROOT_GROUP
{
    FILE_ROOT_GROUP_HEADER Header;
    PDWORD FileDataIds;                         // Pointer to the array of File Data IDs

    PFILE_ROOT_ENTRY pRootEntries;              // Valid for WoW 6.x - 8.1.x
    PCONTENT_KEY pCKeyEntries;                  // Valid for WoW 8.2 or newer
    PULONGLONG pHashes;                         // Valid for WoW 8.2 or newer (optional)

} FILE_ROOT_GROUP, *PFILE_ROOT_GROUP;

//-----------------------------------------------------------------------------
// TRootHandler_WoW interface / implementation

struct TRootHandler_WoW : public TFileTreeRoot
{
    public:

    TRootHandler_WoW(ROOT_FORMAT RFormat, ULONG HashlessFileCount) : TFileTreeRoot(FTREE_FLAG_USE_DATA_ID | FTREE_FLAG_USE_LOCALE)
    {
        // Turn off the "we know file names" bit 
        FileCounterHashless = HashlessFileCount;
        FileCounter = 0;
        RootFormat = RFormat;

        // Update the flags based on format
        switch(RootFormat)
        {
            case RootFormatWoW6x:
                dwRootFlags |= ROOT_FLAG_NAME_HASHES;
                break;

            case RootFormatWoW82:
                dwRootFlags |= ROOT_FLAG_FILE_DATA_IDS;
                break;
        }
    }

    static LPBYTE CaptureRootHeader(FILE_ROOT_HEADER_82 & RootHeader, LPBYTE pbRootPtr, LPBYTE pbRootEnd)
    {
        // Validate the root file header
        if((pbRootPtr + sizeof(FILE_ROOT_HEADER_82)) >= pbRootEnd)
            return NULL;
        memcpy(&RootHeader, pbRootPtr, sizeof(FILE_ROOT_HEADER_82));

        // Verify the root file header
        if(RootHeader.Signature != CASC_WOW82_ROOT_SIGNATURE)
            return NULL;
        if(RootHeader.FilesWithNameHash > RootHeader.TotalFiles)
            return NULL;

        return pbRootPtr + sizeof(FILE_ROOT_HEADER_82);
    }

    LPBYTE CaptureRootGroup(FILE_ROOT_GROUP & RootGroup, LPBYTE pbRootPtr, LPBYTE pbRootEnd)
    {
        // Reset the entire root group structure
        memset(&RootGroup, 0, sizeof(FILE_ROOT_GROUP));

        // Validate the locale block header
        if((pbRootPtr + sizeof(FILE_ROOT_GROUP_HEADER)) >= pbRootEnd)
            return NULL;
        memcpy(&RootGroup.Header, pbRootPtr, sizeof(FILE_ROOT_GROUP_HEADER));
        pbRootPtr = pbRootPtr + sizeof(FILE_ROOT_GROUP_HEADER);

        // Validate the array of file data IDs
        if((pbRootPtr + (sizeof(DWORD) * RootGroup.Header.NumberOfFiles)) >= pbRootEnd)
            return NULL;
        RootGroup.FileDataIds = (PDWORD)pbRootPtr;
        pbRootPtr = pbRootPtr + (sizeof(DWORD) * RootGroup.Header.NumberOfFiles);

        // Add the number of files in this block to the number of files loaded
        FileCounter += RootGroup.Header.NumberOfFiles;

        // Validate the array of root entries
        switch(RootFormat)
        {
            case RootFormatWoW6x:
                if((pbRootPtr + (sizeof(FILE_ROOT_ENTRY) * RootGroup.Header.NumberOfFiles)) >= pbRootEnd)
                    return NULL;
                RootGroup.pRootEntries = (PFILE_ROOT_ENTRY)pbRootPtr;

                // Return the position of the next block
                return pbRootPtr + (sizeof(FILE_ROOT_ENTRY) * RootGroup.Header.NumberOfFiles);

            case RootFormatWoW82:
                
                // Verify the position of array of CONTENT_KEY
                if((pbRootPtr + (sizeof(CONTENT_KEY) * RootGroup.Header.NumberOfFiles)) >= pbRootEnd)
                    return NULL;
                RootGroup.pCKeyEntries = (PCONTENT_KEY)pbRootPtr;
                pbRootPtr = pbRootPtr + (sizeof(CONTENT_KEY) * RootGroup.Header.NumberOfFiles);

                // Also include array of file hashes
                if(FileCounter > FileCounterHashless)
                {
                    if((pbRootPtr + (sizeof(ULONGLONG) * RootGroup.Header.NumberOfFiles)) >= pbRootEnd)
                        return NULL;
                    RootGroup.pHashes = (PULONGLONG)pbRootPtr;
                    pbRootPtr = pbRootPtr + (sizeof(ULONGLONG) * RootGroup.Header.NumberOfFiles);
                }

                return pbRootPtr;

            default:
                return NULL;
        }
    }

    int ParseWowRootFile_AddFiles_6x(FILE_ROOT_GROUP & RootGroup)
    {
        PFILE_ROOT_ENTRY pRootEntry = RootGroup.pRootEntries;
        DWORD dwFileDataId = 0;

        // Sanity check
        assert(RootGroup.pRootEntries != NULL);

        // WoW.exe (build 19116): Blocks with zero files are skipped
        for(DWORD i = 0; i < RootGroup.Header.NumberOfFiles; i++, pRootEntry++)
        {
            // Set the file data ID
            dwFileDataId = dwFileDataId + RootGroup.FileDataIds[i];

            // Insert the file node to the tree
            FileTree.Insert(&pRootEntry->CKey, pRootEntry->FileNameHash, dwFileDataId, CASC_INVALID_SIZE, RootGroup.Header.LocaleFlags);

            // Update the file data ID
            assert((dwFileDataId + 1) > dwFileDataId);
            dwFileDataId++;
        }

        return ERROR_SUCCESS;
    }

    int ParseWowRootFile_AddFiles_82(FILE_ROOT_GROUP & RootGroup)
    {
        PCONTENT_KEY pCKeyEntry = RootGroup.pCKeyEntries;
        DWORD dwFileDataId = 0;

        // Sanity check
        assert(RootGroup.pCKeyEntries != NULL);

        // WoW.exe (build 19116): Blocks with zero files are skipped
        for(DWORD i = 0; i < RootGroup.Header.NumberOfFiles; i++, pCKeyEntry++)
        {
            ULONGLONG FileNameHash = 0;

            // Set the file data ID
            dwFileDataId = dwFileDataId + RootGroup.FileDataIds[i];

            // Is there a file name hash?
            if(RootGroup.pHashes != NULL)
                FileNameHash = RootGroup.pHashes[i];

            // Insert the file node to the tree
            FileTree.Insert(pCKeyEntry, FileNameHash, dwFileDataId, CASC_INVALID_SIZE, RootGroup.Header.LocaleFlags);

            // Update the file data ID
            assert((dwFileDataId + 1) > dwFileDataId);
            dwFileDataId++;
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
        FILE_ROOT_GROUP RootBlock;

        // Reset the total file counter
        FileCounter = 0;

        // Now parse the root file
        while(pbRootPtr < pbRootEnd)
        {
            // Validate the file locale block
            pbRootPtr = CaptureRootGroup(RootBlock, pbRootPtr, pbRootEnd);
            if(pbRootPtr == NULL)
                break;

            // WoW.exe (build 19116): Entries with flag 0x100 set are skipped
            if(RootBlock.Header.ContentFlags & 0x100)
                continue;

            // WoW.exe (build 19116): Entries with flag 0x80 set are skipped if overrideArchive CVAR is set to FALSE (which is by default in non-chinese clients)
            if((RootBlock.Header.ContentFlags & 0x80) && bOverrideArchive == 0)
                continue;

            // WoW.exe (build 19116): Entries with (flags >> 0x1F) not equal to bAudioLocale are skipped
            if((RootBlock.Header.ContentFlags >> 0x1F) != bAudioLocale)
                continue;

            // WoW.exe (build 19116): Locales other than defined mask are skipped too
            if((RootBlock.Header.LocaleFlags & dwLocaleMask) == 0)
                continue;

            // Now call the custom function
            switch(RootFormat)
            {
                case RootFormatWoW82:
                    ParseWowRootFile_AddFiles_82(RootBlock);
                    break;

                case RootFormatWoW6x:
                    ParseWowRootFile_AddFiles_6x(RootBlock);
                    break;

                default:
                    return ERROR_NOT_SUPPORTED;
            }
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

        // Only if we have a listfile
        if(pSearch->pCache != NULL)
        {
            DWORD FileDataId = CASC_INVALID_ID;

            // Keep going through the listfile
            while(ListFile_GetNext(pSearch->pCache, pSearch->szMask, pSearch->szFileName, MAX_PATH, &FileDataId))
            {
                // Retrieve the file item
                pFileNode = FileTree.Find(pSearch->szFileName, FileDataId, NULL);
                if(pFileNode != NULL)
                {
                    // Give the file data id, file size and locale flags
                    FileTree.GetExtras(pFileNode, &pSearch->dwFileDataId, &pSearch->dwFileSize, &pSearch->dwLocaleFlags);
                    return pFileNode->CKey.Value;
                }
            }
        }

        // Try to find ANY items remaining
        return TFileTreeRoot::Search(pSearch);
    }

    ROOT_FORMAT RootFormat;                 // Root file format
    DWORD FileCounterHashless;              // Number of files for which we don't have hash. Meaningless for WoW before 8.2
    DWORD FileCounter;                      // Counter of loaded files. Only used during loading of ROOT file
};

//-----------------------------------------------------------------------------
// Public functions

int RootHandler_CreateWoW(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask)
{
    TRootHandler_WoW * pRootHandler = NULL;
    FILE_ROOT_HEADER_82 RootHeader;
    ROOT_FORMAT RootFormat = RootFormatWoW6x;
    LPBYTE pbRootEnd = pbRootFile + cbRootFile;
    LPBYTE pbRootPtr;
    DWORD FileCounterHashless = 0;
    int nError = ERROR_BAD_FORMAT;

    // Check for the new format (World of Warcraft 8.2, build 30170
    pbRootPtr = TRootHandler_WoW::CaptureRootHeader(RootHeader, pbRootFile, pbRootEnd);
    if(pbRootPtr != NULL)
    {
        FileCounterHashless = RootHeader.TotalFiles - RootHeader.FilesWithNameHash;
        RootFormat = RootFormatWoW82;
        pbRootFile = pbRootPtr;
    }

    // Create the WOW handler
    pRootHandler = new TRootHandler_WoW(RootFormat, FileCounterHashless);
    if(pRootHandler != NULL)
    {
        // Load the root directory. If load failed, we free the object
        nError = pRootHandler->Load(pbRootFile, pbRootEnd, dwLocaleMask);
        if(nError != ERROR_SUCCESS)
        {
            delete pRootHandler;
            pRootHandler = NULL;
        }
    }

    // Assign the root directory (or NULL) and return error
    hs->pRootHandler = pRootHandler;
    return nError;
}

