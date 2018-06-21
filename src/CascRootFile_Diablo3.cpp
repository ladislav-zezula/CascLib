/*****************************************************************************/
/* CascRootFile_Diablo3.cpp               Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Support for loading Diablo 3 ROOT file                                    */
/* Note: D3 offsets refer to Diablo III.exe 2.2.0.30013 (32-bit)             */
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

#define DIABLO3_SUBDIR_SIGNATURE   0xEAF1FE87
#define DIABLO3_PACKAGES_SIGNATURE 0xAABB0002
#define DIABLO3_MAX_SUBDIRS        0x20
#define DIABLO3_MAX_ASSETS         70               // Maximum possible number of assets
#define DIABLO3_MAX_ROOT_FOLDERS   0x20             // Maximum count of root directory named entries

// On-disk structure for a file given by file number
typedef struct _DIABLO3_ASSET_ENTRY
{
    CONTENT_KEY CKey;                              // Content key for the file
    DWORD FileIndex;                               // File index
} DIABLO3_ASSET_ENTRY, *PDIABLO3_ASSET_ENTRY;

// On-disk structure for a file given by file number and suffix
typedef struct _DIABLO3_ASSETIDX_ENTRY
{
    CONTENT_KEY CKey;                              // Content key for the file
    DWORD FileIndex;                               // File index
    DWORD SubIndex;                                // File subindex, like "SoundBank\3D Ambience\0000.smp"
} DIABLO3_ASSETIDX_ENTRY, *PDIABLO3_ASSETIDX_ENTRY;

// In-memory structure of the named entry
typedef struct _DIABLO3_NAMED_ENTRY
{
    PCONTENT_KEY pCKey;                             // Pointer to the content key
    const char * szFileName;                        // Pointer to the zero-terminated file name
    const char * szFileEnd;                         // Position of the zero terminator (aka end of the file name)
} DIABLO3_NAMED_ENTRY, *PDIABLO3_NAMED_ENTRY;

// On-disk structure of CoreToc.dat header
typedef struct _DIABLO3_CORE_TOC_HEADER
{
    DWORD EntryCounts[DIABLO3_MAX_ASSETS];          // Array of number of entries (files) for each asset
    DWORD EntryOffsets[DIABLO3_MAX_ASSETS];         // Array of offsets of each DIABLO3_CORE_TOC_ENTRY, relative to data after header
    DWORD Unknowns[DIABLO3_MAX_ASSETS];             // Unknown
    DWORD Alignment;
} DIABLO3_CORE_TOC_HEADER, *PDIABLO3_CORE_TOC_HEADER;

// On-disk structure of the entry in CoreToc.dat
typedef struct _DIABLO3_CORE_TOC_ENTRY
{
    DWORD AssetIndex;                               // Index of the Diablo3 asset (aka directory)
    DWORD FileIndex;                                // File index
    DWORD NameOffset;                               // Offset of the plain file name

} DIABLO3_CORE_TOC_ENTRY, *PDIABLO3_CORE_TOC_ENTRY;

// In-memory structure of parsed directory data
typedef struct _DIABLO3_DIRECTORY
{
    LPBYTE pbDirectoryData;                         // The begin of the directory data block
    LPBYTE pbDirectoryEnd;                          // The end of the directory data block
    LPBYTE pbAssetEntries;                          // Pointer to asset entries without subitem number. Example: "SoundBank\SoundFile.smp"
    LPBYTE pbAssetIdxEntries;                       // Pointer to asset entries with subitem number
    LPBYTE pbNamedEntries;                          // Pointer to named entries. These are for files with arbitrary names, and they do not belong to an asset
    DWORD dwAssetEntries;                           // Number of asset entries without subitem number
    DWORD dwAssetIdxEntries;
    DWORD dwNamedEntries;
    DWORD dwNodeIndex;                              // Index of file node for this folder
} DIABLO3_DIRECTORY, *PDIABLO3_DIRECTORY;

// Structure for conversion DirectoryID -> Directory name
typedef struct _DIABLO3_ASSET_INFO
{
    const char * szDirectoryName;                   // Directory name
    const char * szExtension;

} DIABLO3_ASSET_INFO;
typedef const DIABLO3_ASSET_INFO * PDIABLO3_ASSET_INFO;

//-----------------------------------------------------------------------------
// Structure definitions for Diablo3 root file

struct TRootHandler_Diablo3 : public TRootHandler
{
    // Global tree of all items
    CASC_FILE_TREE FileTree;

    // Array of root directory subdirectories
    DIABLO3_DIRECTORY RootFolders[DIABLO3_MAX_ROOT_FOLDERS];

    // Array of DIABLO3_TOC_ENTRY structures, sorted by the file index
    // Used for converting FileIndex -> Asset+PlainName during loading
    PDIABLO3_CORE_TOC_ENTRY pFileIndices;
    LPBYTE pbCoreTocFile;
    LPBYTE pbCoreTocData;
    size_t cbCoreTocFile;
    size_t nFileIndices;

    // Map for searching a real file extension
    PCASC_MAP pPackagesMap;
    LPBYTE pbPackagesDat;
    size_t cbPackagesDat;

