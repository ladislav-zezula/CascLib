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

#define DIABLO3_INVALID_FILE       0xFFFFFFFF
#define DIABLO3_MAX_ASSETS         70               // Maximum possible number of assets
#define DIABLO3_MAX_LEVEL0_LENGTH  0x10             // Maximum length of the level-0 directory name

#define INVALID_FILE_INDEX          0xFFFFFFFF
#define INVALID_ASSET_INDEX         0xFF

#define ENTRY_FLAG_DIRECTORY_ENTRY   0x80           // The file is actually a directory entry
#define ENTRY_FLAG_PLAIN_NAME        0x01           // If set, the file entry contains offset of the plain file name
#define ENTRY_FLAG_FULL_NAME         0x02           // If set, the file entry contains offset of the full name
#define ENTRY_FLAG_FLAGS_MASK        0xF0           // Mask for the entry flags
#define ENTRY_FLAG_NAME_MASK         0x0F           // Mask for the entry file name type

#define SEARCH_PHASE_NAMES         0                // Searching named entry
#define SEARCH_PHASE_FILE_IDS      1                // Searching filed by ID

// On-disk structure for a file given by file number
typedef struct _DIABLO3_FILEID1_ENTRY
{
    ENCODING_KEY EncodingKey;                       // Encoding key for the file
    DWORD FileIndex;                                // File index
} DIABLO3_FILEID1_ENTRY, *PDIABLO3_FILEID1_ENTRY;

// On-disk structure for a file given by file number and suffix
typedef struct _DIABLO3_FILEID2_ENTRY
{
    ENCODING_KEY EncodingKey;                       // Encoding key for the file
    DWORD FileIndex;                                // File index
    DWORD SubIndex;                                 // File subindex, like "SoundBank\3D Ambience\0000.smp"
} DIABLO3_FILEID2_ENTRY, *PDIABLO3_FILEID2_ENTRY;

// On-disk structure of the named entry
typedef struct _DIABLO3_NAMED_ENTRY
{
    ENCODING_KEY EncodingKey;                       // Encoding key for the file
    BYTE szFileName[1];                             // ASCIIZ file name (variable length)
} DIABLO3_NAMED_ENTRY, *PDIABLO3_NAMED_ENTRY;

// On-disk structure of CoreToc.dat header
typedef struct _DIABLO3_CORE_TOC_HEADER
{
    DWORD EntryCounts[DIABLO3_MAX_ASSETS];          // Array of number of entries (files) dor each asset (level-1 directory)
    DWORD EntryOffsets[DIABLO3_MAX_ASSETS];         // Array of offsets of each asset table, relative to data after header
    DWORD Unknowns[DIABLO3_MAX_ASSETS];             // Unknown at the moment
    DWORD Alignment;
} DIABLO3_CORE_TOC_HEADER, *PDIABLO3_CORE_TOC_HEADER;

// On-disk structure of the entry in CoreToc.dat
typedef struct _DIABLO3_CORE_TOC_ENTRY
{
    DWORD AssetIndex;                               // Index of the Diablo3 asset (aka directory)
    DWORD FileIndex;                                // File index
    DWORD NameOffset;                               // Offset of the plain file name

} DIABLO3_CORE_TOC_ENTRY, *PDIABLO3_CORE_TOC_ENTRY;

// In-memory structure of parsed directory header
typedef struct _DIABLO3_DIR_HEADER
{
    LPBYTE pbEntries1;
    LPBYTE pbEntries2;
    LPBYTE pbEntries3;
    DWORD dwEntries1;
    DWORD dwEntries2;
    DWORD dwEntries3;
} DIABLO3_DIR_HEADER, *PDIABLO3_DIR_HEADER;

// In-memory structure of loaded CoreTOC.dat
typedef struct _DIABLO3_CORE_TOC
{
    DIABLO3_CORE_TOC_HEADER Hdr;                    // Header of CoreTOC.dat

    LPBYTE pbCoreToc;                               // Content of the CoreTOC.dat file
    DIABLO3_CORE_TOC_ENTRY Entries[1];              // Buffer for storing the entries (variable length)

} DIABLO3_CORE_TOC, *PDIABLO3_CORE_TOC;

// On-disk structure of Packages.dat header
typedef struct _DIABLO3_PACKAGES_DAT_HEADER
{
    DWORD Signature;
    DWORD NumberOfNames;
} DIABLO3_PACKAGES_DAT_HEADER, *PDIABLO3_PACKAGES_DAT_HEADER;

// Structure for conversion DirectoryID -> Directory name
typedef struct _DIABLO3_ASSET_INFO
{
    const char * szDirectoryName;                   // Directory name
    const char * szExtension;

} DIABLO3_ASSET_INFO;

typedef const DIABLO3_ASSET_INFO * PDIABLO3_ASSET_INFO;

typedef struct _DIABLO3_NAME_INFO
{
    int nLengthLevel0;                              // Length of the level-0 directory, including backslash
    int nBackslashes;                               // Number of backslashes in the name
    int nNameLength;                                // Length of the file name
    int nExtension;                                 // Position of the extension
    char szNormName[MAX_PATH+1];                    // Normalized file name

} DIABLO3_NAME_INFO, *PDIABLO3_NAME_INFO;

typedef struct _CASC_FILE_ENTRY
{
    ENCODING_KEY EncodingKey;                       // Encoding key
    ULONGLONG FileNameHash;                         // File name hash
    DWORD  NameOffset;                              // Offset of the partial name or full name
    USHORT SubIndex;                                // File\SubFile index
    BYTE   AssetIndex;                              // Asset index (aka directory index)
    BYTE   EntryFlags;                              // Entry flags 
} CASC_FILE_ENTRY, *PCASC_FILE_ENTRY;

// In-memory structure of a directory
typedef struct _CASC_DIRECTORY
{
    ENCODING_KEY EncodingKey;                       // Encoding key for the directory file itself

    PDIABLO3_FILEID1_ENTRY pIndexEntries1;          // Array of index entries without subnames
    PDIABLO3_FILEID2_ENTRY pIndexEntries2;          // Array of index entries with subnames
    DWORD nIndexEntries1;                           // Number of index entries without subnames
    DWORD nIndexEntries2;                           // Number of index entries with subnames

    DWORD FileCount;                                // Number of indexes in pFileList
    DWORD Files[1];                                 // Pointer to the array of indexes to the global file table

} CASC_DIRECTORY, *PCASC_DIRECTORY;

//-----------------------------------------------------------------------------
// Structure definitions for Diablo3 root file

struct TRootHandler_Diablo3 : public TRootHandler
{
    // List of the named entries
    PCASC_DIRECTORY pRootDirectory;                 // Level-0 (root) directory 

