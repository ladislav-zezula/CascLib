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
#define DIABLO3_MAX_SUBDIRS        0x20

#define DIABLO3_INVALID_FILE       0xFFFFFFFF
#define DIABLO3_MAX_ASSETS         70               // Maximum possible number of assets
#define DIABLO3_MAX_LEVEL0_LENGTH  0x10             // Maximum length of the level-0 directory name

#define INVALID_FILE_INDEX          0xFFFFFFFF
#define INVALID_ASSET_INDEX         0xFF

#define ENTRY_FLAG_DIRECTORY_ENTRY   0x80           // The file is actually a directory entry
#define ENTRY_FLAG_PLAIN_NAME        0x01           // If set, the file entry contains offset of the plain file name
#define ENTRY_FLAG_FULL_NAME         0x02           // If set, the file entry contains offset of the full name
#define ENTRY_FLAG_NAME_MASK         0x0F           // If set, the file entry contains offset of the full name

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
    char szFileName[1];                             // ASCIIZ file name (variable length)
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

// Structure for conversion DirectoryID -> Directory name
typedef struct _DIABLO3_ASSET_INFO
{
    const char * szDirectoryName;                   // Directory name
    const char * szExtension;

} DIABLO3_ASSET_INFO;

typedef const DIABLO3_ASSET_INFO * PDIABLO3_ASSET_INFO;

typedef struct _CASC_FILE_ENTRY
{
    ENCODING_KEY EncodingKey;                       // Encoding key
    ULONGLONG FileNameHash;                         // File name hash
    DWORD FileIndex;                                // FileIndex or partial name offset or full name offset
    USHORT SubIndex;                                // File\SubFile index
    BYTE   AssetIndex;                              // Asset index (aka directory index)
    BYTE   EntryFlags;                              // Entry flags 
} CASC_FILE_ENTRY, *PCASC_FILE_ENTRY;

// In-memory structure of a directory
typedef struct _CASC_DIRECTORY
{
    ENCODING_KEY EncodingKey;                       // Encoding key for the directory file itself

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

    // Map of FNAME -> FileEntry
    PCASC_MAP pRootMap;                             

    // Linear global list of all files
    PCASC_FILE_ENTRY pFileTable;                    // Global linear list of all files
    DWORD dwMaxFileIndex;                           // Maximum file index
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

static const char * ExtractDirectoryName0(const char * szFileName, char * szBuffer)
{
    const char * szSaveFileName = szFileName;

    // Find the next path part
    while(szFileName[0] != 0 && szFileName[0] != '\\' && szFileName[0] != '/')
    {
        if((szFileName - szSaveFileName) > DIABLO3_MAX_LEVEL0_LENGTH)
            return NULL;
        *szBuffer++ = *szFileName++;
    }

    // If we found a sub-dir, move it by one character
    if(szFileName[0] != '\\' || szFileName[0] != '/')
        szFileName++;
    *szBuffer = 0;

    return szFileName;
}

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
    LPBYTE pbFileName = (LPBYTE)((PDIABLO3_NAMED_ENTRY)pbNamedEntry)->szFileName;

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

static ULONGLONG CalcFileNameHash(const char * szFileName)
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
    return FileNameHash;
}

static LPBYTE GetCoreTocNameArray(PDIABLO3_CORE_TOC_HEADER pTocHeader, LPBYTE pbCoreTocEnd, DWORD dwIndex, PDWORD PtrNameArraySize)
{
    LPBYTE pbNameArrayEnd = NULL;
    LPBYTE pbNameArray = NULL;
    LPBYTE pbCoreToc = (LPBYTE)(pTocHeader + 1);
    DWORD dwEntryOffset = pTocHeader->EntryOffsets[dwIndex];
    DWORD dwEntryCount = pTocHeader->EntryCounts[dwIndex];
    DWORD cbNameArraySize = 0;

    // Are there some entries?
    if(dwEntryCount != 0)
    {
        // Get the begin of the array
        pbNameArrayEnd = pbCoreTocEnd;
        pbNameArray = pbCoreToc + dwEntryOffset + (dwEntryCount * sizeof(DIABLO3_CORE_TOC_ENTRY));
        
        // Get the end of the name array
        if(dwIndex < (DIABLO3_MAX_ASSETS - 1))
        {
            dwEntryOffset = pTocHeader->EntryOffsets[dwIndex + 1];
            pbNameArrayEnd = pbCoreToc + dwEntryOffset;
        }

        cbNameArraySize = (DWORD)(pbNameArrayEnd - pbNameArray);
    }

    // Give the results to the caller
    if(PtrNameArraySize != NULL)
        PtrNameArraySize[0] = cbNameArraySize;
    return pbNameArray;
}