    // Back pointer to the storage
    TCascStorage * hs;
};

//-----------------------------------------------------------------------------
// Local variables

static const DIABLO3_ASSET_INFO Assets[] =
{
//   DIR-NAME               EXTENSION
//   ==========             =========
    {NULL,                  NULL},         // 0x00
    {"Actor",               "acr"},        // 0x01
    {"Adventure",           "adv"},        // 0x02
    {NULL,                  NULL},         // 0x03
    {NULL,                  NULL},         // 0x04
    {"AmbientSound",        "ams"},        // 0x05
    {"Anim",                "ani"},        // 0x06
    {"Anim2D",              "an2"},        // 0x07
    {"AnimSet",             "ans"},        // 0x08
    {"Appearance",          "app"},        // 0x09
    {NULL,                  NULL},         // 0x0A
    {"Cloth",               "clt"},        // 0x0B
    {"Conversation",        "cnv"},        // 0x0C
    {NULL,                  NULL},         // 0x0D
    {"EffectGroup",         "efg"},        // 0x0E
    {"Encounter",           "enc"},        // 0x0F
    {NULL,                  NULL},         // 0x10
    {"Explosion",           "xpl"},        // 0x11
    {NULL,                  NULL},         // 0x12
    {"Font",                "fnt"},        // 0x13
    {"GameBalance",         "gam"},        // 0x14
    {"Globals",             "glo"},        // 0x15
    {"LevelArea",           "lvl"},        // 0x16
    {"Light",               "lit"},        // 0x17
    {"MarkerSet",           "mrk"},        // 0x18
    {"Monster",             "mon"},        // 0x19
    {"Observer",            "obs"},        // 0x1A
    {"Particle",            "prt"},        // 0x1B
    {"Physics",             "phy"},        // 0x1C
    {"Power",               "pow"},        // 0x1D
    {NULL,                  NULL},         // 0x1E
    {"Quest",               "qst"},        // 0x1F
    {"Rope",                "rop"},        // 0x20
    {"Scene",               "scn"},        // 0x21
    {"SceneGroup",          "scg"},        // 0x22
    {NULL,                  NULL},         // 0x23
    {"ShaderMap",           "shm"},        // 0x24
    {"Shaders",             "shd"},        // 0x25
    {"Shakes",              "shk"},        // 0x26
    {"SkillKit",            "skl"},        // 0x27
    {"Sound",               "snd"},        // 0x28
    {"SoundBank",           "sbk"},        // 0x29
    {"StringList",          "stl"},        // 0x2A
    {"Surface",             "srf"},        // 0x2B
    {"Textures",            "tex"},        // 0x2C
    {"Trail",               "trl"},        // 0x2D
    {"UI",                  "ui"},         // 0x2E
    {"Weather",             "wth"},        // 0x2F
    {"Worlds",              "wrl"},        // 0x30
    {"Recipe",              "rcp"},        // 0x31
    {NULL,                  NULL},         // 0x32
    {"Condition",           "cnd"},        // 0x33
    {NULL,                  NULL},         // 0x34
    {NULL,                  NULL},         // 0x35
    {NULL,                  NULL},         // 0x36
    {NULL,                  NULL},         // 0x37
    {"Act",                 "act"},        // 0x38
    {"Material",            "mat"},        // 0x39
    {"QuestRange",          "qsr"},        // 0x3A
    {"Lore",                "lor"},        // 0x3B
    {"Reverb",              "rev"},        // 0x3C
    {"PhysMesh",            "phm"},        // 0x3D
    {"Music",               "mus"},        // 0x3E
    {"Tutorial",            "tut"},        // 0x3F
    {"BossEncounter",       "bos"},        // 0x40
    {NULL,                  NULL},         // 0x41
    {"Accolade",            "aco"},        // 0x42
};

static const DIABLO3_ASSET_INFO UnknownAsset = {"Unknown", "unk"};

#define DIABLO3_ASSET_COUNT (sizeof(Assets) / sizeof(Assets[0]))

//-----------------------------------------------------------------------------
// Local functions

static char * AppendPathToTotalPath(PATH_BUFFER & PathBuffer, const char * szFileName, const char * szFileEnd, bool bIsDirectory)
{
    char * szPathPtr = PathBuffer.szPtr;
    size_t nLength = (szFileEnd - szFileName);

    // Append the name
    if((szPathPtr + nLength) < PathBuffer.szEnd)
    {
        memcpy(szPathPtr, szFileName, nLength);
        szPathPtr += nLength;
    }

    // Append backslash, if needed
    if(bIsDirectory && (szPathPtr + 1) < PathBuffer.szEnd)
        *szPathPtr++ = '\\';
    if(szPathPtr < PathBuffer.szEnd)
        szPathPtr[0] = 0;
    return szPathPtr;
}

static PDIABLO3_ASSET_INFO GetAssetInfo(DWORD dwAssetIndex)
{
    if(dwAssetIndex < DIABLO3_ASSET_COUNT && Assets[dwAssetIndex].szDirectoryName != NULL)
        return &Assets[dwAssetIndex];
    return NULL;
}

