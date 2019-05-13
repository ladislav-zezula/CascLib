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
#include "common/IndexMap.h"
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

// On-disk CASC structures
#include "CascStructs.h"

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

#define CASC_EXTRA_FILES          0x20          // Number of extra entries to be reserved for additionally inserted files

#define CASC_PACKAGE_BUFFER     0x1000

#ifndef _maxchars
#define _maxchars(buff)  ((sizeof(buff) / sizeof(buff[0])) - 1)
#endif

//-----------------------------------------------------------------------------
// In-memory structures

typedef enum _CBLD_TYPE
{
    CascBuildNone = 0,                              // No build type found
    CascBuildInfo,                                  // .build.info
    CascBuildDb,                                    // .build.db (older storages)
} CBLD_TYPE, *PCBLD_TYPE;

// Tag file entry, loaded from the DOWNLOAD file
typedef struct _CASC_TAG_ENTRY
{
    USHORT HashType;                                // Hash type
    char TagName[1];                                // Tag name. Variable length.
} CASC_TAG_ENTRY, *PCASC_TAG_ENTRY;

// Normalized header of the index files.
// Both version 1 and version 2 are converted to this structure
typedef struct _CASC_INDEX_HEADER
{
    USHORT IndexVersion;                            // 5 for index v 1.0, 7 for index version 2.0
    BYTE   BucketIndex;                             // Should be the same as the first byte of the hex filename. 
    BYTE   StorageOffsetLength;                     // Length, in bytes, of the StorageOffset field in the EKey entry
    BYTE   EncodedSizeLength;                       // Length, in bytes, of the EncodedSize in the EKey entry
    BYTE   EKeyLength;                              // Length, in bytes, of the (trimmed) EKey in the EKey entry
    BYTE   FileOffsetBits;                          // Number of bits of the archive file offset in StorageOffset field. Rest is data segment index
    BYTE   Alignment;
    ULONGLONG SegmentSize;                          // Size of one data segment (aka data.### file)
    size_t HeaderLength;                            // Length of the on-disk header structure, in bytes
    size_t HeaderPadding;                           // Length of padding after the header
    size_t EntryLength;                             // Length of the on-disk EKey entry structure, in bytes
    size_t EKeyCount;                               // Number of EKey entries. Only supplied for index files version 1.

} CASC_INDEX_HEADER, *PCASC_INDEX_HEADER;

// Normalized header of the ENCODING file
typedef struct _CASC_ENCODING_HEADER
{
    USHORT Magic;                                   // FILE_MAGIC_ENCODING ('EN')
    USHORT Version;                                 // Expected to be 1 by CascLib
    BYTE   CKeyLength;                              // The content key length in ENCODING file. Usually 0x10
    BYTE   EKeyLength;                              // The encoded key length in ENCODING file. Usually 0x10
    DWORD  CKeyPageSize;                            // Size of the CKey page in bytes
    DWORD  EKeyPageSize;                            // Size of the CKey page in bytes
    DWORD  CKeyPageCount;                           // Number of CKey pages in the page table
    DWORD  EKeyPageCount;                           // Number of EKey pages in the page table
    DWORD  ESpecBlockSize;                          // Size of the ESpec string block, in bytes
} CASC_ENCODING_HEADER, *PCASC_ENCODING_HEADER;

typedef struct _CASC_DOWNLOAD_HEADER
{
    USHORT Magic;                                   // FILE_MAGIC_DOWNLOAD ('DL')
    USHORT Version;                                 // Version
    USHORT EKeyLength;                              // Length of the EKey
    USHORT EntryHasChecksum;                        // If nonzero, then the entry has checksum
    DWORD  EntryCount;
    DWORD  TagCount;
    USHORT FlagByteSize;
    USHORT BasePriority;

    size_t HeaderLength;                            // Length of the on-disk header, in bytes
    size_t EntryLength;                             // Length of the on-disk entry, in bytes

} CASC_DOWNLOAD_HEADER, *PCASC_DOWNLOAD_HEADER;

typedef struct _CASC_DOWNLOAD_ENTRY
{
    BYTE EKey[MD5_HASH_SIZE];
    ULONGLONG EncodedSize;
    DWORD Checksum;
    DWORD Flags;
    BYTE Priority;
} CASC_DOWNLOAD_ENTRY, *PCASC_DOWNLOAD_ENTRY;

