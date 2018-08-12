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
#include "common/Array.h"
#include "common/Map.h"
#include "common/FileTree.h"
#include "common/FileStream.h"
#include "common/Directory.h"
#include "common/ListFile.h"
#include "common/Csv.h"
#include "common/RootHandler.h"

// Headers from LibTomCrypt
#include "libtomcrypt/src/headers/tomcrypt.h"

// For HashStringJenkins
#include "jenkins/lookup.h"

//-----------------------------------------------------------------------------
// CascLib private defines

#define CASC_GAME_HOTS       0x00010000         // Heroes of the Storm
#define CASC_GAME_WOW6       0x00020000         // World of Warcraft - Warlords of Draenor
#define CASC_GAME_DIABLO3    0x00030000         // Diablo 3 since PTR 2.2.0
#define CASC_GAME_OVERWATCH  0x00040000         // Overwatch since PTR 24919
#define CASC_GAME_STARCRAFT2 0x00050000         // Starcraft II - Legacy of the Void, since build 38996
#define CASC_GAME_STARCRAFT1 0x00060000         // Starcraft 1 (remastered)
#define CASC_GAME_WARCRAFT3  0x00070000         // Warcraft III, since version 1.30, build 9655
#define CASC_GAME_MASK       0xFFFF0000         // Mask for getting game ID

#define CASC_INDEX_COUNT          0x10
#define CASC_CKEY_SIZE            0x10          // Size of the content key
#define CASC_EKEY_SIZE            0x09          // Size of the encoded key
#define CASC_MAX_DATA_FILES      0x100
#define CASC_EXTRA_FILES          0x20          // Number of extra entries to be reserved for additionally inserted files

// Prevent problems with CRT "min" and "max" functions,
// as they are not defined on all platforms
#define CASCLIB_MIN(a, b) ((a < b) ? a : b)
#define CASCLIB_MAX(a, b) ((a > b) ? a : b)
#define CASCLIB_UNUSED(p) ((void)(p))
#define CASC_PACKAGE_BUFFER     0x1000

#ifndef _maxchars
#define _maxchars(buff)  ((sizeof(buff) / sizeof(buff[0])) - 1)
#endif

//-----------------------------------------------------------------------------
// In-memory structures
// See http://pxr.dk/wowdev/wiki/index.php?title=CASC for more information

struct TFileStream;

typedef enum _CBLD_TYPE
{
    CascBuildNone = 0,                              // No build type found
    CascBuildInfo,                                  // .build.info
    CascBuildDb,                                    // .build.db (older storages)
} CBLD_TYPE, *PCBLD_TYPE;

// CKey file entry
typedef struct _CASC_CKEY_ENTRY
{
    USHORT EKeyCount;                               // Number of EKeys
    BYTE ContentSize[4];                            // Content file size (big endian)
    BYTE CKey[CASC_CKEY_SIZE];                      // Content key. This is MD5 of the file content
    BYTE EKey[CASC_CKEY_SIZE];                      // Encoded key. This is (trimmed) MD5 hash of the file header, containing MD5 hashes of all the logical blocks of the file

} CASC_CKEY_ENTRY, *PCASC_CKEY_ENTRY;

// EKey file entry
typedef struct _CASC_EKEY_ENTRY
{
    BYTE EKey[CASC_EKEY_SIZE];                      // The first 9 bytes of the encoded key. The encoded key is MD5 hash of the file header, which contains MD5 hashes of all the logical blocks of the file
    BYTE StorageOffset[5];                          // Offset of the file over the entire storage
    BYTE EncodedSize[4];                            // Encoded size (little endian). This is the size of encoded header, all file frame headers and all file frames
} CASC_EKEY_ENTRY, *PCASC_EKEY_ENTRY;

// Structure describing the index file
typedef struct _CASC_INDEX_FILE
{
    TCHAR * szFileName;                             // Name of the index file
    LPBYTE  pbFileData;                             // Pointer to the file data
    DWORD   cbFileData;                             // Length of the file data
    BYTE   ExtraBytes;                              // (?) Extra bytes in the key record
    BYTE   SpanSizeBytes;                           // Byte size of the "file size" field. Expected to be 5
    BYTE   SpanOffsBytes;                           // Byte size of the "archive+offset" field. Expected to be 5
    BYTE   KeyBytes;                                // Size of the file key. Expected to be 9
    BYTE   FileOffsetBits;                          // Number of bits for the file offset (rest is archive index). Usually 0x1E
    bool   FreeEKeyEntries;                         // If true, then we need to free the EKey map
    ULONGLONG SegmentSize;                          // Size of one segment (aka data.### file)

    PCASC_EKEY_ENTRY pEKeyEntries;                  // Sorted array of EKey entries (into pbFileData)
    DWORD nEKeyEntries;                             // Number of EKey entries

} CASC_INDEX_FILE, *PCASC_INDEX_FILE;