static char * FindPackageName(
    PCASC_MAP pPackageMap,
    const char * szAssetName,
    const char * szPlainName)
{
    char szFileName[MAX_PATH+1];
    size_t nLength;

    // Construct the name without extension and find it in the map
    nLength = sprintf(szFileName, "%s\\%s", szAssetName, szPlainName);
    return (char *)Map_FindString(pPackageMap, szFileName, szFileName + nLength);
}

static LPBYTE LoadFileToMemory(TCascStorage * hs, const char * szFileName, DWORD * pcbFileData)
{
    LPBYTE pbCKey = NULL;
    LPBYTE pbFileData = NULL;
    DWORD dwDummy;

    // Try to find CKey for the file
    pbCKey = RootHandler_GetKey(hs->pRootHandler, szFileName, &dwDummy);
    if(pbCKey != NULL)
        pbFileData = LoadInternalFileToMemory(hs, pbCKey, CASC_OPEN_BY_CKEY, pcbFileData);

    return pbFileData;
}

static LPBYTE CaptureDirectoryData(
    PDIABLO3_DIRECTORY pDirHeader,
    LPBYTE pbDirectory,
    DWORD cbDirectory)
{
    LPBYTE pbDirectoryData = pbDirectory;
    LPBYTE pbDataEnd = pbDirectory + cbDirectory;
    DWORD Signature = 0;

    //
    // Structure of a Diablo3 directory header
    // 1) Signature (4 bytes)
    // 2) Number of DIABLO3_ASSET_ENTRY entries (4 bytes)
    // 3) Array of DIABLO3_ASSET_ENTRY entries
    // 4) Number of DIABLO3_ASSETIDX_ENTRY entries (4 bytes)
    // 5) Array of DIABLO3_ASSETIDX_ENTRY entries
    // 6) Number of DIABLO3_NAMED_ENTRY entries (4 bytes)
    // 7) Array of DIABLO3_NAMED_ENTRY entries
    //

    // Prepare the header signature
    memset(pDirHeader, 0, sizeof(DIABLO3_DIRECTORY));

    // Get the header signature
    pbDirectory = CaptureInteger32(pbDirectory, pbDataEnd, &Signature);
    if((pbDirectory == NULL) || (Signature != CASC_DIABLO3_ROOT_SIGNATURE && Signature != DIABLO3_SUBDIR_SIGNATURE))
        return NULL;

    // Subdirectories have extra two arrays
    if(Signature == DIABLO3_SUBDIR_SIGNATURE)
    {
        // Capture the number of DIABLO3_ASSET_ENTRY items
        pbDirectory = CaptureInteger32(pbDirectory, pbDataEnd, &pDirHeader->dwAssetEntries);
        if(pbDirectory == NULL)
            return NULL;

        // Capture the array of DIABLO3_ASSET_ENTRY
        pbDirectory = CaptureArray(pbDirectory, pbDataEnd, &pDirHeader->pbAssetEntries, DIABLO3_ASSET_ENTRY, pDirHeader->dwAssetEntries);
        if(pbDirectory == NULL)
            return NULL;

        // Capture the number of DIABLO3_ASSETIDX_ENTRY items
        pbDirectory = CaptureInteger32(pbDirectory, pbDataEnd, &pDirHeader->dwAssetIdxEntries);
        if(pbDirectory == NULL)
            return NULL;

        // Capture the array of DIABLO3_ASSETIDX_ENTRY
        pbDirectory = CaptureArray(pbDirectory, pbDataEnd, &pDirHeader->pbAssetIdxEntries, DIABLO3_ASSETIDX_ENTRY, pDirHeader->dwAssetIdxEntries);
        if(pbDirectory == NULL)
            return NULL;
    }

    // Capture the number of DIABLO3_NAMED_ENTRY array
    pbDirectory = CaptureInteger32(pbDirectory, pbDataEnd, &pDirHeader->dwNamedEntries);
    if(pbDirectory == NULL)
        return NULL;

    // Note: Do not capture the array here. We will do that later,
    // when we will be parsing the directory
    pDirHeader->pbNamedEntries = pbDirectory;

    // Put the directory range
    pDirHeader->pbDirectoryData = pbDirectoryData;
    pDirHeader->pbDirectoryEnd = pbDirectoryData + cbDirectory;
    return pbDirectory;
}