static const char * CreateFileName(
    TRootHandler_Diablo3 * pRootHandler,
    PCASC_FILE_ENTRY pFileEntry,
    char * szBuffer,
    size_t cbBuffer)
{
    const char * szFileName;
    char * szBufferEnd;

    // If the file entry has full name, use it as it is
    if((pFileEntry->EntryFlags & ENTRY_FLAG_NAME_MASK) == ENTRY_FLAG_FULL_NAME)
    {
        return pRootHandler->szFileNames + pFileEntry->FileIndex;
    }

    // If the file entry has partial name, we need to construct the name
    if((pFileEntry->EntryFlags & ENTRY_FLAG_NAME_MASK) == ENTRY_FLAG_PLAIN_NAME)
    {
        PDIABLO3_ASSET_INFO pAssetInfo = &UnknownAsset;

        // Set the buffer
        szBufferEnd = szBuffer + cbBuffer;
        szFileName = szBuffer;

        // Retrieve the asset. If the asset is unknown, put a placeholder
        if(pFileEntry->AssetIndex < DIABLO3_ASSET_COUNT && Assets[pFileEntry->AssetIndex].szDirectoryName != NULL)
            pAssetInfo = &Assets[pFileEntry->AssetIndex];

        // Append the asset name (aka level-1 directory)
        szBuffer = AppendPathElement(szBuffer, szBufferEnd, pAssetInfo->szDirectoryName, '\\');

        // If there is no sub-index, we construct the file name from the plain name
        if(pFileEntry->SubIndex == 0)
        {
            // Append plain name and extension
            szBuffer = AppendPathElement(szBuffer, szBufferEnd, pRootHandler->szFileNames + pFileEntry->FileIndex, '.');
            szBuffer = AppendPathElement(szBuffer, szBufferEnd, pAssetInfo->szExtension, 0);
            
            // Terminate the name and exit
            szBuffer[0] = 0;
            return szFileName;
        }

        // Append plain name as subdirectory. The file name is four digits
        szBuffer = AppendPathElement(szBuffer, szBufferEnd, pRootHandler->szFileNames + pFileEntry->FileIndex, '\\');
        szBuffer += sprintf(szBuffer, "%04u.xxx", pFileEntry->SubIndex - 1);
        return szFileName;
    }

    return NULL;
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

static DWORD InsertIndexEntry(
    TRootHandler_Diablo3 * pRootHandler,
    ENCODING_KEY & EncodingKey,
    DWORD dwFileIndex,
    DWORD dwSubIndex)
{
    PCASC_FILE_ENTRY pFileEntry = pRootHandler->pFileTable + pRootHandler->dwFileCount;
    DWORD dwTableIndex = pRootHandler->dwFileCount;

    // Keep an eye of the maximum file index found
    if(dwFileIndex > pRootHandler->dwMaxFileIndex)
        pRootHandler->dwMaxFileIndex = dwFileIndex;

    // Fill the file entry
    pFileEntry->EncodingKey  = EncodingKey;
    pFileEntry->FileNameHash = 0;
    pFileEntry->FileIndex    = dwFileIndex;
    pFileEntry->AssetIndex   = INVALID_ASSET_INDEX;
    pFileEntry->SubIndex     = (USHORT)dwSubIndex;
    pFileEntry->EntryFlags   = 0;
    pRootHandler->dwFileCount++;

    return dwTableIndex;
}

static DWORD InsertNamedEntry(
    TRootHandler_Diablo3 * pRootHandler,
    PDIABLO3_NAMED_ENTRY pNamedEntry,
    DWORD cbNamedEntry)
{
    PCASC_FILE_ENTRY pFileEntry = pRootHandler->pFileTable + pRootHandler->dwFileCount;
    char * szFileName;
    DWORD dwNameOffset = pRootHandler->cbFileNames;
    DWORD dwTableIndex = pRootHandler->dwFileCount;
    DWORD cbFileName = cbNamedEntry - sizeof(ENCODING_KEY);

    // First, try to copy the name to the global name buffer
    if(!EnlargeNameBuffer(pRootHandler, pRootHandler->cbFileNames + cbFileName))
        return INVALID_FILE_INDEX;

    // Copy the file name to the name buffer
    memcpy(pRootHandler->szFileNames + pRootHandler->cbFileNames, pNamedEntry->szFileName, cbFileName);
    szFileName = pRootHandler->szFileNames + pRootHandler->cbFileNames;
    pRootHandler->cbFileNames += cbFileName;

    // Store the info into the file entry
    pFileEntry->EncodingKey  = pNamedEntry->EncodingKey;
    pFileEntry->FileNameHash = CalcFileNameHash(szFileName);
    pFileEntry->EntryFlags   = ENTRY_FLAG_FULL_NAME;
    pFileEntry->AssetIndex   = INVALID_ASSET_INDEX;
    pFileEntry->FileIndex    = dwNameOffset;
    pFileEntry->SubIndex     = 0;
    pRootHandler->dwFileCount++;

    // Calculate the file name hash
    Map_InsertObject(pRootHandler->pRootMap, pFileEntry, &pFileEntry->FileNameHash);

    // Return the index of the created entry
    return dwTableIndex;
}

static void InsertPlainNamedEntry(
    TRootHandler_Diablo3 * pRootHandler,
    PCASC_FILE_ENTRY pFileEntry)
{
    const char * szFileName;
    char szNameBuff[MAX_PATH+1];

    // Only if the entry was not inserted before
    if(pFileEntry->FileNameHash == 0)
    {
        // Construct the file name
        szFileName = CreateFileName(pRootHandler, pFileEntry, szNameBuff, MAX_PATH);
        if(szFileName != NULL)
        {
            // Calculate the file name hash
            pFileEntry->FileNameHash = CalcFileNameHash(szFileName);

            // Calculate the file name hash
            Map_InsertObject(pRootHandler->pRootMap, pFileEntry, &pFileEntry->FileNameHash);
        }
    }
}

static int ParseIndexEntries1(
    TRootHandler_Diablo3 * pRootHandler,    
    PCASC_DIRECTORY pDirectory,
    LPBYTE pbIndexEntries,
    LPBYTE pbFileEnd)
{
    DWORD dwFileIndex;

    // Sanity checks
    assert(pRootHandler->pFileTable != NULL);
    assert(pRootHandler->dwFileCount < pRootHandler->dwFileCountMax);

    // Parse the file entry
    while(pbIndexEntries < pbFileEnd)
    {
        PDIABLO3_FILEID1_ENTRY pSrcEntry = (PDIABLO3_FILEID1_ENTRY)pbIndexEntries;

        // Insert the named entry to the global file table
        dwFileIndex = InsertIndexEntry(pRootHandler, pSrcEntry->EncodingKey, pSrcEntry->FileIndex, 0);
        if(dwFileIndex == INVALID_FILE_INDEX)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Insert the named entry to the directory
        pDirectory->Files[pDirectory->FileCount++] = dwFileIndex;
        pbIndexEntries += sizeof(DIABLO3_FILEID1_ENTRY);
    }

    return ERROR_SUCCESS;
}

static int ParseIndexEntries2(
    TRootHandler_Diablo3 * pRootHandler,    
    PCASC_DIRECTORY pDirectory,
    LPBYTE pbIndexEntries,
    LPBYTE pbFileEnd)
{
    DWORD dwFileIndex;

    // Sanity checks
    assert(pRootHandler->pFileTable != NULL);
    assert(pRootHandler->dwFileCount < pRootHandler->dwFileCountMax);

    // Parse the file entry
    while(pbIndexEntries < pbFileEnd)
    {
        PDIABLO3_FILEID2_ENTRY pSrcEntry = (PDIABLO3_FILEID2_ENTRY)pbIndexEntries;

        // Insert the named entry to the global file table
        dwFileIndex = InsertIndexEntry(pRootHandler, pSrcEntry->EncodingKey, pSrcEntry->FileIndex, pSrcEntry->SubIndex + 1);
        if(dwFileIndex == INVALID_FILE_INDEX)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Insert the named entry to the directory
        pDirectory->Files[pDirectory->FileCount++] = dwFileIndex;
        pbIndexEntries += sizeof(DIABLO3_FILEID2_ENTRY);
    }

    return ERROR_SUCCESS;
}

static int ParseNamedFileEntries(
    TRootHandler_Diablo3 * pRootHandler,    
    PCASC_DIRECTORY pDirectory,
    LPBYTE pbNamedEntries,
    LPBYTE pbFileEnd)
{
    PCASC_FILE_ENTRY pFileEntry;
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
        dwFileIndex = InsertNamedEntry(pRootHandler, pSrcEntry, cbNamedEntry);
        if(dwFileIndex == INVALID_FILE_INDEX)
            return ERROR_NOT_ENOUGH_MEMORY;
        pFileEntry = pRootHandler->pFileTable + dwFileIndex;

        // Insert the named entry to the directory
        pDirectory->Files[pDirectory->FileCount++] = dwFileIndex;
        pbNamedEntries += cbNamedEntry;
    }

    return ERROR_SUCCESS;
}