// Converted header of the ENCODING file
typedef struct _CASC_ENCODING_HEADER
{
    USHORT Version;                                 // Expected to be 1 by CascLib
    BYTE   CKeyLength;                              // The content key length in ENCODING file. Usually 0x10
    BYTE   EKeyLength;                              // The encoded key length in ENCODING file. Usually 0x10
    DWORD  CKeyPageSize;                            // Size of the CKey page in bytes
    DWORD  EKeyPageSize;                            // Size of the CKey page in bytes
    DWORD  CKeyPageCount;                           // Number of CKey pages in the page table
    DWORD  EKeyPageCount;                           // Number of EKey pages in the page table
    DWORD  ESpecBlockSize;                          // Size of the ESpec string block, in bytes
} CASC_ENCODING_HEADER, *PCASC_ENCODING_HEADER;

typedef struct _CASC_FILE_FRAME
{
    CONTENT_KEY FrameHash;                          // MD5 hash of the file frame
    DWORD DataFileOffset;                           // Offset in the data file (data.###)
    DWORD FileOffset;                               // File offset of this frame
    DWORD EncodedSize;                              // Encoded size of the frame
    DWORD ContentSize;                              // Content size of the frame
} CASC_FILE_FRAME, *PCASC_FILE_FRAME;

//-----------------------------------------------------------------------------
// Structures for CASC storage and CASC file

typedef struct _TCascStorage
{
    const char * szClassName;                       // "TCascStorage"
    const TCHAR * szIndexFormat;                    // Format of the index file name
    TCHAR * szBuildFile;                            // Build file name (.build.info or .build.db)
    TCHAR * szDataPath;                             // This is the directory where data files are
    TCHAR * szIndexPath;                            // This is the directory where index files are
    TCHAR * szCdnList;                              // Multi-SZ list of CDN servers, including subfolders
    DWORD dwRefCount;                               // Number of references
    DWORD dwGameInfo;                               // Game type
    DWORD dwBuildNumber;                            // Game build number
    DWORD dwHeaderSpanSize;                         // Size of the header span. Usually 0x1E. Zero on older storages
    DWORD dwDefaultLocale;                          // Default locale, read from ".build.info"

    CBLD_TYPE BuildFileType;                        // Type of the build file

    QUERY_KEY CdnConfigKey;                         // Currently selected CDN config file. Points to "config\%02X\%02X\%s
    QUERY_KEY CdnBuildKey;                          // Currently selected CDN build file. Points to "config\%02X\%02X\%s

    QUERY_KEY ArchiveGroup;                         // Key array of the "archive-group"
    QUERY_KEY ArchivesKey;                          // Key array of the "archives"
    QUERY_KEY PatchArchivesKey;                     // Key array of the "patch-archives"
    QUERY_KEY PatchArchivesGroup;                   // Key array of the "patch-archive-group"
    QUERY_KEY BuildFiles;                           // List of supported build files

    CASC_CKEY_ENTRY EncodingFile;                   // Information about ENCODING file
    CASC_CKEY_ENTRY RootFile;                       // Information about ROOT file
    CASC_CKEY_ENTRY InstallFile;                    // Information about INSTALL file
    CASC_CKEY_ENTRY DownloadFile;                   // Information about DOWNLOAD file
    CASC_CKEY_ENTRY PatchFile;                      // Information about PATCH file

    CASC_CKEY_ENTRY VfsRoot;                        // The main VFS root file
    CASC_ARRAY VfsRootList;                         // List of VFS root files. Used on TVFS root keys

    TFileStream * DataFiles[CASC_MAX_DATA_FILES];   // Array of open data files

    CASC_INDEX_FILE IndexFile[CASC_INDEX_COUNT];    // Array of index files
    PCASC_MAP pEKeyEntryMap;                        // Map of EKey entries

    PCASC_MAP  pCKeyEntryMap;                       // Map of CKey -> CASC_CKEY_ENTRY
    QUERY_KEY  EncodingData;                        // Content of the ENCODING file. Keep this in for encoding table.

    TRootHandler * pRootHandler;                    // Common handler for various ROOT file formats

} TCascStorage;