static LPBYTE CaptureCoreTocHeader(
    PDIABLO3_CORE_TOC_HEADER * PtrHeader,
    PDWORD PtrMaxIndex,
    LPBYTE pbDataPtr,
    LPBYTE pbDataEnd)
{
    PDIABLO3_CORE_TOC_HEADER pTocHeader = (PDIABLO3_CORE_TOC_HEADER)pbDataPtr;
    DWORD dwMaxFileIndex = 0;

    // Check the space for header
    if((pbDataPtr + sizeof(DIABLO3_CORE_TOC_HEADER)) > pbDataEnd)
        return NULL;
    pbDataPtr += sizeof(DIABLO3_CORE_TOC_HEADER);

    // Verify all asset arrays
    for(size_t i = 0; i < DIABLO3_MAX_ASSETS; i++)
    {
        PDIABLO3_CORE_TOC_ENTRY pTocEntry = (PDIABLO3_CORE_TOC_ENTRY)(pbDataPtr + pTocHeader->EntryOffsets[i]);
        DWORD EntryOffset = pTocHeader->EntryOffsets[i];
        DWORD EntryCount = pTocHeader->EntryCounts[i];

        // Verify file range
        if((pbDataPtr + EntryOffset + EntryCount * sizeof(DIABLO3_CORE_TOC_ENTRY)) > pbDataEnd)
            return NULL;

        // Find out the entry with the maximum index
        for(DWORD n = 0; n < EntryCount; n++)
        {
            if(pTocEntry->FileIndex >= dwMaxFileIndex)
                dwMaxFileIndex = pTocEntry->FileIndex;
            pTocEntry++;
        }
    }

    // Give data and return
    PtrMaxIndex[0] = dwMaxFileIndex;
    PtrHeader[0] = pTocHeader;
    return pbDataPtr;
}

static LPBYTE CaptureNamedEntry(
    LPBYTE pbDataPtr,
    LPBYTE pbDataEnd,
    PDIABLO3_NAMED_ENTRY pEntry)
{
    // Capture the content key
    pbDataPtr = CaptureContentKey(pbDataPtr, pbDataEnd, &pEntry->pCKey);
    if(pbDataPtr == NULL)
        return NULL;

    // Capture file name. Must be ASCIIZ file name
    pEntry->szFileName = (const char *)pbDataPtr;
    while(pbDataPtr < pbDataEnd && pbDataPtr[0] != 0)
        pbDataPtr++;

    // Did we find a zero char?
    if(pbDataPtr < pbDataEnd && pbDataPtr[0] == 0)
    {
        pEntry->szFileEnd = (const char *)pbDataPtr;
        return pbDataPtr + 1;
    }

    return NULL;
}

static int LoadDirectoryFile(TRootHandler_Diablo3 * pRootHandler, size_t nIndex, PCONTENT_KEY pCKey)
{
    LPBYTE pbData;
    DWORD cbData = 0;

    // Do we still have space?
    if(nIndex >= DIABLO3_MAX_ROOT_FOLDERS)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Load the n-th folder
    pbData = LoadInternalFileToMemory(pRootHandler->hs, pCKey->Value, CASC_OPEN_BY_CKEY, &cbData);
    if(pbData && cbData)
    {
        if(CaptureDirectoryData(&pRootHandler->RootFolders[nIndex], pbData, cbData) == NULL)
        {
            // Clear the directory
            CASC_FREE(pbData);
            return ERROR_BAD_FORMAT;
        }
    }
    return ERROR_SUCCESS;
}

static bool CreateAssetFileName(
    TRootHandler_Diablo3 * pRootHandler,
    PATH_BUFFER & PathBuffer,
    DWORD FileIndex,
    DWORD SubIndex)
{
    PDIABLO3_CORE_TOC_ENTRY pTocEntry;
    PDIABLO3_ASSET_INFO pAssetInfo;
    PCASC_MAP pPackageMap = pRootHandler->pPackagesMap;
    const char * szPackageName = NULL;
    const char * szPlainName;
    const char * szFormat;
    char * szPathPtr = PathBuffer.szPtr;
    size_t nLength = 0;

    // Find and check the entry
    pTocEntry = pRootHandler->pFileIndices + FileIndex;
    if(pTocEntry->FileIndex == FileIndex)
    {
        // Retrieve the asset information
        pAssetInfo = GetAssetInfo(pTocEntry->AssetIndex);
        
        // Either use the asset info for getting the folder name or supply "Asset##"
        if(pAssetInfo != NULL)
        {
            strcpy(szPathPtr, pAssetInfo->szDirectoryName);
            szPathPtr += strlen(szPathPtr);
        }
        else
        {
            szPathPtr[0] = 'A';
            szPathPtr[1] = 's';
            szPathPtr[2] = 's';
            szPathPtr[3] = 'e';
            szPathPtr[4] = 't';
            szPathPtr[5] = (char)('0' + (pTocEntry->AssetIndex / 10));
            szPathPtr[6] = (char)('0' + (pTocEntry->AssetIndex % 10));
            szPathPtr += 7;
        }

        // Put the backslash
        if(szPathPtr < PathBuffer.szEnd)
            *szPathPtr++ = '\\';

        // Construct the file name with ending "." for extension
        szPlainName = (const char *)(pRootHandler->pbCoreTocData + pTocEntry->NameOffset);
        szFormat = (SubIndex != CASC_INVALID_INDEX) ? "%s\\%04u." : "%s.";
        nLength = sprintf(szPathPtr, szFormat, szPlainName, SubIndex);

        // Try to fixup the file extension from the package name.
        // File extensions are not predictable because for subitems,
        // they are not always equal to the main items:
        //
        //  SoundBank\3D Ambience.sbk
        //  SoundBank\3D Ambience\0000.smp
        //  SoundBank\3D Ambience\0002.smp
        //  ...
        //  SoundBank\Angel.sbk
        //  SoundBank\Angel\0000.fsb
        //  SoundBank\Angel\0002.fsb
        //
        // We use the Base\Data_D3\PC\Misc\Packages.dat for real file extensions, where possible
        //

        if(pPackageMap != NULL && pAssetInfo != NULL)
        {
            // Retrieve the asset name
            szPackageName = FindPackageName(pPackageMap, pAssetInfo->szDirectoryName, szPathPtr);
            if(szPackageName != NULL)
            {
                strcpy(PathBuffer.szPtr, szPackageName);
                return true;
            }
        }

        // Just use the extension from the AssetInfo
        if(pAssetInfo != NULL && pAssetInfo->szExtension != NULL)
        {
            strcpy(szPathPtr + nLength, pAssetInfo->szExtension);
            return true;
        }

        // Otherwise, supply "a##"
        sprintf(szPathPtr + nLength, "a%02u", pTocEntry->AssetIndex);
        return true;
    }

    return false;
}