// Capturable tag structure for loading from DOWNLOAD manifest
typedef struct _CASC_TAG_ENTRY1
{
    const char * szTagName;                         // Tag name
    LPBYTE Bitmap;                                  // Bitmap
    size_t BitmapLength;                            // Length of the bitmap, in bytes
    size_t NameLength;                              // Length of the tag name, in bytes, not including '\0'
    size_t TagLength;                               // Length of the on-disk tag, in bytes
    DWORD TagValue;                                 // Tag value
} CASC_TAG_ENTRY1, *PCASC_TAG_ENTRY1;

// Tag structure for storing in arrays
typedef struct _CASC_TAG_ENTRY2
{
    size_t NameLength;                              // Length of the on-disk tag, in bytes
    DWORD TagValue;                                 // Tag value
    char szTagName[0x08];                           // Tag string. This member can be longer than declared. Aligned to 8 bytes.
} CASC_TAG_ENTRY2, *PCASC_TAG_ENTRY2;

typedef struct _CASC_INSTALL_HEADER
{
    USHORT Magic;                                   // FILE_MAGIC_DOWNLOAD ('DL')
    BYTE   Version;                                 // Version
    BYTE   EKeyLength;                              // Length of the EKey
    DWORD  EntryCount;
    DWORD  TagCount;

    size_t HeaderLength;                            // Length of the on-disk header, in bytes
} CASC_INSTALL_HEADER, *PCASC_INSTALL_HEADER;

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
    const char * szProductName;                     // String representation of the product name
    TCHAR * szBuildFile;                            // Build file name (.build.info or .build.db)
    TCHAR * szDataPath;                             // This is the directory where data files are
    TCHAR * szIndexPath;                            // This is the directory where index files are
    TCHAR * szCdnList;                              // Multi-SZ list of CDN servers, including subfolders
    CASC_PRODUCT Product;                           // Product enum value (see CASC_PRODUCT)
    DWORD dwBuildNumber;                            // Product build number
    DWORD dwRefCount;                               // Number of references
    DWORD dwHeaderSpanSize;                         // Size of the header span. Usually 0x1E. Zero on older storages
    DWORD dwDefaultLocale;                          // Default locale, read from ".build.info"
    DWORD dwFeatures;                               // List of CASC features. See CASC_FEATURE_XXX

    CBLD_TYPE BuildFileType;                        // Type of the build file

    QUERY_KEY CdnConfigKey;                         // Currently selected CDN config file. Points to "config\%02X\%02X\%s
    QUERY_KEY CdnBuildKey;                          // Currently selected CDN build file. Points to "config\%02X\%02X\%s

    QUERY_KEY ArchiveGroup;                         // Key array of the "archive-group"
    QUERY_KEY ArchivesKey;                          // Key array of the "archives"
    QUERY_KEY PatchArchivesKey;                     // Key array of the "patch-archives"
    QUERY_KEY PatchArchivesGroup;                   // Key array of the "patch-archive-group"
    QUERY_KEY BuildFiles;                           // List of supported build files

    TFileStream * DataFiles[CASC_MAX_DATA_FILES];   // Array of open data files

    CASC_CKEY_ENTRY EncodingCKey;                   // Information about ENCODING file
    CASC_CKEY_ENTRY DownloadCKey;                   // Information about DOWNLOAD file
    CASC_CKEY_ENTRY InstallFile;                    // Information about INSTALL file
    CASC_CKEY_ENTRY PatchFile;                      // Information about PATCH file
    CASC_CKEY_ENTRY RootFile;                       // Information about ROOT file
    CASC_CKEY_ENTRY SizeFile;                       // Information about SIZE file
    CASC_CKEY_ENTRY VfsRoot;                        // The main VFS root file
    CASC_ARRAY VfsRootList;                         // List of CASC_EKEY_ENTRY for each TVFS sub-root

    TRootHandler * pRootHandler;                    // Common handler for various ROOT file formats
    CASC_ARRAY IndexArray;                          // This is the array that contains the list CASC_CKEY_ENTRY loaded from index files
    CASC_ARRAY CKeyArray;                           // This is the array that contains the list CASC_CKEY_ENTRY, one entry for each physical file
    CASC_ARRAY TagsArray;                           // Array of tags (CASC_DOWNLOAD_TAG2)
    CASC_MAP CKeyMap;                               // Map of CKey -> CASC_CKEY_ENTRY
    CASC_MAP EKeyMap;                               // Map of EKey -> CASC_EKEY_ENTRY
    size_t LocalFiles;                              // Number of files that are present locally
    size_t TotalFiles;                              // Total number of files in the storage, some may not be present locally
    size_t EKeyEntries;                             // Number of CKeyEntry-ies loaded from text build file
    size_t OrphanItems;                             // Number of EKey entries in indexes that do not have their counterpart in ENCODING
    size_t SkippedItems;                            // Number of EKey entries in indexes that were ignored due to insufficient capacity of CKeyArray
    size_t EKeyLength;                              // EKey length from the index files
    DWORD FileOffsetBits;                           // Nimber of bits in the storage offset which mean data segent offset

    CASC_ARRAY ExtraKeysList;                       // List additional encryption keys
    CASC_MAP   EncryptionKeys;                      // Map of encryption keys

} TCascStorage;

