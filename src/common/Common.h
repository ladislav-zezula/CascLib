/*****************************************************************************/
/* CascCommon.h                           Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Common functions for CascLib                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascCommon.h                    */
/*****************************************************************************/

#ifndef __COMMON_H__
#define __COMMON_H__

//-----------------------------------------------------------------------------
// Common macros

// Macro for building 64-bit file offset from two 32-bit
#define MAKE_OFFSET64(hi, lo)      (((ULONGLONG)hi << 32) | (ULONGLONG)lo)

#ifndef ALIGN_TO_SIZE
#define ALIGN_TO_SIZE(x, a)   (((x) + (a)-1) & ~((a)-1))
#endif

//-----------------------------------------------------------------------------
// Common structures

// Structure for both static content key (CKey) and encoded key (EKey)
// The CKey is a MD5 hash of the file data.
// The EKey is (shortened) MD5 hash of the file header, which contains MD5 hashes of all the logical blocks of the file.
typedef struct _CONTENT_KEY
{
    BYTE Value[MD5_HASH_SIZE];                      // MD5 of the file

} CONTENT_KEY, *PCONTENT_KEY, ENCODED_KEY, *PENCODED_KEY;

// Helper structure for merging file paths
typedef struct _PATH_BUFFER
{
    char * szBegin;
    char * szPtr;
    char * szEnd;
} PATH_BUFFER, *PPATH_BUFFER;

//-----------------------------------------------------------------------------
// Conversion tables

extern unsigned char AsciiToLowerTable_Slash[256];
extern unsigned char AsciiToUpperTable_BkSlash[256];
extern unsigned char IntToHexChar[];

//-----------------------------------------------------------------------------
// Big endian number manipulation

DWORD ConvertBytesToInteger_2(LPBYTE ValueAsBytes);
DWORD ConvertBytesToInteger_3(LPBYTE ValueAsBytes);
DWORD ConvertBytesToInteger_4(LPBYTE ValueAsBytes);
DWORD ConvertBytesToInteger_X(LPBYTE ValueAsBytes, DWORD dwByteSize);
DWORD ConvertBytesToInteger_4_LE(LPBYTE ValueAsBytes);
ULONGLONG ConvertBytesToInteger_5(LPBYTE ValueAsBytes);
void ConvertIntegerToBytes_4(DWORD Value, LPBYTE ValueAsBytes);

//-----------------------------------------------------------------------------
// Linear data stream manipulation

LPBYTE CaptureInteger32(LPBYTE pbDataPtr, LPBYTE pbDataEnd, PDWORD PtrValue);
LPBYTE CaptureInteger32_BE(LPBYTE pbDataPtr, LPBYTE pbDataEnd, PDWORD PtrValue);
LPBYTE CaptureByteArray(LPBYTE pbDataPtr, LPBYTE pbDataEnd, size_t nLength, LPBYTE pbOutput);
LPBYTE CaptureContentKey(LPBYTE pbDataPtr, LPBYTE pbDataEnd, PCONTENT_KEY * PtrCKey);
LPBYTE CaptureArray_(LPBYTE pbDataPtr, LPBYTE pbDataEnd, LPBYTE * PtrArray, size_t ItemSize, size_t ItemCount);

#define CaptureArray(pbDataPtr, pbDataEnd, PtrArray, type, count) CaptureArray_(pbDataPtr, pbDataEnd, PtrArray, sizeof(type), count) 

//-----------------------------------------------------------------------------
// String manipulation

void CopyString(char * szTarget, const char * szSource, size_t cchLength);
void CopyString(wchar_t * szTarget, const char * szSource, size_t cchLength);
void CopyString(char * szTarget, const wchar_t * szSource, size_t cchLength);

char * CascNewStr(const char * szString, size_t nCharsToReserve);
wchar_t * CascNewStr(const wchar_t * szString, size_t nCharsToReserve);

TCHAR * CascNewStrFromAnsi(const char * szBegin, const char * szEnd);

TCHAR * CombinePath(const TCHAR * szPath, const TCHAR * szSubDir);
TCHAR * CombinePathAndString(const TCHAR * szPath, const char * szString, size_t nLength);

size_t NormalizeFileName_UpperBkSlash(char * szNormName, const char * szFileName, size_t cchMaxChars);
size_t NormalizeFileName_LowerSlash(char * szNormName, const char * szFileName, size_t cchMaxChars);

ULONGLONG CalcNormNameHash(const char * szNormName, size_t nLength);
ULONGLONG CalcFileNameHash(const char * szFileName);

int ConvertDigitToInt32(const TCHAR * szString, PDWORD PtrValue);
int ConvertStringToInt08(const char * szString, PDWORD PtrValue);
int ConvertStringToInt32(const TCHAR * szString, size_t nMaxDigits, PDWORD PtrValue);
int ConvertStringToBinary(const char * szString, size_t nMaxDigits, LPBYTE pbBinary);
char * StringFromBinary(LPBYTE pbBinary, size_t cbBinary, char * szBuffer);
char * StringFromMD5(LPBYTE md5, char * szBuffer);

//-----------------------------------------------------------------------------
// File name utilities

bool CheckWildCard(const char * szString, const char * szWildCard);
const wchar_t * GetPlainFileName(const wchar_t * szFileName);
const char * GetPlainFileName(const char * szFileName);

//-----------------------------------------------------------------------------
// Hashing functions

ULONGLONG HashStringJenkins(const char * szFileName);

bool IsValidMD5(LPBYTE pbMd5);
void CalculateDataBlockHash(void * pvDataBlock, DWORD cbDataBlock, LPBYTE md5_hash);
bool VerifyDataBlockHash(void * pvDataBlock, DWORD cbDataBlock, LPBYTE expected_md5);

//-----------------------------------------------------------------------------
// Scanning a directory

typedef bool (*INDEX_FILE_FOUND)(const TCHAR * szFileName, PDWORD IndexArray, PDWORD OldIndexArray, void * pvContext);

bool DirectoryExists(const TCHAR * szDirectory);

int ScanIndexDirectory(
    const TCHAR * szIndexPath,
    INDEX_FILE_FOUND pfnOnFileFound,
    PDWORD IndexArray,
    PDWORD OldIndexArray,
    void * pvContext
    );

#endif // __COMMON_H__