// Parse the asset entries
static int ParseAssetEntries(
    TRootHandler_Diablo3 * pRootHandler,
    DIABLO3_DIRECTORY & Directory,
    PATH_BUFFER & PathBuffer)
{
    PDIABLO3_ASSET_ENTRY pEntry = (PDIABLO3_ASSET_ENTRY)Directory.pbAssetEntries;
    DWORD dwEntries = Directory.dwAssetEntries;

    // Do nothing if there is no entries
    if(pEntry != NULL && dwEntries != 0)
    {
        // Insert all asset entries to the file tree
        for(DWORD i = 0; i < dwEntries; i++, pEntry++)
        {
            // Construct the full path name of the entry
            if(CreateAssetFileName(pRootHandler, PathBuffer, pEntry->FileIndex, CASC_INVALID_INDEX))
            {
                // Insert the entry to the file tree
//              fprintf(fp, "%08u %s\n", pEntry->FileIndex, PathBuffer.szBegin);
                FileTree_Insert(&pRootHandler->FileTree, &pEntry->CKey, PathBuffer.szBegin);
            }
        }
    }

    return ERROR_SUCCESS;
}

static int ParseAssetAndIdxEntries(
    TRootHandler_Diablo3 * pRootHandler,
    DIABLO3_DIRECTORY & Directory,
    PATH_BUFFER & PathBuffer)
{
    PDIABLO3_ASSETIDX_ENTRY pEntry = (PDIABLO3_ASSETIDX_ENTRY)Directory.pbAssetIdxEntries;
    DWORD dwEntries = Directory.dwAssetIdxEntries;

    // Do nothing if there is no entries
    if(pEntry != NULL && dwEntries != 0)
    {
        // Insert all asset entries to the file tree
        for(DWORD i = 0; i < dwEntries; i++, pEntry++)
        {
            // Construct the full path name of the entry
            if(CreateAssetFileName(pRootHandler, PathBuffer, pEntry->FileIndex, pEntry->SubIndex))
            {
                // Insert the entry to the file tree
//              fprintf(fp, "%08u %04u %s\n", pEntry->FileIndex, pEntry->SubIndex, PathBuffer.szBegin);
                FileTree_Insert(&pRootHandler->FileTree, &pEntry->CKey, PathBuffer.szBegin);
            }
        }
    }

    return ERROR_SUCCESS;
}

// Parse the named entries of all folders
static int ParseDirectory_Phase1(
    TRootHandler_Diablo3 * pRootHandler,
    DIABLO3_DIRECTORY & Directory,
    PATH_BUFFER & PathBuffer,
    bool bIsRootDirectory)
{
    DIABLO3_NAMED_ENTRY NamedEntry;
    char * szSavePtr = PathBuffer.szPtr;
    size_t nFolderIndex = 0;
    int nError = ERROR_SUCCESS;

    // Do nothing if there is no named headers
    if(Directory.pbNamedEntries && Directory.dwNamedEntries)
    {
        void * pFileNode;
        LPBYTE pbDataPtr = Directory.pbNamedEntries;
        LPBYTE pbDataEnd = Directory.pbDirectoryEnd;
        DWORD dwNodeIndex;

        // Parse all entries
        while(pbDataPtr < pbDataEnd)
        {
            // Capture the named entry
            pbDataPtr = CaptureNamedEntry(pbDataPtr, pbDataEnd, &NamedEntry);
            if(pbDataPtr == NULL)
                return ERROR_BAD_FORMAT;

            // Append the path fragment to the total path
            PathBuffer.szPtr = AppendPathToTotalPath(PathBuffer, NamedEntry.szFileName, NamedEntry.szFileEnd, bIsRootDirectory);
            
            // Create file node belonging to this folder
            pFileNode = FileTree_Insert(&pRootHandler->FileTree, NamedEntry.pCKey, PathBuffer.szBegin);
            dwNodeIndex = (DWORD)FileTree_IndexOf(&pRootHandler->FileTree, pFileNode);

            // If we are parsing root folder, we also need to load the data of the sub-folder file
            if(bIsRootDirectory)
            {
                // Load the sub-directory file
                nError = LoadDirectoryFile(pRootHandler, nFolderIndex, NamedEntry.pCKey);
                if(nError != ERROR_SUCCESS)
                    return nError;

                // Parse the sub-directory file
                nError = ParseDirectory_Phase1(pRootHandler, pRootHandler->RootFolders[nFolderIndex], PathBuffer, false);
                if(nError != ERROR_SUCCESS)
                    return nError;

                // Also save the item pointer and increment the folder index
                pRootHandler->RootFolders[nFolderIndex].dwNodeIndex = dwNodeIndex;
                nFolderIndex++;
            }

            // Restore the path pointer
            PathBuffer.szPtr = szSavePtr;
            szSavePtr[0] = 0;
        }
    }

    return nError;
}

