/*****************************************************************************/
/* CascLib.h                              Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* CascLib library v 1.00                                                    */
/*                                                                           */
/* Author : Ladislav Zezula                                                  */
/* E-mail : ladik@zezula.net                                                 */
/* WWW    : http://www.zezula.net                                            */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __CASCLIB_H__
#define __CASCLIB_H__

#ifdef _MSC_VER
#pragma warning(disable:4668)       // 'XXX' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif'
#pragma warning(disable:4820)       // 'XXX' : '2' bytes padding added after data member 'XXX::yyy'
#endif

#include "CascPort.h"

#ifdef __cplusplus
extern "C" {
#endif

//-----------------------------------------------------------------------------
// Use the apropriate library
//
// The library type is encoded in the library name as the following
// CascLibXYZ.lib
//
//  X - D for Debug version, R for Release version
//  Y - A for ANSI version, U for Unicode version
//  Z - S for static-linked CRT library, D for multithreaded DLL CRT library
//
#if defined(_MSC_VER) && !defined(__CASCLIB_SELF__)

  #ifdef _DEBUG                                 // DEBUG VERSIONS
    #ifndef _UNICODE
      #ifdef _DLL
        #pragma comment(lib, "CascLibDAD.lib") // Debug Ansi CRT-DLL version
      #else
        #pragma comment(lib, "CascLibDAS.lib") // Debug Ansi CRT-LIB version
      #endif
    #else
      #ifdef _DLL
        #pragma comment(lib, "CascLibDUD.lib") // Debug Unicode CRT-DLL version
      #else
        #pragma comment(lib, "CascLibDUS.lib") // Debug Unicode CRT-LIB version
      #endif
    #endif
  #else                                         // RELEASE VERSIONS
    #ifndef _UNICODE
      #ifdef _DLL
        #pragma comment(lib, "CascLibRAD.lib") // Release Ansi CRT-DLL version
      #else
        #pragma comment(lib, "CascLibRAS.lib") // Release Ansi CRT-LIB version
      #endif
    #else
      #ifdef _DLL
        #pragma comment(lib, "CascLibRUD.lib") // Release Unicode CRT-DLL version
      #else
        #pragma comment(lib, "CascLibRUS.lib") // Release Unicode CRT-LIB version
      #endif
    #endif
  #endif

#endif
//-----------------------------------------------------------------------------
// Defines

#define CASCLIB_VERSION                 0x010B  // Current version of CascLib (1.11)
#define CASCLIB_VERSION_STRING          "1.11"  // String version of CascLib version

// Values for CascOpenStorage
#define CASC_STOR_XXXXX             0x00000001  // Not used

// Values for CascOpenFile
#define CASC_OPEN_BY_NAME           0x00000000  // Open the file by name. This is the default value
#define CASC_OPEN_BY_CKEY           0x00000001  // The name is just the content key; skip ROOT file processing
#define CASC_OPEN_BY_EKEY           0x00000002  // The name is just the encoded key; skip ROOT file processing
#define CASC_OPEN_TYPE_MASK         0x0000000F  // The mask which gets open type from the dwFlags
#define CASC_STRICT_DATA_CHECK      0x00000010  // Verify all data read from a file

#define CASC_LOCALE_ALL             0xFFFFFFFF
#define CASC_LOCALE_NONE            0x00000000
#define CASC_LOCALE_UNKNOWN1        0x00000001
#define CASC_LOCALE_ENUS            0x00000002
#define CASC_LOCALE_KOKR            0x00000004
#define CASC_LOCALE_RESERVED        0x00000008
#define CASC_LOCALE_FRFR            0x00000010
#define CASC_LOCALE_DEDE            0x00000020
#define CASC_LOCALE_ZHCN            0x00000040
#define CASC_LOCALE_ESES            0x00000080
#define CASC_LOCALE_ZHTW            0x00000100
#define CASC_LOCALE_ENGB            0x00000200
#define CASC_LOCALE_ENCN            0x00000400
#define CASC_LOCALE_ENTW            0x00000800
#define CASC_LOCALE_ESMX            0x00001000
#define CASC_LOCALE_RURU            0x00002000
#define CASC_LOCALE_PTBR            0x00004000
#define CASC_LOCALE_ITIT            0x00008000
#define CASC_LOCALE_PTPT            0x00010000

#define CASC_LOCALE_BIT_ENUS        0x01
#define CASC_LOCALE_BIT_KOKR        0x02
#define CASC_LOCALE_BIT_RESERVED    0x03
#define CASC_LOCALE_BIT_FRFR        0x04
#define CASC_LOCALE_BIT_DEDE        0x05
#define CASC_LOCALE_BIT_ZHCN        0x06
#define CASC_LOCALE_BIT_ESES        0x07
#define CASC_LOCALE_BIT_ZHTW        0x08
#define CASC_LOCALE_BIT_ENGB        0x09
#define CASC_LOCALE_BIT_ENCN        0x0A
#define CASC_LOCALE_BIT_ENTW        0x0B
#define CASC_LOCALE_BIT_ESMX        0x0C
#define CASC_LOCALE_BIT_RURU        0x0D
#define CASC_LOCALE_BIT_PTBR        0x0E
#define CASC_LOCALE_BIT_ITIT        0x0F
#define CASC_LOCALE_BIT_PTPT        0x10


#define MAX_CASC_KEY_LENGTH               0x10  // Maximum length of the key (equal to MD5 hash)

#ifndef MD5_HASH_SIZE
#define MD5_HASH_SIZE                     0x10
#define MD5_STRING_SIZE                   0x20
#endif

#ifndef SHA1_DIGEST_SIZE
#define SHA1_DIGEST_SIZE                  0x14  // 160 bits
#endif

#ifndef LANG_NEUTRAL
#define LANG_NEUTRAL                      0x00  // Neutral locale
#endif

// Return value for CascGetFileSize and CascSetFilePointer
#define CASC_INVALID_INDEX          0xFFFFFFFF
#define CASC_INVALID_SIZE           0xFFFFFFFF
#define CASC_INVALID_POS            0xFFFFFFFF
#define CASC_INVALID_ID             0xFFFFFFFF

// Flags for CascGetStorageInfo
#define CASC_FEATURE_HAS_NAMES      0x00000001  // The storage contains file names

//-----------------------------------------------------------------------------
// Structures

typedef enum _CASC_STORAGE_INFO_CLASS
{
    // Return the total number of unique files in the storage. Note that files
    // can exist under different names, so the total number of files in the archive
    // can be higher than the value returned by this info class
    CascStorageFileCount,

    CascStorageFeatures,
    CascStorageGameInfo,
    CascStorageGameBuild,
    CascStorageInstalledLocales,
    CascStorageInfoClassMax

} CASC_STORAGE_INFO_CLASS, *PCASC_STORAGE_INFO_CLASS;

typedef enum _CASC_FILE_INFO_CLASS
{
    CascFileContentKey,
    CascFileEncodedKey,
    CascFileInfoClassMax
} CASC_FILE_INFO_CLASS, *PCASC_FILE_INFO_CLASS;

// Query key for a file. Contains CKey [+EKey]
typedef struct _QUERY_KEY
{
    LPBYTE pbData;
    size_t cbData;
} QUERY_KEY, *PQUERY_KEY;

// Query size for a file. Contains CSize + ESize
typedef struct _QUERY_SIZE
{
    DWORD ContentSize;
    DWORD EncodedSize;
} QUERY_SIZE, *PQUERY_SIZE;

// Structure for SFileFindFirstFile and SFileFindNextFile
typedef struct _CASC_FIND_DATA
{
    // Full name of the found file. In case when this is CKey/EKey,
    // this will be just string representation of the key stored in 'FileKey'
    char   szFileName[MAX_PATH];
    
    // Plain name of the found file. Pointing inside the 'szFileName' array
    char * szPlainName;

    // Content/Encoded key. The type can be determined by dwOpenFlags
    // (CASC_OPEN_BY_CKEY vs CASC_OPEN_BY_EKEY)
    BYTE   FileKey[MD5_HASH_SIZE];

    // Locale flags. Only for games that support locale flags (WoW)
    DWORD  dwLocaleFlags;

    // File data ID. Only for games that support File data ID (WoW)
    DWORD  dwFileDataId;
    
    // Size of the file, as retrieved from encoding entry or index entry
    DWORD  dwFileSize;

    // It is recommended to use this value for subsequent CascOpenFile
    // Contains valid value if the 'szFileName' contains file key.
    DWORD  dwOpenFlags;

} CASC_FIND_DATA, *PCASC_FIND_DATA;

//-----------------------------------------------------------------------------
// Callback functions

typedef struct TFileStream TFileStream;
typedef void (WINAPI * STREAM_DOWNLOAD_CALLBACK)(void * pvUserData, ULONGLONG ByteOffset, DWORD dwTotalBytes);

//-----------------------------------------------------------------------------
// We have our own qsort implementation, optimized for sorting array of pointers

void qsort_pointer_array(void ** base, size_t num, int (*compare)(const void *, const void *, const void *), const void * context);

//-----------------------------------------------------------------------------
// Functions for storage manipulation

bool  WINAPI CascOpenStorage(const TCHAR * szDataPath, DWORD dwLocaleMask, HANDLE * phStorage);
bool  WINAPI CascGetStorageInfo(HANDLE hStorage, CASC_STORAGE_INFO_CLASS InfoClass, void * pvStorageInfo, size_t cbStorageInfo, size_t * pcbLengthNeeded);
bool  WINAPI CascCloseStorage(HANDLE hStorage);

bool  WINAPI CascOpenFileByEKey(HANDLE hStorage, PQUERY_KEY pCKey, PQUERY_KEY pEKey, DWORD dwOpenFlags, DWORD dwEncodedSize, HANDLE * phFile);
bool  WINAPI CascOpenFileByCKey(HANDLE hStorage, PQUERY_KEY pCKey, DWORD dwOpenFlags, HANDLE * phFile);
bool  WINAPI CascOpenFile(HANDLE hStorage, const char * szFileName, DWORD dwLocaleFlags, DWORD dwOpenFlags, HANDLE * phFile);
bool  WINAPI CascGetFileInfo(HANDLE hFile, CASC_FILE_INFO_CLASS InfoClass, void * pvFileInfo, size_t cbFileInfo, size_t * pcbLengthNeeded);
DWORD WINAPI CascGetFileSize(HANDLE hFile, PDWORD pdwFileSizeHigh);
DWORD WINAPI CascGetFileId(HANDLE hStorage, const char * szFileName);
DWORD WINAPI CascSetFilePointer(HANDLE hFile, LONG lFilePos, LONG * plFilePosHigh, DWORD dwMoveMethod);
bool  WINAPI CascReadFile(HANDLE hFile, void * lpBuffer, DWORD dwToRead, PDWORD pdwRead);
bool  WINAPI CascCloseFile(HANDLE hFile);

HANDLE WINAPI CascFindFirstFile(HANDLE hStorage, const char * szMask, PCASC_FIND_DATA pFindData, const TCHAR * szListFile);
bool  WINAPI CascFindNextFile(HANDLE hFind, PCASC_FIND_DATA pFindData);
bool  WINAPI CascFindClose(HANDLE hFind);

//-----------------------------------------------------------------------------
// GetLastError/SetLastError support for non-Windows platform

#ifndef PLATFORM_WINDOWS

DWORD GetLastError();
void SetLastError(DWORD dwErrCode);

#endif  // PLATFORM_WINDOWS

#ifdef __cplusplus
}   // extern "C"
#endif

#endif  // __CASCLIB_H__
