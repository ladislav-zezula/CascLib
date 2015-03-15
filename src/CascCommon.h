/*****************************************************************************/
/* CascCommon.h                           Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Common functions for CascLib                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascCommon.h                    */
/*****************************************************************************/

#ifndef __CASCCOMMON_H__
#define __CASCCOMMON_H__

//-----------------------------------------------------------------------------
// Compression support

// Include functions from zlib
#ifndef __SYS_ZLIB
  #include "zlib/zlib.h"
#else
  #include <zlib.h>
#endif

#include "CascPort.h"
#include "common/Common.h"
#include "common/Map.h"
#include "common/FileStream.h"
#include "common/ListFile.h"
#include "common/RootFile.h"

// Headers from LibTomCrypt
#include "libtomcrypt/src/headers/tomcrypt.h"

// For HashStringJenkins
#include "jenkins/lookup.h"

//-----------------------------------------------------------------------------
// CascLib private defines

#define CASC_GAME_HOTS      0x00010000          // Heroes of the Storm
#define CASC_GAME_WOW6      0x00020000          // World of Warcraft - Warlords of Draenor
#define CASC_GAME_DIABLO3   0x00030000          // Diablo 3 Since PTR 2.2.0
#define CASC_GAME_MASK      0xFFFF0000          // Mask for getting game ID

#define CASC_INDEX_COUNT          0x10
#define CASC_FILE_KEY_SIZE        0x09          // Size of the file key
#define CASC_MAX_DATA_FILES      0x100

#define CASC_SEARCH_HAVE_NAME   0x0001          // Indicated that previous search found a name

#define BLTE_HEADER_SIGNATURE   0x45544C42      // 'BLTE' header in the data files
#define BLTE_HEADER_DELTA       0x1E            // Distance of BLTE header from begin of the header area
#define MAX_HEADER_AREA_SIZE    0x2A            // Length of the file header area

// File header area in the data.xxx:
//  BYTE  HeaderHash[MD5_HASH_SIZE];            // MD5 of the frame array
//  DWORD dwFileSize;                           // Size of the file (see comment before CascGetFileSize for details)
//  BYTE  SomeSize[4];                          // Some size (big endian)
//  BYTE  Padding[6];                           // Padding (?)
//  DWORD dwSignature;                          // Must be "BLTE"
//  BYTE  HeaderSizeAsBytes[4];                 // Header size in bytes (big endian)
//  BYTE  MustBe0F;                             // Must be 0x0F. Optional, only if HeaderSizeAsBytes != 0
//  BYTE  FrameCount[3];                        // Frame count (big endian). Optional, only if HeaderSizeAsBytes != 0

// Prevent problems with CRT "min" and "max" functions,
// as they are not defined on all platforms
#define CASCLIB_MIN(a, b) ((a < b) ? a : b)
#define CASCLIB_MAX(a, b) ((a > b) ? a : b)
#define CASCLIB_UNUSED(p) ((void)(p))

#define CASC_PACKAGE_BUFFER     0x1000

//-----------------------------------------------------------------------------
// In-memory structures

struct TFileStream;

typedef struct _CASC_INDEX_ENTRY
{
    BYTE IndexKey[CASC_FILE_KEY_SIZE];              // The first 9 bytes of the encoding key
    BYTE FileOffsetBE[5];                           // Index of data file and offset within (big endian).
    BYTE FileSizeLE[4];                             // Size occupied in the storage file (data.###). See comment before CascGetFileSize for details
} CASC_INDEX_ENTRY, *PCASC_INDEX_ENTRY;

typedef struct _CASC_MAPPING_TABLE
{
    TCHAR * szFileName;                             // Name of the key mapping file
    LPBYTE  pbFileData;                             // Pointer to the file data
    DWORD   cbFileData;                             // Length of the file data
    BYTE   ExtraBytes;                              // (?) Extra bytes in the key record
    BYTE   SpanSizeBytes;                           // Size of field with file size
    BYTE   SpanOffsBytes;                           // Size of field with file offset
    BYTE   KeyBytes;                                // Size of the file key
    BYTE   SegmentBits;                             // Number of bits for the file offset (rest is archive index)
    ULONGLONG MaxFileOffset;
        
    PCASC_INDEX_ENTRY pIndexEntries;                // Sorted array of index entries
    DWORD nIndexEntries;                            // Number of index entries

} CASC_MAPPING_TABLE, *PCASC_MAPPING_TABLE;

typedef struct _CASC_FILE_FRAME
{
    DWORD FrameArchiveOffset;                       // Archive file pointer corresponding to the begin of the frame
    DWORD FrameFileOffset;                          // File pointer corresponding to the begin of the frame
    DWORD CompressedSize;                           // Compressed size of the file
    DWORD FrameSize;                                // Size of the frame
    BYTE  md5[MD5_HASH_SIZE];                       // MD5 hash of the file sector
} CASC_FILE_FRAME, *PCASC_FILE_FRAME;