// Parse the nameless entries of all folders
static int ParseDirectory_Phase2(TRootHandler_Diablo3 * pRootHandler)
{
    PATH_BUFFER PathBuffer;
    char szPathBuffer[MAX_PATH];

    // Parse each root subdirectory
    for(size_t i = 0; i < DIABLO3_MAX_ROOT_FOLDERS; i++)
    {
        // Is this root folder loaded?
        if(pRootHandler->RootFolders[i].pbDirectoryData != NULL)
        {
            PathBuffer.szBegin = szPathBuffer;
            PathBuffer.szPtr = szPathBuffer;
            PathBuffer.szEnd = szPathBuffer + MAX_PATH - 1;
            szPathBuffer[0] = 0;

            // Retrieve the parent name
            if(pRootHandler->RootFolders[i].dwNodeIndex != 0)
            {
                FileTree_PathAt(&pRootHandler->FileTree, szPathBuffer, MAX_PATH, pRootHandler->RootFolders[i].dwNodeIndex);
                PathBuffer.szPtr = PathBuffer.szBegin + strlen(szPathBuffer);
            }

//          FILE * fp = fopen("E:\\FileIndex.txt", "wt");

            // Array of DIABLO3_ASSET_ENTRY entries.
            // These are for files belonging to an asset, without subitem number.
            // Example: "SoundBank\SoundFile.smp"
            ParseAssetEntries(pRootHandler, pRootHandler->RootFolders[i], PathBuffer);

            // Array of DIABLO3_ASSETIDX_ENTRY entries.
            // These are for files belonging to an asset, with a subitem number.
            // Example: "SoundBank\SoundFile\0001.smp"
            ParseAssetAndIdxEntries(pRootHandler, pRootHandler->RootFolders[i], PathBuffer);

//          fclose(fp);
        }
    }

    return ERROR_SUCCESS;
}

// Creates an array of DIABLO3_CORE_TOC_ENTRY entries indexed by FileIndex
// Used as lookup table when we have FileIndex and need Asset+PlainName
static int CreateMapOfFileIndices(TRootHandler_Diablo3 * pRootHandler, const char * szFileName)
{
    PDIABLO3_CORE_TOC_HEADER pTocHeader = NULL;
    PDIABLO3_CORE_TOC_ENTRY pFileIndices;
    LPBYTE pbFileData;
    DWORD dwMaxFileIndex = 0;
    DWORD cbFileData = 0;

    // Load the entire file to memory
    pbFileData = LoadFileToMemory(pRootHandler->hs, szFileName, &cbFileData);
    if(pbFileData && cbFileData)
    {
        LPBYTE pbCoreTocData = pbFileData;
        LPBYTE pbCoreTocEnd = pbFileData + cbFileData;

        // Capture the header
        if((pbCoreTocData = CaptureCoreTocHeader(&pTocHeader, &dwMaxFileIndex, pbCoreTocData, pbCoreTocEnd)) == NULL)
            return ERROR_BAD_FORMAT;

        // If there are no indices, return NULL
        if(dwMaxFileIndex == 0)
            return ERROR_SUCCESS;

        // Allocate and populate the array of DIABLO3_CORE_TOC_ENTRYs
        pFileIndices = CASC_ALLOC(DIABLO3_CORE_TOC_ENTRY, dwMaxFileIndex + 1);
        if(pFileIndices != NULL)
        {
            // Initialize all entries to invalid
            memset(pFileIndices, 0xFF, (dwMaxFileIndex + 1) * sizeof(DIABLO3_CORE_TOC_ENTRY));

            // Populate the linear array with the file indices
            for(size_t i = 0; i < DIABLO3_MAX_ASSETS; i++)
            {
                PDIABLO3_CORE_TOC_ENTRY pTocEntry = (PDIABLO3_CORE_TOC_ENTRY)(pbCoreTocData + pTocHeader->EntryOffsets[i]);
                LPBYTE pbCoreTocNames = (LPBYTE)(pTocEntry + pTocHeader->EntryCounts[i]);

                // Setup the entries
                for(DWORD n = 0; n < pTocHeader->EntryCounts[i]; n++)
                {
                    DWORD dwFileIndex = pTocEntry->FileIndex;

                    pFileIndices[dwFileIndex].AssetIndex = pTocEntry->AssetIndex;
                    pFileIndices[dwFileIndex].FileIndex  = pTocEntry->FileIndex;
                    pFileIndices[dwFileIndex].NameOffset = (DWORD)(pbCoreTocNames - pbCoreTocData) + pTocEntry->NameOffset;
                    pTocEntry++;
                }
            }

            // Save the file to the root handler
            pRootHandler->pFileIndices = pFileIndices;
            pRootHandler->nFileIndices = dwMaxFileIndex;
            pRootHandler->pbCoreTocFile = pbFileData;
            pRootHandler->pbCoreTocData = pbCoreTocData;
            pRootHandler->cbCoreTocFile = cbFileData;
        }
    }
    return ERROR_SUCCESS;
}