typedef struct _TCascFile
{
    TCascStorage * hs;                              // Pointer to storage structure
    TFileStream * pStream;                          // An open data stream
    const char * szClassName;                       // "TCascFile"

    PCASC_CKEY_ENTRY pCKeyEntry;
    PCASC_FILE_FRAME pFrames;                       // Array of file frames
    DWORD ArchiveIndex;                             // Index of the archive (data.###)
    DWORD ArchiveOffset;                            // Offset in the archive (data.###)
    DWORD FilePointer;                              // Current file pointer
    DWORD EncodedSize;                              // Encoded size. This is the size of encoded header, all file frame headers and all file frames
    DWORD ContentSize;                              // Content size. This is the size of the file content, aka the file size
    DWORD FrameCount;                               // Number of the file frames
    DWORD bVerifyIntegrity:1;                       // If true, then the data are validated more strictly when read

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

    // Provider-specific data
    void * pRootContext;                            // Root-specific search context
    size_t nFileIndex;                              // Root-specific search context
    DWORD dwState;                                  // Pointer to the search state (0 = listfile, 1 = nameless, 2 = done)
    DWORD bListFileUsed:1;                          // TRUE: The listfile has already been loaded

    DWORD BitArray[1];                              // Bit array of EKeys. Set for each entry that has already been reported

} TCascSearch;

//-----------------------------------------------------------------------------
// Common functions (CascCommon.cpp)

bool OpenFileByCKeyEntry(TCascStorage * hs, PCASC_CKEY_ENTRY pCKeyEntry, DWORD dwOpenFlags, HANDLE * PtrFileHandle);
bool OpenFileInternal(TCascStorage * hs, LPBYTE pbQueryKey, DWORD dwOpenFlags, HANDLE * PtrFileHandle);

LPBYTE LoadInternalFileToMemory(TCascStorage * hs, PCASC_CKEY_ENTRY pCKeyEntry, DWORD * pcbFileData);
LPBYTE LoadExternalFileToMemory(const TCHAR * szFileName, DWORD * pcbFileData);
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

void * ProbeOutputBuffer(void * pvBuffer, size_t cbLength, size_t cbMinLength, size_t * pcbLengthNeeded);

PCASC_CKEY_ENTRY FindCKeyEntry_CKey(TCascStorage * hs, LPBYTE pbCKey, PDWORD PtrIndex = NULL);
PCASC_CKEY_ENTRY FindCKeyEntry_EKey(TCascStorage * hs, LPBYTE pbEKey, PDWORD PtrIndex = NULL);

int CascDecompress(LPBYTE pvOutBuffer, PDWORD pcbOutBuffer, LPBYTE pvInBuffer, DWORD cbInBuffer);
int CascDirectCopy(LPBYTE pbOutBuffer, PDWORD pcbOutBuffer, LPBYTE pbInBuffer, DWORD cbInBuffer);

int CascLoadEncryptionKeys(TCascStorage * hs);
int CascDecrypt(TCascStorage * hs, LPBYTE pbOutBuffer, PDWORD pcbOutBuffer, LPBYTE pbInBuffer, DWORD cbInBuffer, DWORD dwFrameIndex);

//-----------------------------------------------------------------------------
// Support for ROOT file

void InitRootHandler_FileTree(TRootHandler * pRootHandler, size_t nStructSize);

int RootHandler_CreateMNDX(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootHandler_CreateTVFS(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootHandler_CreateDiablo3(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootHandler_CreateWoW(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask);
int RootHandler_CreateOverwatch(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int RootHandler_CreateStarcraft1(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);

//-----------------------------------------------------------------------------
// Dumpers (CascDumpData.cpp)

#ifdef _DEBUG
void CascDumpFile(HANDLE hFile, const char * szDumpFile = NULL);
void CascDumpStorage(HANDLE hStorage, const char * szDumpFile = NULL);
#endif

#endif // __CASCCOMMON_H__