typedef struct _CASC_ENCODING_HEADER
{
    BYTE Magic[2];                                  // "EN"
    BYTE field_2;
    BYTE field_3;
    BYTE field_4;
    BYTE field_5[2];
    BYTE field_7[2];
    BYTE NumSegments[4];                            // Number of entries (big endian)
    BYTE field_D[4];
    BYTE field_11;
    BYTE SegmentsPos[4];                            // Offset of encoding segments

} CASC_ENCODING_HEADER, *PCASC_ENCODING_HEADER;

typedef struct _CASC_ENCODING_ENTRY
{
    USHORT KeyCount;                                // Number of subitems
    BYTE FileSizeBE[4];                             // Compressed file size (header area + frame headers + compressed frames), in bytes
    BYTE EncodingKey[MD5_HASH_SIZE];                // File encoding key

    // Followed by the index keys
    // (number of items = KeyCount)
    // Followed by the index keys (number of items = KeyCount)
} CASC_ENCODING_ENTRY, *PCASC_ENCODING_ENTRY;

#define GET_INDEX_KEY(pEncodingEntry)  (pEncodingEntry->EncodingKey + MD5_HASH_SIZE)

//-----------------------------------------------------------------------------
// Structures for CASC storage and CASC file

typedef struct _TCascStorage
{
    const char * szClassName;                       // "TCascStorage"
    const TCHAR * szIndexFormat;                    // Format of the index file name
    TCHAR * szRootPath;                             // This is the game directory
    TCHAR * szDataPath;                             // This is the directory where data files are
    TCHAR * szIndexPath;                            // This is the directory where index files are
    TCHAR * szUrlPath;                              // URL to the Blizzard servers
    DWORD dwRefCount;                               // Number of references
    DWORD dwGameInfo;                               // Game type
    DWORD dwBuildNumber;                            // Game build number
    DWORD dwFileBeginDelta;                         // This is number of bytes to shift back from archive offset (from index entry) to actual begin of file data
    DWORD dwDefaultLocale;                          // Default locale, read from ".build.info"
    
    QUERY_KEY CdnConfigKey;
    QUERY_KEY CdnBuildKey;

    PQUERY_KEY pArchiveArray;                       // Array of the archives
    QUERY_KEY ArchiveGroup;                         // Name of the group archive file
    DWORD ArchiveCount;                             // Number of archives in the array

    PQUERY_KEY pPatchArchiveArray;                  // Array of the patch archives
    QUERY_KEY PatchArchiveGroup;                    // Name of the patch group archive file
    DWORD PatchArchiveCount;                        // Number of patch archives in the array

    QUERY_KEY RootKey;
    QUERY_KEY PatchKey;
    QUERY_KEY DownloadKey;
    QUERY_KEY InstallKey;

    PQUERY_KEY pEncodingKeys;
    QUERY_KEY EncodingKey;
    QUERY_KEY EncodingEKey;
    DWORD EncodingKeys;

    TFileStream * DataFileArray[CASC_MAX_DATA_FILES]; // Data file handles

    CASC_MAPPING_TABLE KeyMapping[CASC_INDEX_COUNT]; // Key mapping
    PCASC_MAP pIndexEntryMap;                       // Map of index entries

    PCASC_ENCODING_ENTRY * ppEncodingEntries;       // Map of encoding entries
    LPBYTE pbEncodingFile;                          // The encoding file
    size_t nEncodingEntries;                        // Number of encoding entries

    TRootFile * pRootFile;                          // Common handler for various ROOT file formats

} TCascStorage;

typedef struct _TCascFile
{
    TCascStorage * hs;                              // Pointer to storage structure
    TFileStream * pStream;                          // An open data stream
    const char * szClassName;                       // "TCascFile"
    
    DWORD FilePointer;                              // Current file pointer

    DWORD ArchiveIndex;                             // Index of the archive (data.###)
    DWORD HeaderOffset;                             // Offset of the BLTE header, relative to the begin of the archive
    DWORD HeaderSize;                               // Length of the BLTE header
    DWORD FramesOffset;                             // Offset of the frame data, relative to the begin of the archive
    DWORD CompressedSize;                           // Compressed size of the file (in bytes)
    DWORD FileSize;                                 // Size of file, in bytes
    BYTE FrameArrayHash[MD5_HASH_SIZE];             // MD5 hash of the frame array

    PCASC_FILE_FRAME pFrames;                       // Array of file frames
    DWORD FrameCount;                               // Number of the file frames

    LPBYTE pbFileCache;                             // Pointer to file cache
    DWORD cbFileCache;                              // Size of the file cache
    DWORD CacheStart;                               // Starting offset in the cache
    DWORD CacheEnd;                                 // Ending offset in the cache

#ifdef CASCLIB_TEST     // Extra fields for analyzing the file size problem
    DWORD FileSize_RootEntry;                       // File size, from the root entry
    DWORD FileSize_EncEntry;                        // File size, from the encoding entry
    DWORD FileSize_IdxEntry;                        // File size, from the index entry
    DWORD FileSize_HdrArea;                         // File size, as stated in the file header area
    DWORD FileSize_FrameSum;                        // File size as sum of frame sizes
#endif

} TCascFile;