// Packages.dat contains a list of full file names (without locale prefix).
// They are not sorted, nor they correspond to file IDs.
// Does the sort order mean something? Perhaps we could use them as listfile?
static int CreateMapOfRealNames(TRootHandler_Diablo3 * pRootHandler, const char * szFileName)
{
    PCASC_MAP pPackageMap;
    LPBYTE pbFileData;
    DWORD cbFileData = 0;
    DWORD Signature = 0;
    DWORD NumberOfNames = 0;

    // Load the entire file to memory
    pbFileData = LoadFileToMemory(pRootHandler->hs, szFileName, &cbFileData);
    if(pbFileData && cbFileData)
    {
        LPBYTE pbPackagesDat = pbFileData;
        LPBYTE pbPackagesEnd = pbFileData + cbFileData;
/*
        LPBYTE pbPackagesPtr = pbPackagesDat + 8;
        FILE * fp = fopen("E:\\Packages.dat", "wt");
        if(fp != NULL)
        {
            while(pbPackagesPtr < pbPackagesEnd)
            {
                fprintf(fp, "%s\n", pbPackagesPtr);
                pbPackagesPtr = pbPackagesPtr + strlen((char *)pbPackagesPtr) + 1;
            }
            fclose(fp);
        }
*/
        // Get the header. There is just Signature + NumberOfNames
        if((pbPackagesDat = CaptureInteger32(pbPackagesDat, pbPackagesEnd, &Signature)) == NULL)
            return ERROR_BAD_FORMAT;
        if((pbPackagesDat = CaptureInteger32(pbPackagesDat, pbPackagesEnd, &NumberOfNames)) == NULL)
            return ERROR_BAD_FORMAT;
        if(Signature != DIABLO3_PACKAGES_SIGNATURE || NumberOfNames == 0)
            return ERROR_BAD_FORMAT;

        // Create the map for fast search of the file name
        pPackageMap = Map_Create(NumberOfNames, KEY_LENGTH_STRING, 0);
        if(pPackageMap != NULL)
        {
            const char * szPackageName = (const char *)pbPackagesDat;

            // Go as long as there is something
            for(DWORD i = 0; i < NumberOfNames; i++)
            {
                // Get the file extension
                if((LPBYTE)szPackageName >= pbPackagesEnd)
                    break;

                // Insert the file name to the map. The file extension is not included
                Map_InsertString(pPackageMap, szPackageName, true);
                szPackageName = szPackageName + strlen(szPackageName) + 1;
            }

            // Store the map and the file data to the ROOT handler
            pRootHandler->pPackagesMap = pPackageMap;
            pRootHandler->pbPackagesDat = pbFileData;
            pRootHandler->cbPackagesDat = cbFileData;
        }
    }

    return ERROR_SUCCESS;
}

static void FreeLoadingStuff(TRootHandler_Diablo3 * pRootHandler)
{
    // Free the captured root sub-directories
    for(size_t i = 0; i < DIABLO3_MAX_SUBDIRS; i++)
    {
        if(pRootHandler->RootFolders[i].pbDirectoryData)
            CASC_FREE(pRootHandler->RootFolders[i].pbDirectoryData);
        pRootHandler->RootFolders[i].pbDirectoryData = NULL;
    }

    // Free the array of file indices
    if(pRootHandler->pFileIndices != NULL)
        CASC_FREE(pRootHandler->pFileIndices);
    pRootHandler->pFileIndices = NULL;

    // Free the package map
    if(pRootHandler->pPackagesMap != NULL)
        Map_Free(pRootHandler->pPackagesMap);
    pRootHandler->pPackagesMap = NULL;

    // Free the loaded CoreTOC.dat file
    if(pRootHandler->pbCoreTocFile != NULL)
        CASC_FREE(pRootHandler->pbCoreTocFile);
    pRootHandler->pbCoreTocFile = NULL;

    // Free the loaded Packages.dat file
    if(pRootHandler->pbPackagesDat != NULL)
        CASC_FREE(pRootHandler->pbPackagesDat);
    pRootHandler->pbPackagesDat = NULL;

    // Set the referenced storage handle to NULL
    pRootHandler->hs = NULL;
}