    // List of subdirectories
    PCASC_DIRECTORY SubDirs[DIABLO3_MAX_SUBDIRS];   // PCASC_DIRECTORY pointer for each subdirectory
    DWORD dwSubDirs;

    // Global map of FNAME -> FileEntry
    PCASC_MAP pRootMap;                             

    // Linear global list of all files
    PCASC_FILE_ENTRY pFileTable;                    // Global linear list of all files
    DWORD dwFileCountMax;                           // Maximum number of files in the table
    DWORD dwFileCount;                              // Current number of files in the table

    // Linear global list of names
    char * szFileNames;                             // Pointer to global name buffer
    DWORD cbFileNamesMax;                           // Maximum size of the name buffer
    DWORD cbFileNames;                              // Current size of the name buffer
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

static const DIABLO3_ASSET_INFO UnknownAsset = {"Unknown", "xxx"};

#define DIABLO3_ASSET_COUNT (sizeof(Assets) / sizeof(Assets[0]))

//-----------------------------------------------------------------------------
// Local functions

static char * AppendPathElement(char * szBuffer, char * szBufferEnd, const char * szDirName, char chSeparator)
{
    // Copy the directory name
    while(szBuffer < szBufferEnd && szDirName[0] != 0)
        *szBuffer++ = *szDirName++;

    // Append backslash
    if(szBuffer < szBufferEnd)
        *szBuffer++ = chSeparator;
    return szBuffer;
}

static DWORD VerifyNamedFileEntry(LPBYTE pbNamedEntry, LPBYTE pbFileEnd)
{
    LPBYTE pbFileName = ((PDIABLO3_NAMED_ENTRY)pbNamedEntry)->szFileName;

    // Find the end of the name
    while(pbFileName < pbFileEnd && pbFileName[0] != 0)
        pbFileName++;

    // Did we get past the end of the root file?
    if(pbFileName >= pbFileEnd)
        return 0;
    pbFileName++;

    // Return the length of the structure
    return (DWORD)(pbFileName - pbNamedEntry);
}

static bool NormalizeFileName_Diablo3(PDIABLO3_NAME_INFO pNameInfo, const char * szRootSubDir, const char * szFileName)
{
    char * szNormName = pNameInfo->szNormName;
    int nLengthLevel0 = 0;
    int nBackslashes = 0;
    int nNameLength = 0;
    int nExtension = 0;

    // Do we have parent directory name?
    if(szRootSubDir != NULL)
    {
        // Copy the name of the parent sirectory
        while(szRootSubDir[0] != 0 && nNameLength < MAX_PATH)
        {
            szNormName[nNameLength++] = AsciiToUpperTable_BkSlash[*szRootSubDir++];
        }

        // Append backslash
        if(nNameLength < MAX_PATH)
            szNormName[nNameLength++] = '\\';
        nLengthLevel0 = nNameLength;
        nBackslashes++;
    }

    // Normalize the name. Also count number or backslashes and remember position of the extension
    while(szFileName[0] != 0 && nNameLength < MAX_PATH)
    {
        // Put the normalizet character
        szNormName[nNameLength] = AsciiToUpperTable_BkSlash[*szFileName++];

        // Take backslashes and dots into account
        if(szNormName[nNameLength] == '\\')
            nBackslashes++;
        if(szNormName[nNameLength] == '.')
            nExtension = nNameLength;
        nNameLength++;
    }

    // Terminate the name
    szNormName[nNameLength] = 0;

    // If the name had two backslashes (like "SoundBank\X1_Monster_Westmarch_Rat\0061.fsb"),
    // we need to replace the extension with "xxx"
    if(nBackslashes == 3 && (nNameLength - nExtension) == 4)
    {
        szNormName[nExtension + 1] = 'X';
        szNormName[nExtension + 2] = 'X';
        szNormName[nExtension + 3] = 'X';
    }

    // Fill-in the name info
    pNameInfo->nLengthLevel0 = nLengthLevel0;
    pNameInfo->nBackslashes = nBackslashes;
    pNameInfo->nNameLength = nNameLength;
    pNameInfo->nExtension = nExtension;
    return true;
}

static ULONGLONG CalcFileNameHashEx(PDIABLO3_NAME_INFO pNameInfo)
{
    uint32_t dwHashHigh = 0;
    uint32_t dwHashLow = 0;

    // Calculate the HASH value of the normalized file name
    hashlittle2(pNameInfo->szNormName, pNameInfo->nNameLength, &dwHashHigh, &dwHashLow);
    return ((ULONGLONG)dwHashHigh << 0x20) | dwHashLow;
}

static ULONGLONG CalcFileNameHash(const char * szRootSubDir, const char * szFileName)
{
    DIABLO3_NAME_INFO NameInfo;

    NormalizeFileName_Diablo3(&NameInfo, szRootSubDir, szFileName);
    return CalcFileNameHashEx(&NameInfo);
}

static const char * CreateFileName(
    TRootHandler_Diablo3 * pRootHandler,
    PCASC_FILE_ENTRY pParent,
    DWORD dwAssetIndex,
    DWORD dwNameOffset,
    DWORD dwSubIndex,
    char * szBuffer)
{
    PDIABLO3_ASSET_INFO pAssetInfo = &UnknownAsset;
    char * szSaveBuffer = szBuffer;
    char * szBufferEnd = szBuffer + MAX_PATH;

    // Retrieve the asset. If the asset is unknown, put a placeholder
    if(dwAssetIndex < DIABLO3_ASSET_COUNT && Assets[dwAssetIndex].szDirectoryName != NULL)
        pAssetInfo = &Assets[dwAssetIndex];

    // Append the level-0 directory name
    if(pParent != NULL)
        szBuffer = AppendPathElement(szBuffer, szBufferEnd, pRootHandler->szFileNames + pParent->NameOffset, '\\');

    // Append the level-1 directory name (asset name)
    szBuffer = AppendPathElement(szBuffer, szBufferEnd, pAssetInfo->szDirectoryName, '\\');

    // If there is no sub-index, we construct the file name from the plain name
    if(dwSubIndex == 0)
    {
        // Append plain name and extension
        szBuffer = AppendPathElement(szBuffer, szBufferEnd, pRootHandler->szFileNames + dwNameOffset, '.');
        szBuffer = AppendPathElement(szBuffer, szBufferEnd, pAssetInfo->szExtension, 0);
    }
    else
    {
        // Append plain name as subdirectory. The file name is four digits
        szBuffer = AppendPathElement(szBuffer, szBufferEnd, pRootHandler->szFileNames + dwNameOffset, '\\');
        szBuffer += sprintf(szBuffer, "%04u.xxx", dwSubIndex - 1);
    }
    
    return szSaveBuffer;
}

static const char * CreateFileName(
    TRootHandler_Diablo3 * pRootHandler,
    PCASC_FILE_ENTRY pParent,
    DWORD dwFileNameOffset,
    char * szBuffer)
{
    char * szSaveBuffer = szBuffer;
    char * szBufferEnd = szBuffer + MAX_PATH;

    // Sanity checks
    assert(dwFileNameOffset < pRootHandler->cbFileNamesMax);

    // Append the level-0 directory name
    if(pParent != NULL)
        szBuffer = AppendPathElement(szBuffer, szBufferEnd, pRootHandler->szFileNames + pParent->NameOffset, '\\');
    
    // Append the rest
    szBuffer = AppendPathElement(szBuffer, szBufferEnd, pRootHandler->szFileNames + dwFileNameOffset, 0);
    return szSaveBuffer;
}

static bool EnlargeNameBuffer(TRootHandler_Diablo3 * pRootHandler, DWORD cbNewSize)
{
    char * szFileNames;
    DWORD cbFileNamesMax;

    // We expect it to be already allocated
    assert(pRootHandler->szFileNames != NULL);
    assert(pRootHandler->cbFileNamesMax != 0);
    
    // Shall we enlarge the table?
    if(cbNewSize > pRootHandler->cbFileNamesMax)
    {
        // Calculate new table size
        cbFileNamesMax = pRootHandler->cbFileNamesMax;
        while(cbFileNamesMax < cbNewSize)
            cbFileNamesMax = cbFileNamesMax << 1;

        // Allocate new table
        szFileNames = CASC_REALLOC(char, pRootHandler->szFileNames, cbFileNamesMax);
        if(szFileNames == NULL)
            return false;

        // Set the new table size
        pRootHandler->szFileNames = szFileNames;
        pRootHandler->cbFileNamesMax = cbFileNamesMax;
    }

    return true;
}

static char * InsertNamesToBuffer(TRootHandler_Diablo3 * pRootHandler, LPBYTE pbNewNames, DWORD cbNewNames)
{
    char * szFileName;

    // Try to enlarge the buffer, if needed
    if(!EnlargeNameBuffer(pRootHandler, pRootHandler->cbFileNames + cbNewNames))
        return NULL;
    szFileName = pRootHandler->szFileNames + pRootHandler->cbFileNames;

    // Copy the file name to the name buffer
    memcpy(szFileName, pbNewNames, cbNewNames);
    pRootHandler->cbFileNames += cbNewNames;

    // Return the newly allocates name
    return szFileName;
}

static DWORD InsertFileEntry(
    TRootHandler_Diablo3 * pRootHandler,
    PCASC_FILE_ENTRY pParent,
    ENCODING_KEY & EncodingKey,
    DWORD dwAssetIndex,
    LPBYTE pbPlainName,
    DWORD dwSubIndex)
{
    PCASC_FILE_ENTRY pFileEntry = pRootHandler->pFileTable + pRootHandler->dwFileCount;
    DWORD dwTableIndex = pRootHandler->dwFileCount;
    DWORD dwNameOffset;
    char * szPlainName;    
    char szFileName[MAX_PATH+1];

    // Make sure that we don't exceed the file limit at this phase
    if(pRootHandler->dwFileCount >= pRootHandler->dwFileCountMax)
    {
        assert(false);
        return INVALID_FILE_INDEX;
    }

    // Insert the plain name to the root handler's global name list
    szPlainName = InsertNamesToBuffer(pRootHandler, pbPlainName, strlen((char *)pbPlainName) + 1);
    if(szPlainName != NULL)
    {
        // Create the full file name 
        dwNameOffset = (DWORD)(szPlainName - pRootHandler->szFileNames);
        CreateFileName(pRootHandler, pParent, dwAssetIndex, dwNameOffset, dwSubIndex, szFileName);

        // Fill the file entry
        pFileEntry->EncodingKey  = EncodingKey;
        pFileEntry->FileNameHash = CalcFileNameHash(NULL, szFileName); 
        pFileEntry->NameOffset   = dwNameOffset;
        pFileEntry->AssetIndex   = (BYTE)dwAssetIndex;
        pFileEntry->SubIndex     = (USHORT)dwSubIndex;
        pFileEntry->EntryFlags   = ENTRY_FLAG_PLAIN_NAME;
        pRootHandler->dwFileCount++;

        // Verify collisions (debug version only)
        assert(Map_FindObject(pRootHandler->pRootMap, &pFileEntry->FileNameHash, NULL) == NULL);

        // We must use the file name INCLUDING the level-0 directory name,
        // otherwise we get collisions like this:
        // enGB\SoundBank\D_A1C5RFarmerScavengerEpilogue2.sbk
        // enUS\SoundBank\D_A1C5RFarmerScavengerEpilogue2.sbk
        Map_InsertObject(pRootHandler->pRootMap, pFileEntry, &pFileEntry->FileNameHash);
    }

    return dwTableIndex;
}

static DWORD InsertFileEntry(
    TRootHandler_Diablo3 * pRootHandler,
    PCASC_FILE_ENTRY pParent,
    ENCODING_KEY & EncodingKey,
    LPBYTE pbFileName,
    DWORD cbFileName)
{
    PCASC_FILE_ENTRY pFileEntry = pRootHandler->pFileTable + pRootHandler->dwFileCount;
    char * szSubDirName;
    char * szFileName;
    DWORD dwNameOffset = pRootHandler->cbFileNames;
    DWORD dwTableIndex = pRootHandler->dwFileCount;

    // Make sure that we don't exceed the file limit at this phase
    if(pRootHandler->dwFileCount >= pRootHandler->dwFileCountMax)
    {
        assert(false);
        return INVALID_FILE_INDEX;
    }

    // First, try to copy the name to the global name buffer
    szFileName = InsertNamesToBuffer(pRootHandler, pbFileName, cbFileName);
    if(szFileName != NULL)
    {
        // Retrieve the subdirectory name
        szSubDirName = (pParent != NULL) ? pRootHandler->szFileNames + pParent->NameOffset : NULL;

        // Store the info into the file entry
        pFileEntry->EncodingKey  = EncodingKey;
        pFileEntry->FileNameHash = CalcFileNameHash(szSubDirName, szFileName);
        pFileEntry->EntryFlags   = ENTRY_FLAG_FULL_NAME;
        pFileEntry->AssetIndex   = INVALID_ASSET_INDEX;
        pFileEntry->NameOffset   = dwNameOffset;
        pFileEntry->SubIndex     = 0;
        pRootHandler->dwFileCount++;

        // Verify collisions (debug version only)
        assert(Map_FindObject(pRootHandler->pRootMap, &pFileEntry->FileNameHash, NULL) == NULL);

        // Calculate the file name hash
        Map_InsertObject(pRootHandler->pRootMap, pFileEntry, &pFileEntry->FileNameHash);
    }

    // Return the index of the created entry
    return dwTableIndex;
}

static PCASC_DIRECTORY CreateDiablo3Directory(
    PDIABLO3_DIR_HEADER pDirHeader)
{
    PCASC_DIRECTORY pDirectory;
    size_t cbToAllocate;

    // Calculate total space
    cbToAllocate = sizeof(CASC_DIRECTORY) + (pDirHeader->dwEntries1 + pDirHeader->dwEntries2 + pDirHeader->dwEntries3) * sizeof(DWORD);
    pDirectory = (PCASC_DIRECTORY)CASC_ALLOC(BYTE, cbToAllocate);
    if(pDirectory != NULL)
    {
        // Initialize the directory
        memset(pDirectory, 0, cbToAllocate);

        // Allocate array of files without sub-item
        if(pDirHeader->pbEntries1 && pDirHeader->dwEntries1)
        {
            pDirectory->pIndexEntries1 = CASC_ALLOC(DIABLO3_FILEID1_ENTRY, pDirHeader->dwEntries1);
            if(pDirectory->pIndexEntries1 != NULL)
            {
                memcpy(pDirectory->pIndexEntries1, pDirHeader->pbEntries1, sizeof(DIABLO3_FILEID1_ENTRY) * pDirHeader->dwEntries1);
                pDirectory->nIndexEntries1 = pDirHeader->dwEntries1;
            }
        }

        // Allocate array of files with sub-item
        if(pDirHeader->pbEntries2 && pDirHeader->dwEntries2)
        {
            pDirectory->pIndexEntries2 = CASC_ALLOC(DIABLO3_FILEID2_ENTRY, pDirHeader->dwEntries2);
            if(pDirectory->pIndexEntries2 != NULL)
            {
                memcpy(pDirectory->pIndexEntries2, pDirHeader->pbEntries2, sizeof(DIABLO3_FILEID2_ENTRY) * pDirHeader->dwEntries2);
                pDirectory->nIndexEntries2 = pDirHeader->dwEntries2;
            }
        }
    }

    return pDirectory;
}

static int ParseNamedFileEntries(
    TRootHandler_Diablo3 * pRootHandler,    
    PCASC_FILE_ENTRY pParent,
    PCASC_DIRECTORY pDirectory,
    LPBYTE pbNamedEntries,
    LPBYTE pbFileEnd)
{
    DWORD dwFileIndex;
    DWORD cbNamedEntry;

    // Sanity checks
    assert(pRootHandler->pFileTable != NULL);
    assert(pRootHandler->dwFileCount < pRootHandler->dwFileCountMax);

    // Parse the file entry
    while(pbNamedEntries < pbFileEnd)
    {
        PDIABLO3_NAMED_ENTRY pSrcEntry = (PDIABLO3_NAMED_ENTRY)pbNamedEntries;

        // Move to the next entry
        cbNamedEntry = VerifyNamedFileEntry(pbNamedEntries, pbFileEnd);
        if(cbNamedEntry == 0)
            return ERROR_FILE_CORRUPT;

        // Insert the named entry to the global file table
        dwFileIndex = InsertFileEntry(pRootHandler,
                                      pParent,
                                      pSrcEntry->EncodingKey,
                                      pSrcEntry->szFileName,
                                     (cbNamedEntry - sizeof(ENCODING_KEY)));
        if(dwFileIndex == INVALID_FILE_INDEX)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Insert the named entry to the directory
        pDirectory->Files[pDirectory->FileCount++] = dwFileIndex;
        pbNamedEntries += cbNamedEntry;
    }

    return ERROR_SUCCESS;
}

static void ResolveFullFileNames(
    TRootHandler_Diablo3 * pRootHandler,
    PDIABLO3_CORE_TOC pCoreToc,
    DWORD dwDirIndex)
{
    PDIABLO3_CORE_TOC_ENTRY pTocEntry;
    PDIABLO3_FILEID1_ENTRY pIndexEntry1;
    PDIABLO3_FILEID2_ENTRY pIndexEntry2;
    PCASC_FILE_ENTRY pParent;
    PCASC_DIRECTORY pSubDir;
    DWORD dwFileIndex;
    DWORD i;

    // Get the subdirectory and the file entry
    pParent = pRootHandler->pFileTable + dwDirIndex;
    pSubDir = pRootHandler->SubDirs[dwDirIndex];

    // Only if the subdirectory is valid
    if(pSubDir != NULL)
    {
        // Step 1: Resolve the file names without subnames
        for(i = 0; i < pSubDir->nIndexEntries1; i++)
        {
            // Get the pointer to the index entry without subname
            pIndexEntry1 = pSubDir->pIndexEntries1 + i;
            pTocEntry = &pCoreToc->Entries[pIndexEntry1->FileIndex];
            if(pTocEntry->FileIndex != INVALID_FILE_INDEX)
            {
                // Create new file entry in the global table
                dwFileIndex = InsertFileEntry(pRootHandler,
                                              pParent,
                                              pIndexEntry1->EncodingKey,
                                              pTocEntry->AssetIndex,
                                              pCoreToc->pbCoreToc + pTocEntry->NameOffset,
                                              0);
                
                // Also insert the file in the directory table
                if(dwFileIndex != INVALID_FILE_INDEX)
                    pSubDir->Files[pSubDir->FileCount++] = dwFileIndex;
            }
        }

        // Step 1: Resolve the file names with subnames
        for(i = 0; i < pSubDir->nIndexEntries2; i++)
        {
            // Get the pointer to the index entry without subname
            pIndexEntry2 = pSubDir->pIndexEntries2 + i;
            pTocEntry = &pCoreToc->Entries[pIndexEntry2->FileIndex];
            if(pTocEntry->FileIndex != INVALID_FILE_INDEX)
            {
                // Create new file entry in the global table
                dwFileIndex = InsertFileEntry(pRootHandler,
                                              pParent,
                                              pIndexEntry2->EncodingKey,
                                              pTocEntry->AssetIndex,
                                              pCoreToc->pbCoreToc + pTocEntry->NameOffset,
                                              pIndexEntry2->SubIndex + 1);

                // Also insert the file in the directory table
                if(dwFileIndex != INVALID_FILE_INDEX)
                    pSubDir->Files[pSubDir->FileCount++] = dwFileIndex;
            }
        }
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

static LPBYTE LoadFileToMemory(TCascStorage * hs, const char * szFileName, DWORD * pcbFileData)
{
    LPBYTE pbEncodingKey = NULL;
    LPBYTE pbFileData = NULL;

    // Try to find encoding key for the file
    pbEncodingKey = RootHandler_GetKey(hs->pRootHandler, szFileName);
    if(pbEncodingKey != NULL)
        pbFileData = LoadFileToMemory(hs, pbEncodingKey, pcbFileData);

    return pbFileData;
}

static int ParseDirectoryHeader(
    PDIABLO3_DIR_HEADER pDirHeader,
    LPBYTE pbDirFile,
    LPBYTE pbFileEnd)
{
    DWORD dwSignature = 0;

    //
    // Structure of a Diablo3 directory file
    // 1) Signature (4 bytes)
    // 2) Number of DIABLO3_FILEID1_ENTRY entries (4 bytes)
    // 3) Array of DIABLO3_FILEID1_ENTRY entries 
    // 4) Number of DIABLO3_FILEID2_ENTRY entries (4 bytes)
    // 5) Array of DIABLO3_FILEID2_ENTRY entries
    // 6) Number of DIABLO3_NAMED_ENTRY entries (4 bytes)
    // 7) Array of DIABLO3_NAMED_ENTRY entries
    //

    // Prepare the header signature
    memset(pDirHeader, 0, sizeof(DIABLO3_DIR_HEADER));

    // Get the signature
    if((pbDirFile + sizeof(DWORD)) >= pbFileEnd)
        return ERROR_BAD_FORMAT;
    dwSignature = *(PDWORD)pbDirFile;

    // Check the signature
    if(dwSignature != CASC_DIABLO3_ROOT_SIGNATURE && dwSignature != DIABLO3_SUBDIR_SIGNATURE)
        return ERROR_BAD_FORMAT;
    pbDirFile += sizeof(DWORD);

    // Subdirectories have extra two arrays
    if(dwSignature == DIABLO3_SUBDIR_SIGNATURE)
    {
        // Get the number of DIABLO3_FILEID1_ENTRY items
        if((pbDirFile + sizeof(DWORD)) >= pbFileEnd)
            return ERROR_BAD_FORMAT;
        pDirHeader->dwEntries1 = *(PDWORD)pbDirFile;

        // Get the array of DIABLO3_FILEID1_ENTRY
        pDirHeader->pbEntries1 = (pbDirFile + sizeof(DWORD));
        pbDirFile = pbDirFile + sizeof(DWORD) + pDirHeader->dwEntries1 * sizeof(DIABLO3_FILEID1_ENTRY);

        // Get the number of DIABLO3_FILEID2_ENTRY items
        if((pbDirFile + sizeof(DWORD)) >= pbFileEnd)
            return ERROR_BAD_FORMAT;
        pDirHeader->dwEntries2 = *(PDWORD)pbDirFile;

        // Get the array of DIABLO3_FILEID2_ENTRY
        pDirHeader->pbEntries2 = (pbDirFile + sizeof(DWORD));
        pbDirFile = pbDirFile + sizeof(DWORD) + pDirHeader->dwEntries2 * sizeof(DIABLO3_FILEID2_ENTRY);
    }

    // Get the pointer and length DIABLO3_NAMED_ENTRY array
    if((pbDirFile + sizeof(DWORD)) >= pbFileEnd)
        return ERROR_BAD_FORMAT;
    pDirHeader->dwEntries3 = *(PDWORD)pbDirFile;
    pDirHeader->pbEntries3 = (pbDirFile + sizeof(DWORD));
    return ERROR_SUCCESS;
}

static DWORD ScanDirectoryFile(
    TCascStorage * hs,
    LPBYTE pbRootFile,
    LPBYTE pbFileEnd)
{
    PDIABLO3_NAMED_ENTRY pNamedEntry;
    DIABLO3_DIR_HEADER RootHeader;
    DIABLO3_DIR_HEADER DirHeader;
    LPBYTE pbSubDir;
    DWORD dwTotalFileCount;
    DWORD cbNamedEntry;
    DWORD cbSubDir;
    int nError;

    // Parse the directory header in order to retrieve the items
    nError = ParseDirectoryHeader(&RootHeader, pbRootFile, pbFileEnd);
    if(nError != ERROR_SUCCESS)
        return 0;

    // Add the root directory's entries
    dwTotalFileCount = RootHeader.dwEntries1 + RootHeader.dwEntries2 + RootHeader.dwEntries3;

    // Parse the named entries
    for(DWORD i = 0; i < RootHeader.dwEntries3; i++)
    {
        // Get the this named entry
        if((cbNamedEntry = VerifyNamedFileEntry(RootHeader.pbEntries3, pbFileEnd)) == 0)
            return 0;
        pNamedEntry = (PDIABLO3_NAMED_ENTRY)RootHeader.pbEntries3;
        RootHeader.pbEntries3 += cbNamedEntry;

        // Load the subdirectory to memory
        pbSubDir = LoadFileToMemory(hs, pNamedEntry->EncodingKey.Value, &cbSubDir);
        if(pbSubDir != NULL)
        {
            // Count the files in the subdirectory
            if(ParseDirectoryHeader(&DirHeader, pbSubDir, pbSubDir + cbSubDir) == ERROR_SUCCESS)
            {
                dwTotalFileCount += DirHeader.dwEntries1 + DirHeader.dwEntries2 + DirHeader.dwEntries3;
            }

            // Free the subdirectory
            CASC_FREE(pbSubDir);
        }
    }

    // Return the total number of entries
    return dwTotalFileCount;
}

static int ParseDirectoryFile(
    TRootHandler_Diablo3 * pRootHandler,
    PCASC_FILE_ENTRY pParent,
    LPBYTE pbEncodingKey,
    LPBYTE pbDirFile,
    LPBYTE pbFileEnd,
    PCASC_DIRECTORY * PtrDirectory)
{
    DIABLO3_DIR_HEADER DirHeader;
    PCASC_DIRECTORY pDirectory;
    int nError;

    // Parse the directory header in order to retrieve the items
    nError = ParseDirectoryHeader(&DirHeader, pbDirFile, pbFileEnd);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Create the directory structure
    pDirectory = CreateDiablo3Directory(&DirHeader);
    if(pDirectory != NULL)
    {
        // Copy the encoding key
        memcpy(pDirectory->EncodingKey.Value, pbEncodingKey, MD5_HASH_SIZE);

        // Only the named entries are eligible to be inserted to the global file table
        if(DirHeader.pbEntries3 && DirHeader.dwEntries3 != NULL)
            nError = ParseNamedFileEntries(pRootHandler, pParent, pDirectory, DirHeader.pbEntries3, pbFileEnd);
    }
    else
    {
        nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Give the directory to the caller
    PtrDirectory[0] = pDirectory;
    return nError;
}

static int ParseCoreTOC(TRootHandler_Diablo3 * pRootHandler, LPBYTE pbCoreToc, LPBYTE pbCoreTocEnd)
{
    PDIABLO3_CORE_TOC_HEADER pTocHeader;
    PDIABLO3_CORE_TOC_ENTRY pTocEntry;
    PDIABLO3_CORE_TOC pCoreToc;
    LPBYTE pbCoreTocNames;
    LPBYTE pbCoreTocEntryEnd;
    size_t cbToAllocate;
    DWORD dwMaxFileIndex = 0;
    DWORD i;

    // Check the space for header
    if((pbCoreToc + sizeof(DIABLO3_CORE_TOC_HEADER)) > pbCoreTocEnd)
        return ERROR_FILE_CORRUPT;
    pTocHeader = (PDIABLO3_CORE_TOC_HEADER)pbCoreToc;
    pbCoreToc += sizeof(DIABLO3_CORE_TOC_HEADER);

    // Calculate space needed for allocation
    for(i = 0; i < DIABLO3_MAX_ASSETS; i++)
    {
        // Get the first entry
        pTocEntry = (PDIABLO3_CORE_TOC_ENTRY)(pbCoreToc + pTocHeader->EntryOffsets[i]);

        // Find out the entry tith the maximum index
        for(DWORD n = 0; n < pTocHeader->EntryCounts[i]; n++)
        {
            if(pTocEntry->FileIndex > dwMaxFileIndex)
                dwMaxFileIndex = pTocEntry->FileIndex;
            pTocEntry++;
        }
    }

    // Allocate the DIABLO3_CORE_TOC structure
    // Note that we will not copy the file names - they stay in the loaded CoreTOC.dat
    cbToAllocate = sizeof(DIABLO3_CORE_TOC) + (dwMaxFileIndex + 1) * sizeof(DIABLO3_CORE_TOC_ENTRY);
    pCoreToc = (PDIABLO3_CORE_TOC)CASC_ALLOC(BYTE, cbToAllocate);
    if(pCoreToc != NULL)
    {
        // Initialize the DIABLO3_CORE_TOC structure
        memcpy(&pCoreToc->Hdr, pTocHeader, sizeof(DIABLO3_CORE_TOC_HEADER));
        memset(pCoreToc->Entries, 0xFF, dwMaxFileIndex * sizeof(DIABLO3_CORE_TOC_ENTRY));
        pCoreToc->pbCoreToc = pbCoreToc;

        // Parse the file again and copy all entries and names
        for(i = 0; i < DIABLO3_MAX_ASSETS; i++)
        {
            // Set the pointers
            pTocEntry = (PDIABLO3_CORE_TOC_ENTRY)(pbCoreToc + pTocHeader->EntryOffsets[i]);
            pbCoreTocNames = (LPBYTE)(pTocEntry + pTocHeader->EntryCounts[i]);
            pbCoreTocEntryEnd = pbCoreTocEnd;

            // Get the end of this entry block
            if(i < DIABLO3_MAX_ASSETS - 1)
                pbCoreTocEntryEnd = pbCoreToc + pTocHeader->EntryOffsets[i+1];

            // Setup the entries
            for(DWORD n = 0; n < pTocHeader->EntryCounts[i]; n++)
            {
                pCoreToc->Entries[pTocEntry->FileIndex].AssetIndex = pTocEntry->AssetIndex;
                pCoreToc->Entries[pTocEntry->FileIndex].FileIndex  = pTocEntry->FileIndex;
                pCoreToc->Entries[pTocEntry->FileIndex].NameOffset = (DWORD)(pbCoreTocNames - pbCoreToc) + pTocEntry->NameOffset;
                pTocEntry++;
            }
        }

        // Now parse all subdirectories of the root directory and resolve names of each item
        for(i = 0; i < pRootHandler->pRootDirectory->FileCount; i++)
        {
            ResolveFullFileNames(pRootHandler, pCoreToc, i);
        }
    }

    return ERROR_SUCCESS;
}

static int ParsePackagesDat(TRootHandler_Diablo3 * pRootHandler, LPBYTE pbPackagesDat, LPBYTE pbPackagesEnd)
{
    PDIABLO3_PACKAGES_DAT_HEADER pDatHeader = (PDIABLO3_PACKAGES_DAT_HEADER)pbPackagesDat;
    PCASC_FILE_ENTRY pFileEntry;
    PCASC_FILE_ENTRY pParent;
    PCASC_DIRECTORY pSubDir;
    char * szSubDirName;
    char * szFileName;
    LPBYTE pbFileName;

    // Get the header
    if((pbPackagesDat + sizeof(DIABLO3_PACKAGES_DAT_HEADER)) >= pbPackagesEnd)
        return ERROR_BAD_FORMAT;
    pbPackagesDat += sizeof(DIABLO3_PACKAGES_DAT_HEADER);

    // Check the signature and name count
    if(pDatHeader->Signature != DIABLO3_PACKAGES_SIGNATURE)
        return ERROR_BAD_FORMAT;

    // Attempt to load the files for all level-0 directories
    for(DWORD i = 0; i < pRootHandler->pRootDirectory->FileCount; i++)
    {
        // Get pointers to subdirectory
        pParent = pRootHandler->pFileTable + i;
        pSubDir = pRootHandler->SubDirs[i];
        pbFileName = pbPackagesDat;

        // Only if that subdir is valid
        if(pSubDir != NULL)
        {
            // Check all file names
            for(DWORD n = 0; n < pDatHeader->NumberOfNames; n++)
            {
                DIABLO3_NAME_INFO NameInfo;
                ULONGLONG FileNameHash;

                // Convert the name to a normalized name
                szSubDirName = pRootHandler->szFileNames + pParent->NameOffset;
                NormalizeFileName_Diablo3(&NameInfo, szSubDirName, (char *)pbFileName);

                // If the name had three backslashes (like "Base\SoundBank\X1_Monster_Westmarch_Rat\0061.fsb"),
                // we try to find the name in the name table
                if(NameInfo.nBackslashes == 3 && (NameInfo.nNameLength - NameInfo.nExtension) == 4)
                {
                    // Calculate the hash of the normalized file name
                    FileNameHash = CalcFileNameHashEx(&NameInfo);

                    // Try to find the value
                    pFileEntry = (PCASC_FILE_ENTRY)Map_FindObject(pRootHandler->pRootMap, &FileNameHash, NULL);
                    if(pFileEntry != NULL && (pFileEntry->EntryFlags & ENTRY_FLAG_NAME_MASK) == ENTRY_FLAG_PLAIN_NAME)
                    {
                        // Insert the discovered name to the global name buffer
                        szFileName = InsertNamesToBuffer(pRootHandler, pbFileName, NameInfo.nNameLength + 1);
                        if(szFileName != NULL)
                        {
                            // Switch the file entry to hae full name
                            pFileEntry->NameOffset = (DWORD)(szFileName - pRootHandler->szFileNames);
                            pFileEntry->EntryFlags = (pFileEntry->EntryFlags & ENTRY_FLAG_FLAGS_MASK) | ENTRY_FLAG_FULL_NAME;
                        }
                    }
                }

                // Move to the next file name
                if((pbFileName + NameInfo.nNameLength + 1) >= pbPackagesEnd)
                    break;
                pbFileName = pbFileName + (NameInfo.nNameLength - NameInfo.nLengthLevel0) + 1;
            }
        }
    }

    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// Implementation of Diablo III root file

static LPBYTE D3Handler_Search(TRootHandler_Diablo3 * pRootHandler, TCascSearch * pSearch, PDWORD /* PtrFileSize */, PDWORD /* PtrLocaleFlags */)
{
    PCASC_FILE_ENTRY pRootEntry;
    PCASC_FILE_ENTRY pFileEntry;
    PCASC_DIRECTORY pRootDir = pRootHandler->pRootDirectory;
    PCASC_DIRECTORY pSubDir;

    // Are we still inside the root directory range?
    while(pSearch->IndexLevel1 < pRootDir->FileCount)
    {
        // Get the currently searched root entry
        pRootEntry = pRootHandler->pFileTable + pRootDir->Files[pSearch->IndexLevel1];
        pSubDir = pRootHandler->SubDirs[pSearch->IndexLevel1];
        if(pSubDir != NULL)
        {
            // Are we still inside the range of directory entries?
            while(pSearch->IndexLevel2 < pSubDir->FileCount)
            {
                // Get the pointer to the file entry
                pFileEntry = pRootHandler->pFileTable + pSubDir->Files[pSearch->IndexLevel2];
                pSearch->IndexLevel2++;

                // Create the file name from the file entry with full name
                if(pFileEntry->EntryFlags & ENTRY_FLAG_FULL_NAME)
                {
                    CreateFileName(pRootHandler,
                                   pRootEntry,
                                   pFileEntry->NameOffset,
                                   pSearch->szFileName);
                    return pFileEntry->EncodingKey.Value;
                }

                // Create the file name from the file entry with plain name
                if(pFileEntry->EntryFlags & ENTRY_FLAG_PLAIN_NAME)
                {
                    CreateFileName(pRootHandler,
                                   pRootEntry,
                                   pFileEntry->AssetIndex,
                                   pFileEntry->NameOffset,
                                   pFileEntry->SubIndex,
                                   pSearch->szFileName);
                    return pFileEntry->EncodingKey.Value;
                }
            }
        }

        // Move to the next level
        pSearch->IndexLevel2 = 0;
        pSearch->IndexLevel1++;
    }

    // No more entries
    return NULL;
}

static void D3Handler_EndSearch(TRootHandler_Diablo3 * /* pRootHandler */, TCascSearch * /* pSearch */)
{
    // Do nothing
}

static LPBYTE D3Handler_GetKey(TRootHandler_Diablo3 * pRootHandler, const char * szFileName)
{
    PCASC_FILE_ENTRY pFileEntry;
    ULONGLONG FileNameHash;

    // Find the file directly
    FileNameHash = CalcFileNameHash(NULL, szFileName);
    pFileEntry = (PCASC_FILE_ENTRY)Map_FindObject(pRootHandler->pRootMap, &FileNameHash, NULL);

    // Return the entry's encoding key or NULL
    return (pFileEntry != NULL) ? pFileEntry->EncodingKey.Value : NULL;
}

static void D3Handler_Close(TRootHandler_Diablo3 * pRootHandler)
{
    if(pRootHandler != NULL)
    {
        // Free the subdirectories
        for(DWORD i = 0; i < DIABLO3_MAX_SUBDIRS; i++)
        {
            if(pRootHandler->SubDirs[i] != NULL)
                CASC_FREE(pRootHandler->SubDirs[i]);
            pRootHandler->SubDirs[i] = NULL;
        }

        // Free the root directory itself
        if(pRootHandler->pRootDirectory != NULL)
            CASC_FREE(pRootHandler->pRootDirectory);
        pRootHandler->pRootDirectory = NULL;

        // Free the file map
        if(pRootHandler->pRootMap)
            Map_Free(pRootHandler->pRootMap);
        pRootHandler->pRootMap = NULL;

        // Free the file table
        if(pRootHandler->pFileTable != NULL)
            CASC_FREE(pRootHandler->pFileTable);
        pRootHandler->pFileTable = NULL;

        // Free the array of the file names
        if(pRootHandler->szFileNames)
            CASC_FREE(pRootHandler->szFileNames);
        pRootHandler->szFileNames = NULL;

        // Free the root file itself
        CASC_FREE(pRootHandler);
    }
}

/*
static void DumpRootFile(TDumpContext * dc, LPBYTE pbFileData, LPBYTE pbFileDataEnd)
{
    char  szMD5Buffer[MD5_STRING_SIZE];
    DWORD dwSignature;
    DWORD dwItemCount;
    DWORD i;

    dwSignature = *(PDWORD)pbFileData;
    if(dwSignature != CASC_DIABLO3_SUBDIR_SIGNATURE)
        return;
    pbFileData += sizeof(DWORD);

    // Dump items that contain EncodingKey + AssetId
    dwItemCount = *(PDWORD)pbFileData;
    pbFileData += sizeof(DWORD);
    for(i = 0; i < dwItemCount; i++)
    {
        PCASC_DIABLO3_ASSET_ENTRY pEntry = (PCASC_DIABLO3_ASSET_ENTRY)pbFileData;

        if((pbFileData + sizeof(*pEntry)) > pbFileDataEnd)
            return;
        pbFileData += sizeof(*pEntry);

        dump_print(dc, "%s %08X\n", StringFromMD5(pEntry->EncodingKey, szMD5Buffer), pEntry->AssetId);
    }

    // Terminate with two newlines
    dump_print(dc, "\n");

    // Dump items that contain EncodingKey + AssetId + FileNumber
    dwItemCount = *(PDWORD)pbFileData;
    pbFileData += sizeof(DWORD);
    for(i = 0; i < dwItemCount; i++)
    {
        PCASC_DIABLO3_ASSET_ENTRY2 pEntry = (PCASC_DIABLO3_ASSET_ENTRY2)pbFileData;

        if((pbFileData + sizeof(*pEntry)) > pbFileDataEnd)
            return;
        pbFileData += sizeof(*pEntry);

        dump_print(dc, "%s %08X %08X\n", StringFromMD5((LPBYTE)pEntry->EncodingKey, szMD5Buffer), pEntry->AssetId, pEntry->FileNumber);
    }

    // Terminate with two newlines
    dump_print(dc, "\n");

    // Dump items that contain EncodingKey + FileName
    dwItemCount = *(PDWORD)pbFileData;
    pbFileData += sizeof(DWORD);
    for(i = 0; i < dwItemCount; i++)
    {
        PDIABLO3_NAMED_ENTRY pEntry = (PDIABLO3_NAMED_ENTRY)pbFileData;
        DWORD dwEntrySize = VerifyNamedFileEntry(pbFileData, pbFileDataEnd);

        if((pbFileData + dwEntrySize) > pbFileDataEnd)
            return;
        pbFileData += dwEntrySize;

        dump_print(dc, "%s %s\n", StringFromMD5((LPBYTE)pEntry->EncodingKey, szMD5Buffer), pEntry->szFileName);
    }

    dump_print(dc, "\n\n");
}
*/
//-----------------------------------------------------------------------------
// Public functions

int RootHandler_CreateDiablo3(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_Diablo3 * pRootHandler;
    LPBYTE pbRootFileEnd = pbRootFile + cbRootFile;
    DWORD dwTotalFileCount;
    DWORD dwRootEntries = 0;
    DWORD i;
    int nError;

    // Allocate the root handler object
    pRootHandler = CASC_ALLOC(TRootHandler_Diablo3, 1);
    if(pRootHandler == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill-in the handler functions
    memset(pRootHandler, 0, sizeof(TRootHandler_Diablo3));
    pRootHandler->Search      = (ROOT_SEARCH)D3Handler_Search;
    pRootHandler->EndSearch   = (ROOT_ENDSEARCH)D3Handler_EndSearch;
    pRootHandler->GetKey      = (ROOT_GETKEY)D3Handler_GetKey;
    pRootHandler->Close       = (ROOT_CLOSE)D3Handler_Close;

    // Fill-in the flags
    pRootHandler->dwRootFlags |= ROOT_FLAG_HAS_NAMES;
    hs->pRootHandler = pRootHandler;

    // Scan the total number of files in the root directories
    dwTotalFileCount = ScanDirectoryFile(hs, pbRootFile, pbRootFileEnd);
    if(dwTotalFileCount == 0)
        return ERROR_FILE_CORRUPT;

    // Allocate global buffer for file names
    pRootHandler->szFileNames = CASC_ALLOC(char, 0x10000);
    if(pRootHandler->szFileNames == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    pRootHandler->cbFileNamesMax = 0x10000;

    // Allocate the global linear file table
    // Note: This is about 18 MB of memory for Diablo III PTR build 30013
    pRootHandler->pFileTable = CASC_ALLOC(CASC_FILE_ENTRY, dwTotalFileCount);
    if(pRootHandler->pFileTable == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    pRootHandler->dwFileCountMax = dwTotalFileCount;

    // Create map of ROOT_ENTRY -> FileEntry
    pRootHandler->pRootMap = Map_Create(dwTotalFileCount, sizeof(ULONGLONG), FIELD_OFFSET(CASC_FILE_ENTRY, FileNameHash));
    if(pRootHandler->pRootMap == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Create the root directory
    nError = ParseDirectoryFile(pRootHandler, NULL, hs->RootKey.pbData, pbRootFile, pbRootFileEnd, &pRootHandler->pRootDirectory);
    if(nError == ERROR_SUCCESS)
    {
        PCASC_FILE_ENTRY pRootEntry = pRootHandler->pFileTable;

        // We expect the number of subdirectories to be less than maximum
        dwRootEntries = pRootHandler->pRootDirectory->FileCount;
        assert(dwRootEntries < DIABLO3_MAX_SUBDIRS);

        // Now parse the all root items and load them
        for(i = 0; i < dwRootEntries; i++, pRootEntry++)
        {
            // Mark the root entry as directory
            pRootEntry->EntryFlags |= ENTRY_FLAG_DIRECTORY_ENTRY;

            // Load the entire file to memory
            pbRootFile = LoadFileToMemory(hs, pRootEntry->EncodingKey.Value, &cbRootFile);
            if(pbRootFile != NULL)
            {
                PCASC_DIRECTORY pSubDir = NULL;

                nError = ParseDirectoryFile(pRootHandler, 
                                            pRootEntry,
                                            pRootEntry->EncodingKey.Value,
                                            pbRootFile,
                                            pbRootFile + cbRootFile,
                                           &pSubDir);
                
                if(nError == ERROR_SUCCESS && pSubDir != NULL)
                    pRootHandler->SubDirs[i] = pSubDir;

                CASC_FREE(pbRootFile);
            }
        }
    }

    // Vast majorify of files at this moment don't have names.
    // We can load the Base\CoreToC.dat file in order
    // to get directory asset indexes, file names and extensions
    if(nError == ERROR_SUCCESS)
    {
        LPBYTE pbCoreTOC;
        DWORD cbCoreTOC = 0;

        // Load the entire file to memory
        pbCoreTOC = LoadFileToMemory(hs, "Base\\CoreTOC.dat", &cbCoreTOC);
        if(pbCoreTOC != NULL)
        {
            ParseCoreTOC(pRootHandler, pbCoreTOC, pbCoreTOC + cbCoreTOC);
            CASC_FREE(pbCoreTOC);
        }
    }

    // Load the file Base\Data_D3\PC\Misc\Packages.dat and fixup
    // the names with sub-items
    if(nError == ERROR_SUCCESS)
    {
        LPBYTE pbPackagesDat;
        DWORD cbPackagesDat = 0;

        // Load the entire file to memory
        pbPackagesDat = LoadFileToMemory(hs, "Base\\Data_D3\\PC\\Misc\\Packages.dat", &cbPackagesDat);
        if(pbPackagesDat != NULL)
        {
            ParsePackagesDat(pRootHandler, pbPackagesDat, pbPackagesDat + cbPackagesDat);
            CASC_FREE(pbPackagesDat);
        }
    }

    // Free all remaining file ID lists
    for(i = 0; i < dwRootEntries; i++)
    {
        PCASC_DIRECTORY pSubDir = pRootHandler->SubDirs[i];

        if(pSubDir != NULL)
        {
            if(pSubDir->pIndexEntries1 != NULL)
                CASC_FREE(pSubDir->pIndexEntries1);
            if(pSubDir->pIndexEntries2 != NULL)
                CASC_FREE(pSubDir->pIndexEntries2);

            pSubDir->pIndexEntries1 = NULL;
            pSubDir->pIndexEntries2 = NULL;
            pSubDir->nIndexEntries1 = 0;
            pSubDir->nIndexEntries2 = 0;
        }
    }

    return nError;
}