typedef struct _TCascFile
{
    TCascStorage * hs;                              // Pointer to storage structure
    TFileStream * pStream;                          // An open data stream
    const char * szClassName;                       // "TCascFile"

    PCASC_FILE_FRAME pFrames;                       // Array of file frames
    CONTENT_KEY CKey;                               // Content key of the file. Effectively a MD5 hash of the file content
    ENCODED_KEY EKey;                               // Encoded key of the file. MD5 hash of the encoded header + frame headers
    DWORD ArchiveIndex;                             // Index of the archive (data.###)
    DWORD ArchiveOffset;                            // Offset in the archive (data.###)
    DWORD FilePointer;                              // Current file pointer
    DWORD EncodedSize;                              // Encoded size. This is the size of encoded header, all file frame headers and all file frames
    DWORD ContentSize;                              // Content size. This is the size of the file content, aka the file size
    DWORD FrameCount;                               // Number of the file frames
    DWORD OpenFlags;                                // Opening flags

    LPBYTE pbFileCache;                             // Pointer to file cache
    DWORD cbFileCache;                              // Size of the file cache

} TCascFile;

typedef struct _TCascSearch
{
    TCascStorage * hs;                              // Pointer to the storage handle
    const char * szClassName;                       // Contains "TCascSearch"
    TCHAR * szListFile;                             // Name of the listfile
    void * pCache;                                  // Listfile cache
    char * szMask;                                  // Search mask
    char szFileName[MAX_PATH];                      // Buffer for the file name
    DWORD dwFileDataId;                             // For File Data ID
    DWORD dwFileSize;                               // For file size
    DWORD dwLocaleFlags;                            // For locale flags

    // Provider-specific data
    void * pRootContext;                            // Root-specific search context
    size_t IndexLevel1;                             // Root-specific search context
    size_t IndexLevel2;                             // Root-specific search context
    DWORD dwState;                                  // Pointer to the search state (0 = listfile, 1 = nameless, 2 = done)

    DWORD BitArray[1];                              // Bit array of EKeys. Set for each entry that has already been reported

} TCascSearch;

//-----------------------------------------------------------------------------
// Common functions (CascCommon.cpp)

LPBYTE LoadInternalFileToMemory(TCascStorage * hs, LPBYTE pbQueryKey, DWORD dwOpenFlags, DWORD * pcbFileData);
void FreeCascBlob(PQUERY_KEY pQueryKey);

//-----------------------------------------------------------------------------
// Text file parsing (CascFiles.cpp)

int CheckGameDirectory(TCascStorage * hs, TCHAR * szDirectory);
int LoadBuildInfo(TCascStorage * hs);
int LoadCdnConfigFile(TCascStorage * hs);
int LoadCdnBuildFile(TCascStorage * hs);

//-----------------------------------------------------------------------------
// Internal file functions

TCascStorage * IsValidCascStorageHandle(HANDLE hStorage);
TCascFile * IsValidCascFileHandle(HANDLE hFile);

PCASC_CKEY_ENTRY FindCKeyEntry(TCascStorage * hs, PQUERY_KEY pCKey, PDWORD PtrIndex = NULL);
PCASC_EKEY_ENTRY FindEKeyEntry(TCascStorage * hs, PQUERY_KEY pEKey, PDWORD PtrIndex = NULL);

int CascDecompress(LPBYTE pvOutBuffer, PDWORD pcbOutBuffer, LPBYTE pvInBuffer, DWORD cbInBuffer);
int CascDecrypt   (LPBYTE pbOutBuffer, PDWORD pcbOutBuffer, LPBYTE pbInBuffer, DWORD cbInBuffer, DWORD dwFrameIndex);
int CascDirectCopy(LPBYTE pbOutBuffer, PDWORD pcbOutBuffer, LPBYTE pbInBuffer, DWORD cbInBuffer);

//-----------------------------------------------------------------------------
// Support for ROOT file

void InitRootHandler_FileTree(TRootHandler * pRootHandler, size_t nStructSize);

int RootHandler_CreateMNDX(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootHandler_CreateTVFS(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootHandler_CreateDiablo3(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootHandler_CreateWoW6(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask);
int RootHandler_CreateOverwatch(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootHandler_CreateStarcraft1(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);

//-----------------------------------------------------------------------------
// Dumping CASC data structures

#ifdef _DEBUG
void CascDumpFile(HANDLE hFile, const char * szDumpFile = NULL);
void CascDumpStorage(HANDLE hStorage, const char * szDumpFile = NULL);
#endif  // _DEBUG

#endif // __CASCCOMMON_H__