/*
static void DumpRootFile(TDumpContext * dc, LPBYTE pbFileData, LPBYTE pbFileDataEnd)
{
    char  szMD5Buffer[MD5_STRING_SIZE+1];
    DWORD dwSignature;
    DWORD dwItemCount;
    DWORD i;

    dwSignature = *(PDWORD)pbFileData;
    if(dwSignature != CASC_DIABLO3_SUBDIR_SIGNATURE)
        return;
    pbFileData += sizeof(DWORD);

    // Dump items that contain CKey + AssetId
    dwItemCount = *(PDWORD)pbFileData;
    pbFileData += sizeof(DWORD);
    for(i = 0; i < dwItemCount; i++)
    {
        PCASC_DIABLO3_ASSET_ENTRY pEntry = (PCASC_DIABLO3_ASSET_ENTRY)pbFileData;

        if((pbFileData + sizeof(*pEntry)) > pbFileDataEnd)
            return;
        pbFileData += sizeof(*pEntry);

        dump_print(dc, "%s %08X\n", StringFromMD5(pEntry->CKey, szMD5Buffer), pEntry->AssetId);
    }

    // Terminate with two newlines
    dump_print(dc, "\n");

    // Dump items that contain CKey + AssetId + FileNumber
    dwItemCount = *(PDWORD)pbFileData;
    pbFileData += sizeof(DWORD);
    for(i = 0; i < dwItemCount; i++)
    {
        PCASC_DIABLO3_ASSET_ENTRY2 pEntry = (PCASC_DIABLO3_ASSET_ENTRY2)pbFileData;

        if((pbFileData + sizeof(*pEntry)) > pbFileDataEnd)
            return;
        pbFileData += sizeof(*pEntry);

        dump_print(dc, "%s %08X %08X\n", StringFromMD5((LPBYTE)pEntry->CKey, szMD5Buffer), pEntry->AssetId, pEntry->FileNumber);
    }

    // Terminate with two newlines
    dump_print(dc, "\n");

    // Dump items that contain CKey + FileName
    dwItemCount = *(PDWORD)pbFileData;
    pbFileData += sizeof(DWORD);
    for(i = 0; i < dwItemCount; i++)
    {
        PDIABLO3_NAMED_ENTRY pEntry = (PDIABLO3_NAMED_ENTRY)pbFileData;
        DWORD dwEntrySize = VerifyNamedFileEntry(pbFileData, pbFileDataEnd);

        if((pbFileData + dwEntrySize) > pbFileDataEnd)
            return;
        pbFileData += dwEntrySize;

        dump_print(dc, "%s %s\n", StringFromMD5((LPBYTE)pEntry->CKey, szMD5Buffer), pEntry->szFileName);
    }

    dump_print(dc, "\n\n");
}
*/
//-----------------------------------------------------------------------------
// Public functions

int RootHandler_CreateDiablo3(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_Diablo3 * pRootHandler;
    DIABLO3_DIRECTORY RootDirectory;
    char szPathBuffer[MAX_PATH];
    int nError = ERROR_BAD_FORMAT;

    // Verify the header of the ROOT file
    if(CaptureDirectoryData(&RootDirectory, pbRootFile, cbRootFile) != NULL)
    {
        // Allocate the root handler object
        hs->pRootHandler = pRootHandler = CASC_ALLOC(TRootHandler_Diablo3, 1);
        if(pRootHandler == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Fill-in the handler functions
        InitRootHandler_FileTree(pRootHandler, sizeof(TRootHandler_Diablo3));

        // Also supply storage pointer
        pRootHandler->dwRootFlags |= ROOT_FLAG_HAS_NAMES;
        pRootHandler->hs = hs;

        // Allocate the generic file tree
        nError = FileTree_Create(&pRootHandler->FileTree);
        if(nError == ERROR_SUCCESS)
        {
            PATH_BUFFER PathBuffer;

            // Initialize path buffer and go parse the directory
            PathBuffer.szBegin = szPathBuffer;
            PathBuffer.szPtr = szPathBuffer;
            PathBuffer.szEnd = szPathBuffer + MAX_PATH;
            szPathBuffer[0] = 0;

            // Always parse the named entries first. They always point to a file.
            // These are entries with arbitrary names, and they do not belong to an asset
            nError = ParseDirectory_Phase1(pRootHandler, RootDirectory, PathBuffer, true);
            if(nError != ERROR_SUCCESS)
                return nError;

            // The asset entries in the ROOT file don't contain file names, but indices.
            // To convert a file index to a file name, we need to load and parse the "Base\\CoreTOC.dat" file.
            nError = CreateMapOfFileIndices(pRootHandler, "Base\\CoreTOC.dat");
            if(nError == ERROR_SUCCESS)
            {
                // The file "Base\Data_D3\PC\Misc\Packages.dat" contains the file names
                // (without level-0 and level-1 directory).
                // We can use these names for supplying the missing extensions
                CreateMapOfRealNames(pRootHandler, "Base\\Data_D3\\PC\\Misc\\Packages.dat");

                // Now parse all folders and resolve the full names
                ParseDirectory_Phase2(pRootHandler);
            }
        }

        // Free all stuff that was used during loading of the ROOT file
        FreeLoadingStuff(pRootHandler);
    }

    return nError;
}