static int ParseDirectoryFile(
    TRootHandler_Diablo3 * pRootHandler,
    LPBYTE pbDirFile,
    LPBYTE pbFileEnd,
    PCASC_DIRECTORY * PtrDirectory)
{
    PCASC_DIRECTORY pDirectory;
    LPBYTE pbIndexEntries1 = NULL;
    LPBYTE pbIndexEntries2 = NULL;
    LPBYTE pbNamedEntries = NULL;
    size_t cbToAllocate;
    DWORD dwDirSignature = 0;
    DWORD dwIndexEntries1 = 0;
    DWORD dwIndexEntries2 = 0;
    DWORD dwNamedEntries = 0;
    int nError = ERROR_SUCCESS;

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

    // Get the signature
    if((pbDirFile + sizeof(DWORD)) <= pbFileEnd)
        dwDirSignature = *(PDWORD)pbDirFile;
    pbDirFile += sizeof(DWORD);

    // Check the signature
    if(dwDirSignature != CASC_DIABLO3_ROOT_SIGNATURE && dwDirSignature != DIABLO3_SUBDIR_SIGNATURE)
        return ERROR_BAD_FORMAT;

    if(dwDirSignature == DIABLO3_SUBDIR_SIGNATURE)
    {
        // Get the pointer and length DIABLO3_FILEID1_ENTRY array
        if((pbDirFile + sizeof(DWORD)) < pbFileEnd)
            dwIndexEntries1 = *(PDWORD)pbDirFile;
        pbIndexEntries1 = pbDirFile + sizeof(DWORD);
        pbDirFile = pbIndexEntries1 + dwIndexEntries1 * sizeof(DIABLO3_FILEID1_ENTRY);

        // Get the pointer and length DIABLO3_FILEID2_ENTRY array
        if((pbDirFile + sizeof(DWORD)) < pbFileEnd)
            dwIndexEntries2 = *(PDWORD)pbDirFile;
        pbIndexEntries2 = pbDirFile + sizeof(DWORD);
        pbDirFile = pbIndexEntries2 + dwIndexEntries2 * sizeof(DIABLO3_FILEID2_ENTRY);
    }

    // Get the pointer and length DIABLO3_NAMED_ENTRY array
    if((pbDirFile + sizeof(DWORD)) < pbFileEnd)
        dwNamedEntries = *(PDWORD)pbDirFile;
    pbNamedEntries = pbDirFile + sizeof(DWORD);
//  pbDirFile = pbNamedEntries + dwNamedEntries * sizeof(DIABLO3_NAMED_ENTRY);
    
    // Allocate directory
    cbToAllocate = sizeof(CASC_DIRECTORY) + (dwIndexEntries1 + dwIndexEntries2 + dwNamedEntries) * sizeof(DWORD);
    pDirectory = (PCASC_DIRECTORY)CASC_ALLOC(BYTE, cbToAllocate);
    if(pDirectory != NULL)
    {
        // Fill it with zeroes
        memset(pDirectory, 0, cbToAllocate);
        PtrDirectory[0] = pDirectory;

        // Parse array of entries which contain EncodingKey + FileIndex
        if(pbIndexEntries1 != NULL)
        {
            nError = ParseIndexEntries1(pRootHandler, pDirectory, pbIndexEntries1, pbIndexEntries1 + dwIndexEntries1 * sizeof(DIABLO3_FILEID1_ENTRY));
            if(nError != ERROR_SUCCESS)
                return nError;
        }
        
        // Parse array of entries which contain EncodingKey + FileIndex + SubIndex
        if(pbIndexEntries2 != NULL)
        {
            nError = ParseIndexEntries2(pRootHandler, pDirectory, pbIndexEntries2, pbIndexEntries2 + dwIndexEntries2 * sizeof(DIABLO3_FILEID2_ENTRY));
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        // Parse array of entries which contain EncodingKey + FileName
        if(pbNamedEntries != NULL)
        {
            nError = ParseNamedFileEntries(pRootHandler, pDirectory, pbNamedEntries, pbFileEnd);
            if(nError != ERROR_SUCCESS)
                return nError;
        }
    }
    else
    {
        nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Give the directory to the caller
    return nError;
}

static int ParseCoreTOC(TRootHandler_Diablo3 * pRootHandler, LPBYTE pbCoreToc, LPBYTE pbCoreTocEnd)
{
    PDIABLO3_CORE_TOC_HEADER pTocHeader;
    PDIABLO3_CORE_TOC_ENTRY pTocTable;
    PCASC_FILE_ENTRY pFileEntry;
    LPBYTE pbCoreTocEntry;
    LPBYTE pbNameArray;
    char * szFileNamesEnd;
    char * szFileNames;
    DWORD cbNameBufferSize = 0;
    DWORD dwTotalFiles = 0;
    DWORD dwFileIndex;
    DWORD i;

    // Check the space for header
    if((pbCoreToc + sizeof(DIABLO3_CORE_TOC_HEADER)) > pbCoreTocEnd)
        return ERROR_FILE_CORRUPT;

    // Get the header itself
    pTocHeader = (PDIABLO3_CORE_TOC_HEADER)pbCoreToc;
    pbCoreToc += sizeof(DIABLO3_CORE_TOC_HEADER);

    // Determine the total file count and size of name buffers
    for(DWORD i = 0; i < DIABLO3_MAX_ASSETS; i++)
    {
        DWORD cbFileNames = 0;

        GetCoreTocNameArray(pTocHeader, pbCoreTocEnd, i, &cbFileNames);
        cbNameBufferSize += cbFileNames;
        dwTotalFiles += pTocHeader->EntryCounts[i];
    }

    // Make sure that we have enough space in the global name buffer
    EnlargeNameBuffer(pRootHandler, pRootHandler->cbFileNames + cbNameBufferSize);

    // Get the range of the name buffer
    szFileNamesEnd = pRootHandler->szFileNames + pRootHandler->cbFileNamesMax;
    szFileNames = pRootHandler->szFileNames + pRootHandler->cbFileNames;

    // Allocate the helper map for FileIndex -> CoreTocEntry
    pTocTable = CASC_ALLOC(DIABLO3_CORE_TOC_ENTRY, pRootHandler->dwMaxFileIndex + 1);
    if(pTocTable != NULL)
    {
        // Fill-in the entire table with zeros
        memset(pTocTable, 0, sizeof(DIABLO3_CORE_TOC_ENTRY) * (pRootHandler->dwMaxFileIndex + 1));

        // Now parse all subitems
        for(i = 0; i < DIABLO3_MAX_ASSETS; i++)
        {
            PDIABLO3_CORE_TOC_ENTRY pTocEntry;
            DWORD dwEntryOffset = pTocHeader->EntryOffsets[i];
            DWORD dwEntryCount = pTocHeader->EntryCounts[i];
            DWORD cbFileNames = 0;

            // Get pointer to the first entry
            pbCoreTocEntry = pbCoreToc + dwEntryOffset;
            pTocEntry = (PDIABLO3_CORE_TOC_ENTRY)pbCoreTocEntry;

            // Get the range of the name array
            pbNameArray = GetCoreTocNameArray(pTocHeader, pbCoreTocEnd, i, &cbFileNames);
            memcpy(szFileNames, pbNameArray, cbFileNames);

            // Get the entry count
            for(DWORD n = 0; n < dwEntryCount; n++, pTocEntry++)
            {
                // Verify range
                if((LPBYTE)(pTocEntry + 1) > pbCoreTocEnd)
                    break;
                
                // Get and verify the file index range
                assert(pTocEntry->FileIndex <= pRootHandler->dwMaxFileIndex);
                dwFileIndex = pTocEntry->FileIndex;

                // Fill the TOC entry at that index
                if(dwFileIndex <= pRootHandler->dwMaxFileIndex)
                {
                    pTocTable[dwFileIndex].AssetIndex = pTocEntry->AssetIndex;
                    pTocTable[dwFileIndex].FileIndex  = pTocEntry->FileIndex;
                    pTocTable[dwFileIndex].NameOffset = (DWORD)(szFileNames - pRootHandler->szFileNames) + pTocEntry->NameOffset;
                }
            }

            // Move the file names to the next block
            szFileNames += cbFileNames;
            assert(szFileNames <= szFileNamesEnd);
        }

        // Go through the entire file table and assign the found entries
        pFileEntry = pRootHandler->pFileTable;
        for(i = 0; i < pRootHandler->dwFileCount; i++, pFileEntry++)
        {
            // Get the file index
            dwFileIndex = pFileEntry->FileIndex;

            // The file index must exist in the CoreToc table
            if((pFileEntry->EntryFlags & ENTRY_FLAG_NAME_MASK) == 0 && pTocTable[dwFileIndex].NameOffset != 0)
            {
                // Setup the entry
                pFileEntry->AssetIndex = (BYTE)pTocTable[dwFileIndex].AssetIndex;
                pFileEntry->FileIndex  = pTocTable[dwFileIndex].NameOffset;
                pFileEntry->EntryFlags |= ENTRY_FLAG_PLAIN_NAME;

                // We can now insert the item to the map of FNAME -> FileEntry
                InsertPlainNamedEntry(pRootHandler, pFileEntry);
            }
        }

        // Free the helper table
        CASC_FREE(pTocTable);
    }
    
    return ERROR_SUCCESS;
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

//-----------------------------------------------------------------------------
// Implementation of Diablo III root file

static LPBYTE D3Handler_Search(TRootHandler_Diablo3 * pRootHandler, TCascSearch * pSearch, PDWORD /* PtrFileSize */, PDWORD /* PtrLocaleFlags */)
{
    PCASC_FILE_ENTRY pRootEntry;
    PCASC_FILE_ENTRY pFileEntry;
    PCASC_DIRECTORY pRootDir = pRootHandler->pRootDirectory;
    PCASC_DIRECTORY pSubDir;
    const char * szName0;
    const char * szName1;
    char * szNameBufferEnd = pSearch->szFileName + MAX_PATH;
    char * szNameBuffer = pSearch->szFileName;
    char szBuffer[MAX_PATH+1];

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
                // Get the index of the file entry
                pFileEntry = pRootHandler->pFileTable + pSubDir->Files[pSearch->IndexLevel2];
                pSearch->IndexLevel2++;

                // Append the level-0 directory file name 
                szName0 = pRootHandler->szFileNames + pRootEntry->FileIndex;
                szNameBuffer = AppendPathElement(szNameBuffer, szNameBufferEnd, szName0, '\\');

                // Get the rest of the name
                szName1 = CreateFileName(pRootHandler, pFileEntry, szBuffer, MAX_PATH);
                szNameBuffer = AppendPathElement(szNameBuffer, szNameBufferEnd, szName1, 0);
                return pFileEntry->EncodingKey.Value;
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
    char szDirName0[DIABLO3_MAX_LEVEL0_LENGTH+1];

    // Extract the level-0 directory name
    szFileName = ExtractDirectoryName0(szFileName, szDirName0);
    if(szFileName == NULL)
        return NULL;

    // Find the level-0 directory entry
    FileNameHash = CalcFileNameHash(szDirName0);
    pFileEntry = (PCASC_FILE_ENTRY)Map_FindObject(pRootHandler->pRootMap, &FileNameHash, NULL);
    if(pFileEntry == NULL)
        return NULL;

    // We allow the user to open just the level-0 directory
    if(szFileName[0] == 0)
        return pFileEntry->EncodingKey.Value;

    // Calculate the name hash of the remaining name
    FileNameHash = CalcFileNameHash(szFileName);
    pFileEntry = (PCASC_FILE_ENTRY)Map_FindObject(pRootHandler->pRootMap, &FileNameHash, NULL);
    if(pFileEntry != NULL)
        return pFileEntry->EncodingKey.Value;

    // No such file
    return NULL;
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

    // Allocate the global linear file table
    // Note: This is about 18 MB of memory for Diablo III PTR build 30013
    pRootHandler->pFileTable = CASC_ALLOC(CASC_FILE_ENTRY, hs->pEncodingMap->ItemCount);
    if(pRootHandler->pFileTable == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    pRootHandler->dwFileCountMax = (DWORD)hs->pEncodingMap->ItemCount;

    // Allocate global buffer for file names
    pRootHandler->szFileNames = CASC_ALLOC(char, 0x10000);
    if(pRootHandler->szFileNames == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    pRootHandler->cbFileNamesMax = 0x10000;

    // Create map of ROOT_ENTRY -> FileEntry
    pRootHandler->pRootMap = Map_Create(pRootHandler->dwFileCountMax, sizeof(ULONGLONG), FIELD_OFFSET(CASC_FILE_ENTRY, FileNameHash));
    if(pRootHandler->pRootMap == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Create the root directory
    nError = ParseDirectoryFile(pRootHandler, pbRootFile, pbRootFileEnd, &pRootHandler->pRootDirectory);
    if(nError == ERROR_SUCCESS)
    {
        PCASC_FILE_ENTRY pRootEntry = pRootHandler->pFileTable;
        DWORD dwRootEntries = pRootHandler->pRootDirectory->FileCount;

        // We expect the number of subdirectories to be less than maximum
        assert(dwRootEntries < DIABLO3_MAX_SUBDIRS);
        memcpy(pRootHandler->pRootDirectory->EncodingKey.Value, hs->RootKey.pbData, MD5_HASH_SIZE);

        // Now parse the all root items and load them
        for(DWORD i = 0; i < dwRootEntries; i++, pRootEntry++)
        {
            // Mark the root entry as directory
            pRootEntry->EntryFlags |= ENTRY_FLAG_DIRECTORY_ENTRY;

            // Load the entire file to memory
            pbRootFile = LoadFileToMemory(hs, pRootEntry->EncodingKey.Value, &cbRootFile);
            if(pbRootFile != NULL)
            {
                PCASC_DIRECTORY pSubDir = NULL;

                nError = ParseDirectoryFile(pRootHandler, 
                                            pbRootFile,
                                            pbRootFile + cbRootFile,
                                           &pSubDir);
                
                if(nError == ERROR_SUCCESS && pSubDir != NULL)
                {
                    pSubDir->EncodingKey = pRootEntry->EncodingKey;
                    pRootHandler->SubDirs[i] = pSubDir;
                }

                CASC_FREE(pbRootFile);
            }
        }
    }

    // Vast majorify of files at this moment don't have names.
    // We can load the Base\CoreToC.dat file in order
    // to get directory asset indexes, file names and extensions
    if(nError == ERROR_SUCCESS)
    {
        LPBYTE pbEncodingKey;
        LPBYTE pbCoreTOC;
        DWORD cbCoreTOC = 0;

        // Try to find encoding key for "Base\\CoreTOC.dat"
        pbEncodingKey = pRootHandler->GetKey(pRootHandler, "Base\\CoreTOC.dat");
        if(pbEncodingKey != NULL)
        {
            // Load the entire file to memory
            pbCoreTOC = LoadFileToMemory(hs, pbEncodingKey, &cbCoreTOC);
            if(pbCoreTOC != NULL)
            {
                // Parse the CoreTOC.dat and extract file names from it
                ParseCoreTOC(pRootHandler, pbCoreTOC, pbCoreTOC + cbCoreTOC);

                // Free the content of CoreTOC.dat
                CASC_FREE(pbCoreTOC);
            }
        }
    }
    return nError;
}