typedef struct _TCascSearch
{
    TCascStorage * hs;                              // Pointer to the storage handle
    const char * szClassName;                       // Contains "TCascSearch"
    TCHAR * szListFile;                             // Name of the listfile
    void * pCache;                                  // Listfile cache
    char * szMask;                                  // Search mask
    char szFileName[MAX_PATH];                      // Buffer for the file name
    char szNormName[MAX_PATH];                      // Buffer for normalized file name

    // Provider-specific data
    void * pRootContext;                            // Root-specific search context
    size_t RootSearchPhase;                         // Root-specific phase value for search phase
    size_t IndexLevel1;                             // Root-specific search context
    size_t IndexLevel2;                             // Root-specific search context
    DWORD dwState;                                  // Pointer to the search state (0 = listfile, 1 = nameless, 2 = done)

    // TODO: DELETE THESE
//  void * pStruct1C;                               // Search structure for MNDX info
    DWORD RootIndex;                                // Root index of the previously found item
    
    BYTE BitArray[1];                               // Bit array of encoding keys. Set for each entry that has already been reported

} TCascSearch;

//-----------------------------------------------------------------------------
// Memory management
//
// We use our own macros for allocating/freeing memory. If you want
// to redefine them, please keep the following rules:
//
//  - The memory allocation must return NULL if not enough memory
//    (i.e not to throw exception)
//  - The allocating function does not need to fill the allocated buffer with zeros
//  - The reallocating function must support NULL as the previous block
//  - Memory freeing function doesn't have to test the pointer to NULL
//

#if defined(_MSC_VER) && defined(_DEBUG)

#define CASC_REALLOC(type, ptr, count) (type *)HeapReAlloc(GetProcessHeap(), 0, ptr, ((count) * sizeof(type)))
#define CASC_ALLOC(type, count)        (type *)HeapAlloc(GetProcessHeap(), 0, ((count) * sizeof(type)))
#define CASC_FREE(ptr)                 HeapFree(GetProcessHeap(), 0, ptr)

#else

#define CASC_REALLOC(type, ptr, count) (type *)realloc(ptr, (count) * sizeof(type))
#define CASC_ALLOC(type, count)        (type *)malloc((count) * sizeof(type))
#define CASC_FREE(ptr)                 free(ptr)

#endif

//-----------------------------------------------------------------------------
// Big endian number manipulation

DWORD ConvertBytesToInteger_3(LPBYTE ValueAsBytes);
DWORD ConvertBytesToInteger_4(LPBYTE ValueAsBytes);
DWORD ConvertBytesToInteger_4_LE(LPBYTE ValueAsBytes);
ULONGLONG ConvertBytesToInteger_5(LPBYTE ValueAsBytes);

//-----------------------------------------------------------------------------
// Build configuration reading

int LoadBuildInfo(TCascStorage * hs);

//-----------------------------------------------------------------------------
// Internal file functions

TCascStorage * IsValidStorageHandle(HANDLE hStorage);
TCascFile * IsValidFileHandle(HANDLE hFile);

PCASC_ENCODING_ENTRY FindEncodingEntry(TCascStorage * hs, PQUERY_KEY pEncodingKey, size_t * PtrIndex);
PCASC_INDEX_ENTRY    FindIndexEntry(TCascStorage * hs, PQUERY_KEY pIndexKey);

int CascDecompress(void * pvOutBuffer, PDWORD pcbOutBuffer, void * pvInBuffer, DWORD cbInBuffer);

//-----------------------------------------------------------------------------
// Support for ROOT file

int RootFile_CreateDiablo3(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootFile_CreateMNDX(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootFile_CreateWoW6(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask);

//-----------------------------------------------------------------------------
// Dumping CASC data structures

#ifdef _DEBUG
void CascDumpSparseArray(const char * szFileName, void * pvSparseArray);
void CascDumpNameFragTable(const char * szFileName, void * pvMarFile);
void CascDumpFileNames(const char * szFileName, void * pvMarFile);
void CascDumpIndexEntries(const char * szFileName, TCascStorage * hs);
//void CascDumpMndxRoot(const char * szFileName, PCASC_MNDX_INFO pMndxInfo);
void CascDumpRootFile(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, const char * szFormat, const TCHAR * szListFile, int nDumpLevel);
void CascDumpFile(const char * szFileName, HANDLE hFile);
#endif  // _DEBUG

#endif // __CASCCOMMON_H__
