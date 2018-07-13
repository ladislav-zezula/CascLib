/*****************************************************************************/
/* CascRootFile_MNDX.cpp                  Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Common functions for CascLib                                              */
/* Note: "HOTS" refers to Play.exe, v2.5.0.29049 (Heroes of the Storm Alpha) */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 18.05.14  1.00  Lad  The first version of CascRootFile_MNDX.cpp           */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local defines

#define MNDX_MAR_SIGNATURE 0x0052414d           // 'MAR\0'
#define MAX_MAR_FILES               3           // Maximum of 3 MAR files are supported

#define MNDX_SEARCH_INITIALIZING    0
#define MNDX_SEARCH_SEARCHING       2
#define MNDX_SEARCH_FINISHED        4

#define MNDX_MAX_ENTRIES(type)  (0xFFFFFFFF / sizeof(type))

//-----------------------------------------------------------------------------
// Local structures

typedef struct _BASEVALS
{
    DWORD BaseValue;                // Always used as base value
    DWORD AddValue0 : 7;            // Used when (((index >> 0x06) & 0x07) - 1) == 0
    DWORD AddValue1 : 8;            // Used when (((index >> 0x06) & 0x07) - 1) == 1
    DWORD AddValue2 : 8;            // Used when (((index >> 0x06) & 0x07) - 1) == 2
    DWORD AddValue3 : 9;            // Used when (((index >> 0x06) & 0x07) - 1) == 3
    DWORD AddValue4 : 9;            // Used when (((index >> 0x06) & 0x07) - 1) == 4
    DWORD AddValue5 : 9;            // Used when (((index >> 0x06) & 0x07) - 1) == 5
    DWORD AddValue6 : 9;            // Used when (((index >> 0x06) & 0x07) - 1) == 6
    DWORD __xalignment : 5;         // Filling
} BASEVALS, *PBASEVALS;

typedef struct _HASH_ENTRY
{
    DWORD NodeIndex;                                // Index of the path node
    DWORD NextIndex;                                // ID of the first subnode in the hash table

    union
    {
        DWORD FragmentOffset;                       // Offset of the path fragment in the TPathFragmentTable
        DWORD ChildTableIndex;                      // Starting search index for the child database (if child database is present)
        char SingleChar;                            // If the upper 24 bits of the FragmentOffset is 0xFFFFFFFF, this single character
    };
                                                    // Otherwise --> Offset to the name fragment table
} HASH_ENTRY, *PHASH_ENTRY;

typedef struct _FILE_MNDX_HEADER
{
    DWORD Signature;                                // 'MNDX'
    DWORD HeaderVersion;                            // Must be <= 2
    DWORD FormatVersion;

} FILE_MNDX_HEADER, *PFILE_MNDX_HEADER;

typedef struct _MNDX_PACKAGE
{
    char * szFileName;                              // Pointer to file name
    size_t nLength;                                 // Length of the file name
    DWORD nIndex;                                   // Package index

} MNDX_PACKAGE, *PMNDX_PACKAGE;

// Root file entry for CASC storages with MNDX root file (Heroes of the Storm)
// Corresponds to the in-file structure
typedef struct _MNDX_ROOT_ENTRY
{
    DWORD Flags;                                    // High 8 bits: Flags, low 24 bits: package index
    BYTE  CKey[MD5_HASH_SIZE];                      // Content key for the file
    DWORD ContentSize;                              // Uncompressed file size, in bytes

} MNDX_ROOT_ENTRY, *PMNDX_ROOT_ENTRY;

typedef struct _FILE_MAR_INFO
{
    DWORD MarIndex;
    DWORD MarDataSize;
    DWORD MarDataSizeHi;
    DWORD MarDataOffset;
    DWORD MarDataOffsetHi;
} FILE_MAR_INFO, *PFILE_MAR_INFO;

//-----------------------------------------------------------------------------
// Local variables

unsigned char table_1BA1818[0x800] =
{
    0x07, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x06, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x07, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x06, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x07, 0x07, 0x07, 0x01, 0x07, 0x02, 0x02, 0x01, 0x07, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x07, 0x04, 0x04, 0x01, 0x04, 0x02, 0x02, 0x01, 0x04, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x07, 0x05, 0x05, 0x01, 0x05, 0x02, 0x02, 0x01, 0x05, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x05, 0x04, 0x04, 0x01, 0x04, 0x02, 0x02, 0x01, 0x04, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x07, 0x06, 0x06, 0x01, 0x06, 0x02, 0x02, 0x01, 0x06, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x06, 0x04, 0x04, 0x01, 0x04, 0x02, 0x02, 0x01, 0x04, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x06, 0x05, 0x05, 0x01, 0x05, 0x02, 0x02, 0x01, 0x05, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x05, 0x04, 0x04, 0x01, 0x04, 0x02, 0x02, 0x01, 0x04, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x07, 0x07, 0x07, 0x01, 0x07, 0x02, 0x02, 0x01, 0x07, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x07, 0x04, 0x04, 0x01, 0x04, 0x02, 0x02, 0x01, 0x04, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x07, 0x05, 0x05, 0x01, 0x05, 0x02, 0x02, 0x01, 0x05, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x05, 0x04, 0x04, 0x01, 0x04, 0x02, 0x02, 0x01, 0x04, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x07, 0x06, 0x06, 0x01, 0x06, 0x02, 0x02, 0x01, 0x06, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x06, 0x04, 0x04, 0x01, 0x04, 0x02, 0x02, 0x01, 0x04, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x06, 0x05, 0x05, 0x01, 0x05, 0x02, 0x02, 0x01, 0x05, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x05, 0x04, 0x04, 0x01, 0x04, 0x02, 0x02, 0x01, 0x04, 0x03, 0x03, 0x01, 0x03, 0x02, 0x02, 0x01,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x02, 0x07, 0x07, 0x07, 0x03, 0x07, 0x03, 0x03, 0x02,
    0x07, 0x07, 0x07, 0x04, 0x07, 0x04, 0x04, 0x02, 0x07, 0x04, 0x04, 0x03, 0x04, 0x03, 0x03, 0x02,
    0x07, 0x07, 0x07, 0x05, 0x07, 0x05, 0x05, 0x02, 0x07, 0x05, 0x05, 0x03, 0x05, 0x03, 0x03, 0x02,
    0x07, 0x05, 0x05, 0x04, 0x05, 0x04, 0x04, 0x02, 0x05, 0x04, 0x04, 0x03, 0x04, 0x03, 0x03, 0x02,
    0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x02, 0x07, 0x06, 0x06, 0x03, 0x06, 0x03, 0x03, 0x02,
    0x07, 0x06, 0x06, 0x04, 0x06, 0x04, 0x04, 0x02, 0x06, 0x04, 0x04, 0x03, 0x04, 0x03, 0x03, 0x02,
    0x07, 0x06, 0x06, 0x05, 0x06, 0x05, 0x05, 0x02, 0x06, 0x05, 0x05, 0x03, 0x05, 0x03, 0x03, 0x02,
    0x06, 0x05, 0x05, 0x04, 0x05, 0x04, 0x04, 0x02, 0x05, 0x04, 0x04, 0x03, 0x04, 0x03, 0x03, 0x02,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x02, 0x07, 0x07, 0x07, 0x03, 0x07, 0x03, 0x03, 0x02,
    0x07, 0x07, 0x07, 0x04, 0x07, 0x04, 0x04, 0x02, 0x07, 0x04, 0x04, 0x03, 0x04, 0x03, 0x03, 0x02,
    0x07, 0x07, 0x07, 0x05, 0x07, 0x05, 0x05, 0x02, 0x07, 0x05, 0x05, 0x03, 0x05, 0x03, 0x03, 0x02,
    0x07, 0x05, 0x05, 0x04, 0x05, 0x04, 0x04, 0x02, 0x05, 0x04, 0x04, 0x03, 0x04, 0x03, 0x03, 0x02,
    0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x02, 0x07, 0x06, 0x06, 0x03, 0x06, 0x03, 0x03, 0x02,
    0x07, 0x06, 0x06, 0x04, 0x06, 0x04, 0x04, 0x02, 0x06, 0x04, 0x04, 0x03, 0x04, 0x03, 0x03, 0x02,
    0x07, 0x06, 0x06, 0x05, 0x06, 0x05, 0x05, 0x02, 0x06, 0x05, 0x05, 0x03, 0x05, 0x03, 0x03, 0x02,
    0x06, 0x05, 0x05, 0x04, 0x05, 0x04, 0x04, 0x02, 0x05, 0x04, 0x04, 0x03, 0x04, 0x03, 0x03, 0x02,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x03,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x04, 0x07, 0x07, 0x07, 0x04, 0x07, 0x04, 0x04, 0x03,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x07, 0x07, 0x07, 0x05, 0x07, 0x05, 0x05, 0x03,
    0x07, 0x07, 0x07, 0x05, 0x07, 0x05, 0x05, 0x04, 0x07, 0x05, 0x05, 0x04, 0x05, 0x04, 0x04, 0x03,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x03,
    0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x04, 0x07, 0x06, 0x06, 0x04, 0x06, 0x04, 0x04, 0x03,
    0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x05, 0x07, 0x06, 0x06, 0x05, 0x06, 0x05, 0x05, 0x03,
    0x07, 0x06, 0x06, 0x05, 0x06, 0x05, 0x05, 0x04, 0x06, 0x05, 0x05, 0x04, 0x05, 0x04, 0x04, 0x03,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x03,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x04, 0x07, 0x07, 0x07, 0x04, 0x07, 0x04, 0x04, 0x03,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x07, 0x07, 0x07, 0x05, 0x07, 0x05, 0x05, 0x03,
    0x07, 0x07, 0x07, 0x05, 0x07, 0x05, 0x05, 0x04, 0x07, 0x05, 0x05, 0x04, 0x05, 0x04, 0x04, 0x03,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x03,
    0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x04, 0x07, 0x06, 0x06, 0x04, 0x06, 0x04, 0x04, 0x03,
    0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x05, 0x07, 0x06, 0x06, 0x05, 0x06, 0x05, 0x05, 0x03,
    0x07, 0x06, 0x06, 0x05, 0x06, 0x05, 0x05, 0x04, 0x06, 0x05, 0x05, 0x04, 0x05, 0x04, 0x04, 0x03,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x07, 0x07, 0x07, 0x05, 0x07, 0x05, 0x05, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x05,
    0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x05, 0x07, 0x06, 0x06, 0x05, 0x06, 0x05, 0x05, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x07, 0x07, 0x07, 0x05, 0x07, 0x05, 0x05, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x05,
    0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x05, 0x07, 0x06, 0x06, 0x05, 0x06, 0x05, 0x05, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x05,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x05,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07
};

//-----------------------------------------------------------------------------
// Local functions - Number of set bits in an integer

// HOTS: inlined
static DWORD GetNumberOfSetBits(DWORD Value32)
{
    Value32 = ((Value32 >> 1) & 0x55555555) + (Value32 & 0x55555555);
    Value32 = ((Value32 >> 2) & 0x33333333) + (Value32 & 0x33333333);
    Value32 = ((Value32 >> 4) & 0x0F0F0F0F) + (Value32 & 0x0F0F0F0F);

    return (Value32 * 0x01010101);
}

static LPBYTE CaptureData(LPBYTE pbRootPtr, LPBYTE pbRootEnd, void * pvBuffer, size_t cbLength)
{
    // Check whether there is enough data in the buffer
    if((pbRootPtr + cbLength) > pbRootEnd)
        return NULL;

    // Copy the data
    memcpy(pvBuffer, pbRootPtr, cbLength);
    return pbRootPtr + cbLength;
}

#define GetNumbrOfSetBits32(x)  (GetNumberOfSetBits(x) >> 0x18)

//-----------------------------------------------------------------------------
// The TPathStop structure

struct TPathStop
{
    TPathStop()
    {
        LoBitsIndex = 0;
        field_4 = 0;
        Count = 0;
        HiBitsIndex_PathFragment = CASC_INVALID_INDEX;
        field_10 = 0xFFFFFFFF;
    }

    TPathStop(DWORD arg_0, DWORD arg_4, DWORD arg_8)
    {
        LoBitsIndex = arg_0;
        field_4 = arg_4;
        Count = arg_8;
        HiBitsIndex_PathFragment = CASC_INVALID_INDEX;
        field_10 = 0xFFFFFFFF;
    }

    DWORD LoBitsIndex;
    DWORD field_4;
    DWORD Count;
    DWORD HiBitsIndex_PathFragment;
    DWORD field_10;
};

//-----------------------------------------------------------------------------
// Basic array implementations

class TByteStream
{
    public:

    // HOTS: 01959990
    TByteStream()
    {
        pbByteData = NULL;
        pvMappedFile = NULL;
        cbByteData = 0;
        field_C = 0;
        hFile = 0;
        hMap = 0;
    }

    // HOTS: 19599F0
    template <typename T>
    int GetBytes(size_t length, T ** Pointer)
    {
        // Is there enough bytes in the array?
        if(length > cbByteData)
            return ERROR_BAD_FORMAT;

        // Give the buffer to the caller
        Pointer[0] = (T *)(pbByteData);

        // Move pointers
        pbByteData += length;
        cbByteData -= length;
        return ERROR_SUCCESS;
    }

    int CopyBytes(void * value, size_t length)
    {
        // Is there enough bytes in the array?
        if(length > cbByteData)
            return ERROR_BAD_FORMAT;

        // Give the buffer to the caller
        memcpy(value, pbByteData, length);

        // Move pointers
        pbByteData += length;
        cbByteData -= length;
        return ERROR_SUCCESS;
    }

    // HOTS: 1959A60
    int SkipBytes(size_t cbByteCount)
    {
        LPBYTE Pointer;

        return GetBytes<BYTE>(cbByteCount, &Pointer);
    }

    // HOTS: 1959AF0
    int SetByteBuffer(LPBYTE pbNewByteData, size_t cbNewByteData)
    {
        if(pbNewByteData != NULL || cbNewByteData == 0)
        {
            pbByteData = pbNewByteData;
            cbByteData = cbNewByteData;
            return ERROR_SUCCESS;
        }

        return ERROR_INVALID_PARAMETER;
    }

    // HOTS: 1957160 <DWORD>
    template <typename T>
    int GetValue(T & Value)
    {
        T * Pointer;
        int nError;

        nError = GetBytes(sizeof(T), (LPBYTE *)(&Pointer));
        if(nError != ERROR_SUCCESS)
            return nError;

        Value = Pointer[0];
        return ERROR_SUCCESS;
    }

    // Retrieves the item count in the array
    template <typename T>
    int GetArrayItemCount(DWORD & ArraySize, DWORD & ItemCount)
    {
        ULONGLONG ByteCount;
        int nError;

        // The first 8 bytes is the byte size of the array
        nError = GetValue<ULONGLONG>(ByteCount);
        if(nError != ERROR_SUCCESS)
            return nError;

        // Extract the number of bytes
        if(ByteCount > 0xFFFFFFFF || (ByteCount % sizeof(T)) != 0)
            return ERROR_BAD_FORMAT;

        // Give the result to the caller
        ItemCount = (DWORD)(ByteCount / sizeof(T));
        ArraySize = (DWORD)(ByteCount);
        return ERROR_SUCCESS;
    }

    // HOTS: 1957190: <DWORD>
    // HOTS: 19571E0: <BASEVALS>
    // HOTS: 1957230: <BYTE>
    // HOTS: 1957280: <HASH_ENTRY>
    template <typename T>
    int GetArray(T ** Pointer, size_t ItemCount)
    {
        int nError = ERROR_SUCCESS;

        // Verify parameters
        if(Pointer == NULL && ItemCount != 0)
            return ERROR_INVALID_PARAMETER;
        if(ItemCount > MNDX_MAX_ENTRIES(T))
            return ERROR_NOT_ENOUGH_MEMORY;

        // Allocate bytes for the array
        if (Pointer != NULL)
        {
            Pointer[0] = CASC_ALLOC(T, ItemCount);
            if (Pointer[0] == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            // Get the pointer to the array
            nError = CopyBytes(Pointer[0], sizeof(T) * ItemCount);
        }

        return nError;
    }

    LPBYTE pbByteData;
    void * pvMappedFile;
    size_t cbByteData;
    DWORD field_C;
    HANDLE hFile;
    HANDLE hMap;
};

//-----------------------------------------------------------------------------
// TGenericArray interface/implementation

template <typename T>
class TGenericArray
{
    public:

    TGenericArray()
    {
        ItemArray = NULL;
        ItemCount = 0;
        MaxItemCount = 0;
        bIsValidArray = false;
    }

    ~TGenericArray()
    {
        if(ItemArray != NULL)
        {
            CASC_FREE(ItemArray);
        }
    }

    // HOTS: 1957090 (SetDwordsValid)
    // HOTS: 19570B0 (SetBaseValuesValid)
    // HOTS: 19570D0 (? SetBitsValid ?)
    // HOTS: 19570F0 (SetPathFragmentsValid)
    int SetArrayValid()
    {
        if(bIsValidArray != 0)
            return ERROR_ALREADY_EXISTS;

        bIsValidArray = true;
        return ERROR_SUCCESS;
    }

    // HOTS: 19575A0 (char)
    // HOTS: 1957600 (TPathStop)
    void SetMaxItems(DWORD NewMaxItemCount)
    {
        T * OldArray = ItemArray;
        T * NewArray;

        // Allocate new data buffer
        NewArray = CASC_ALLOC(T, NewMaxItemCount);
        if(NewArray != NULL)
        {
            // Copy the old items to the buffer
            for(size_t i = 0; i < ItemCount; i++)
            {
                NewArray[i] = ItemArray[i];
            }
        }

        ItemArray = NewArray;
        MaxItemCount = NewMaxItemCount;
        CASC_FREE(OldArray);
    }

    // HOTS: 19575A0 (char)
    // HOTS: 1957600 (TPathStop)
    void SetMaxItemsIf(DWORD NewMaxItemCount)
    {
        if(NewMaxItemCount > MaxItemCount)
        {
            if(MaxItemCount > (NewMaxItemCount / 2))
            {
                if(MaxItemCount <= (MNDX_MAX_ENTRIES(T) / 2))
                    NewMaxItemCount = MaxItemCount + MaxItemCount;
                else
                    NewMaxItemCount = MNDX_MAX_ENTRIES(T);
            }

            SetMaxItems(NewMaxItemCount);
        }
    }

    // HOTS: inline  <char>
    // HOTS: 1958330 <TPathStop>
    void Insert(T NewItem)
    {
        // Make sure we have enough capacity for the new item
        SetMaxItemsIf(ItemCount + 1);

        // Put the character to the slot that has been reserved
        ItemArray[ItemCount++] = NewItem;
    }

    // HOTS: 19583A0 <TPathStop>
    void GrowArray(DWORD NewItemCount)
    {
        DWORD OldMaxItemCount = MaxItemCount;

        // Make sure we have enough capacity for new items
        SetMaxItemsIf(NewItemCount);

        // Initialize the newly inserted items
        for(DWORD i = OldMaxItemCount; i < NewItemCount; i++)
        {
            ItemArray[i] = T();
        }

        ItemCount = NewItemCount;
    }

    // HOTS: 1957440 <DWORD>
    // HOTS: 19574E0 <BASEVALS>
    // HOTS: 1957690 <BYTE>
    // HOTS: 1957700 <HASH_ENTRY>
    // HOTS: 195A220 <char>
    // HOTS: 1958580 <TBitStream, DWORD>
    int LoadFromStream(TByteStream & InStream)
    {
        DWORD NumberOfBytes;
        int nError;

        // Get and verify the number of items
        nError = InStream.GetArrayItemCount<T>(NumberOfBytes, ItemCount);
        if(nError != ERROR_SUCCESS)
            return nError;

        // Get the pointer to the array
        nError = InStream.GetArray<T>(&ItemArray, ItemCount);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = InStream.SkipBytes((0 - (DWORD)NumberOfBytes) & 0x07);
        if(nError != ERROR_SUCCESS)
            return nError;

        return SetArrayValid();
    }

    T * ItemArray;
    DWORD ItemCount;                           // Number of items in the array
    DWORD MaxItemCount;                        // Capacity of the array
    bool bIsValidArray;
};

class TBitEntryArray : public TGenericArray<DWORD>
{
    public:

    TBitEntryArray() : TGenericArray()
    {
        BitsPerEntry = 0;
        EntryBitMask = 0;
        TotalEntries = 0;
    }

    ~TBitEntryArray()
    {}

    DWORD GetItem(DWORD EntryIndex)
    {
        DWORD dwItemIndex = (EntryIndex * BitsPerEntry) >> 0x05;
        DWORD dwStartBit = (EntryIndex * BitsPerEntry) & 0x1F;
        DWORD dwEndBit = dwStartBit + BitsPerEntry;
        DWORD dwResult;

        // If the end bit index is greater than 32,
        // we also need to load from the next 32-bit item
        if(dwEndBit > 0x20)
        {
            dwResult = (ItemArray[dwItemIndex + 1] << (0x20 - dwStartBit)) | (ItemArray[dwItemIndex] >> dwStartBit);
        }
        else
        {
            dwResult = ItemArray[dwItemIndex] >> dwStartBit;
        }

        // Now we also need to mask the result by the bit mask
        return dwResult & EntryBitMask;
    }

    int LoadBitsFromStream(TByteStream & InStream)
    {
        ULONGLONG Value64 = 0;
        int nError;

        nError = LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = InStream.GetValue<DWORD>(BitsPerEntry);
        if(nError != ERROR_SUCCESS)
            return nError;
        if(BitsPerEntry > 0x20)
            return ERROR_BAD_FORMAT;

        nError = InStream.GetValue<DWORD>(EntryBitMask);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = InStream.GetValue<ULONGLONG>(Value64);
        if(nError != ERROR_SUCCESS)
            return nError;
        if(Value64 > 0xFFFFFFFF)
            return ERROR_BAD_FORMAT;
        TotalEntries = (DWORD)Value64;

        assert((BitsPerEntry * TotalEntries) / 32 <= ItemCount);
        return ERROR_SUCCESS;
    }

    DWORD BitsPerEntry;
    DWORD EntryBitMask;
    DWORD TotalEntries;
};

//-----------------------------------------------------------------------------
// TSparseArray functions

class TSparseArray
{
    public:

    TSparseArray()
    {
        TotalItemCount = 0;
        ValidItemCount = 0;
    }

    // HOTS: 1958630
    int LoadFromStream(TByteStream & InStream)
    {
        int nError;

        nError = PresenceBits.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = InStream.GetValue<DWORD>(TotalItemCount);
        if(nError != ERROR_SUCCESS)
            return nError;
        nError = InStream.GetValue<DWORD>(ValidItemCount);
        if(nError != ERROR_SUCCESS)
            return nError;
        if(ValidItemCount > TotalItemCount)
            return ERROR_FILE_CORRUPT;

        nError = BaseValues.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = ArrayDwords_38.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = ArrayDwords_50.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        return ERROR_SUCCESS;
    }

    // Returns true if the array is empty
    bool IsEmpty()
    {
        return (TotalItemCount == 0);
    }

    // Returns true if the item at n-th position is present
    bool IsItemPresent(size_t index)
    {
        // (index >> 0x05) gives the DWORD, (1 << (ItemIndex & 0x1F)) gives the bit
        return (PresenceBits.ItemArray[index >> 0x05] & (1 << (index & 0x1F))) ? true : false;
    }

    // HOTS: 1959B60
    DWORD GetIntValueAt(size_t index)
    {
        PBASEVALS pBaseValues;
        DWORD BaseValue;
        DWORD BitMask;

        //
        // Divide the index to four parts:
        //   31                      8          5           4          0
        //   |-----------------------|----------|-----------|----------|
        //   |           A           |    B     |     C     |     D    |
        //   |      (23 bits)        | (3 bits) |  (1 bit)  | (5 bits) |
        //   |-----------------------|----------|-----------|----------|
        //
        // A (23-bits): Index to the table (60 bits per entry)
        //
        // B (3 bits) : Index of the variable-bit value in the array (val[#], see below)
        //
        // C (32 bits): Number of bits to be checked (up to 0x3F bits).
        //              Number of set bits is then added to the values obtained from A and B
        //
        //   Layout of the table entry:
        //   |--------------------------------|-------|--------|--------|---------|---------|---------|---------|-----|
        //   |  Base Value                    | val[0]| val[1] | val[2] | val[3]  | val[4]  | val[5]  | val[6]  |  -  |
        //   |  32 bits                       | 7 bits| 8 bits | 8 bits | 9 bits  | 9 bits  | 9 bits  | 9 bits  |5bits|
        //   |--------------------------------|-------|--------|--------|---------|---------|---------|---------|-----|
        //

        // Bits Upper 23 bits contain index to the table
        pBaseValues = BaseValues.ItemArray + (index >> 0x09);
        BaseValue = pBaseValues->BaseValue;

        // Next 3 bits contain the index to the VBR
        switch(((index >> 0x06) & 0x07) - 1)
        {
            case 0:     // Add the 1st value (7 bits)
                BaseValue += pBaseValues->AddValue0;
                break;

            case 1:     // Add the 2nd value (8 bits)
                BaseValue += pBaseValues->AddValue1;
                break;

            case 2:     // Add the 3rd value (8 bits)
                BaseValue += pBaseValues->AddValue2;
                break;

            case 3:     // Add the 4th value (9 bits)
                BaseValue += pBaseValues->AddValue3;
                break;

            case 4:     // Add the 5th value (9 bits)
                BaseValue += pBaseValues->AddValue4;
                break;

            case 5:     // Add the 6th value (9 bits)
                BaseValue += pBaseValues->AddValue5;
                break;

            case 6:     // Add the 7th value (9 bits)
                BaseValue += pBaseValues->AddValue6;
                break;
        }

        // 0x20 bit set: (0x20 - 0x40): Also add bits from the previous DWORD
        if(index & 0x20)
            BaseValue += GetNumbrOfSetBits32(PresenceBits.ItemArray[(index >> 0x05) - 1]);

        // Lowest 5 bits (0x00 - 0x1F): Add the number of bits in the masked current DWORD
        BitMask = (1 << (index & 0x1F)) - 1;
        return BaseValue + GetNumbrOfSetBits32(PresenceBits.ItemArray[index >> 0x05] & BitMask);
    }

    // HOTS: 1959CB0
    DWORD sub_1959CB0(DWORD PathNodeId)
    {
        PBASEVALS pBaseValues;
        DWORD dwKeyShifted = (PathNodeId >> 9);
        DWORD eax, ebx, ecx, esi, edi;
        DWORD edx = PathNodeId;

        // If lower 9 bits is zero
        if ((edx & 0x1FF) == 0)
            return ArrayDwords_38.ItemArray[dwKeyShifted];

        eax = ArrayDwords_38.ItemArray[dwKeyShifted] >> 9;
        esi = (ArrayDwords_38.ItemArray[dwKeyShifted + 1] + 0x1FF) >> 9;
        PathNodeId = esi;

        if ((eax + 0x0A) >= esi)
        {
            // HOTS: 1959CF7
            pBaseValues = BaseValues.ItemArray + eax + 1;
            edi = (eax << 0x09);
            ebx = edi - pBaseValues->BaseValue + 0x200;
            while (edx >= ebx)
            {
                // HOTS: 1959D14
                edi += 0x200;
                pBaseValues++;

                ebx = edi - pBaseValues->BaseValue + 0x200;
                eax++;
            }
        }
        else
        {
            // HOTS: 1959D2E
            while ((eax + 1) < esi)
            {
                // HOTS: 1959D38
                // ecx = Struct68_00.BaseValues.ItemArray;
                esi = (esi + eax) >> 1;
                ebx = (esi << 0x09) - BaseValues.ItemArray[esi].BaseValue;
                if (edx < ebx)
                {
                    // HOTS: 01959D4B
                    PathNodeId = esi;
                }
                else
                {
                    // HOTS: 1959D50
                    eax = esi;
                    esi = PathNodeId;
                }
            }
        }

        // HOTS: 1959D5F
        pBaseValues = BaseValues.ItemArray + eax;
        edx += pBaseValues->BaseValue - (eax << 0x09);
        edi = (eax << 4);

        ecx = pBaseValues->AddValue3;
        ebx = 0x100 - pBaseValues->AddValue3;
        if (edx < ebx)
        {
            // HOTS: 1959D8C
            ecx = pBaseValues->AddValue1;
            esi = 0x80 - ecx;
            if (edx < esi)
            {
                // HOTS: 01959DA2
                eax = pBaseValues->AddValue0;
                ecx = 0x40 - eax;
                if (edx >= ecx)
                {
                    // HOTS: 01959DB7
                    edi += 2;
                    edx = edx + eax - 0x40;
                }
            }
            else
            {
                // HOTS: 1959DC0
                eax = pBaseValues->AddValue2;
                esi = 0xC0 - eax;
                if (edx < esi)
                {
                    // HOTS: 1959DD3
                    edi += 4;
                    edx = edx + ecx - 0x80;
                }
                else
                {
                    // HOTS: 1959DD3
                    edi += 6;
                    edx = edx + eax - 0xC0;
                }
            }
        }
        else
        {
            // HOTS: 1959DE8
            eax = pBaseValues->AddValue5;
            ebx = 0x180 - eax;
            if (edx < ebx)
            {
                // HOTS: 01959E00
                esi = pBaseValues->AddValue4;
                eax = (0x140 - esi);
                if (edx < eax)
                {
                    // HOTS: 1959E11
                    edi = edi + 8;
                    edx = edx + ecx - 0x100;
                }
                else
                {
                    // HOTS: 1959E1D
                    edi = edi + 0x0A;
                    edx = edx + esi - 0x140;
                }
            }
            else
            {
                // HOTS: 1959E29
                esi = pBaseValues->AddValue6;
                ecx = (0x1C0 - esi);
                if (edx < ecx)
                {
                    // HOTS: 1959E3D
                    edi = edi + 0x0C;
                    edx = edx + eax - 0x180;
                }
                else
                {
                    // HOTS: 1959E49
                    edi = edi + 0x0E;
                    edx = edx + esi - 0x1C0;
                }
            }
        }

        // HOTS: 1959E53:
        // Calculate the number of bits set in the value of "ecx"
        ecx = ~PresenceBits.ItemArray[edi];
        eax = GetNumberOfSetBits(ecx);
        esi = eax >> 0x18;

        if (edx >= esi)
        {
            // HOTS: 1959ea4
            ecx = ~PresenceBits.ItemArray[++edi];
            edx = edx - esi;
            eax = GetNumberOfSetBits(ecx);
        }

        // HOTS: 1959eea
        // ESI gets the number of set bits in the lower 16 bits of ECX
        esi = (eax >> 0x08) & 0xFF;
        edi = edi << 0x05;
        if (edx < esi)
        {
            // HOTS: 1959EFC
            eax = eax & 0xFF;
            if (edx >= eax)
            {
                // HOTS: 1959F05
                ecx >>= 0x08;
                edi += 0x08;
                edx -= eax;
            }
        }
        else
        {
            // HOTS: 1959F0D
            eax = (eax >> 0x10) & 0xFF;
            if (edx < eax)
            {
                // HOTS: 1959F19
                ecx >>= 0x10;
                edi += 0x10;
                edx -= esi;
            }
            else
            {
                // HOTS: 1959F23
                ecx >>= 0x18;
                edi += 0x18;
                edx -= eax;
            }
        }

        // HOTS: 1959f2b
        edx = edx << 0x08;
        ecx = ecx & 0xFF;

        // BUGBUG: Possible buffer overflow here. Happens when dwItemIndex >= 0x9C.
        // The same happens in Heroes of the Storm (build 29049), so I am not sure
        // if this is a bug or a case that never happens
        assert((ecx + edx) < sizeof(table_1BA1818));
        return table_1BA1818[ecx + edx] + edi;
    }

    DWORD sub_1959F50(DWORD arg_0)
    {
        PBASEVALS pBaseValues;
        PDWORD ItemArray;
        DWORD eax, ebx, ecx, edx, esi, edi;

        edx = arg_0;
        eax = arg_0 >> 0x09;
        if ((arg_0 & 0x1FF) == 0)
            return ArrayDwords_50.ItemArray[eax];

        ItemArray = ArrayDwords_50.ItemArray + eax;
        eax = (ItemArray[0] >> 0x09);
        edi = (ItemArray[1] + 0x1FF) >> 0x09;

        if ((eax + 0x0A) > edi)
        {
            // HOTS: 01959F94
            pBaseValues = BaseValues.ItemArray + eax + 1;
            while (edx >= pBaseValues->BaseValue)
            {
                // HOTS: 1959FA3
                pBaseValues++;
                eax++;
            }
        }
        else
        {
            // Binary search
            // HOTS: 1959FAD
            if ((eax + 1) < edi)
            {
                // HOTS: 1959FB4
                esi = (edi + eax) >> 1;
                if (edx < BaseValues.ItemArray[esi].BaseValue)
                {
                    // HOTS: 1959FC4
                    edi = esi;
                }
                else
                {
                    // HOTS: 1959FC8
                    eax = esi;
                }
            }
        }

        // HOTS: 1959FD4
        pBaseValues = BaseValues.ItemArray + eax;
        edx = edx - pBaseValues->BaseValue;
        edi = eax << 0x04;
        ebx = pBaseValues->AddValue3;
        if (edx < ebx)
        {
            // HOTS: 1959FF1
            esi = pBaseValues->AddValue1;
            if (edx < esi)
            {
                // HOTS: 0195A000
                eax = pBaseValues->AddValue0;
                if (edx >= eax)
                {
                    // HOTS: 195A007
                    edi = edi + 2;
                    edx = edx - eax;
                }
            }
            else
            {
                // HOTS: 195A00E
                eax = pBaseValues->AddValue2;
                if (edx < eax)
                {
                    // HOTS: 195A01A
                    edi += 4;
                    edx = edx - esi;
                }
                else
                {
                    // HOTS: 195A01F
                    edi += 6;
                    edx = edx - eax;
                }
            }
        }
        else
        {
            // HOTS: 195A026
            eax = pBaseValues->AddValue5;
            if (edx < eax)
            {
                // HOTS: 195A037
                esi = pBaseValues->AddValue4;
                if (edx < esi)
                {
                    // HOTS: 195A041
                    edi = edi + 8;
                    edx = edx - ebx;
                }
                else
                {
                    // HOTS: 195A048
                    edi = edi + 0x0A;
                    edx = edx - esi;
                }
            }
            else
            {
                // HOTS: 195A04D
                esi = pBaseValues->AddValue6;
                if (edx < esi)
                {
                    // HOTS: 195A05A
                    edi = edi + 0x0C;
                    edx = edx - eax;
                }
                else
                {
                    // HOTS: 195A061
                    edi = edi + 0x0E;
                    edx = edx - esi;
                }
            }
        }

        // HOTS: 195A066
        esi = PresenceBits.ItemArray[edi];
        eax = GetNumberOfSetBits(esi);
        ecx = eax >> 0x18;

        if (edx >= ecx)
        {
            // HOTS: 195A0B2
            esi = PresenceBits.ItemArray[++edi];
            edx = edx - ecx;
            eax = GetNumberOfSetBits(esi);
        }

        // HOTS: 195A0F6
        ecx = (eax >> 0x08) & 0xFF;

        edi = (edi << 0x05);
        if (edx < ecx)
        {
            // HOTS: 195A111
            eax = eax & 0xFF;
            if (edx >= eax)
            {
                // HOTS: 195A111
                edi = edi + 0x08;
                esi = esi >> 0x08;
                edx = edx - eax;
            }
        }
        else
        {
            // HOTS: 195A119
            eax = (eax >> 0x10) & 0xFF;
            if (edx < eax)
            {
                // HOTS: 195A125
                esi = esi >> 0x10;
                edi = edi + 0x10;
                edx = edx - ecx;
            }
            else
            {
                // HOTS: 195A12F
                esi = esi >> 0x18;
                edi = edi + 0x18;
                edx = edx - eax;
            }
        }

        esi = esi & 0xFF;
        edx = edx << 0x08;

        // BUGBUG: Potential buffer overflow
        // Happens in Heroes of the Storm when arg_0 == 0x5B
        assert((esi + edx) < sizeof(table_1BA1818));
        return table_1BA1818[esi + edx] + edi;
    }

    TGenericArray<DWORD> PresenceBits;          // Bit array for each item (1 = item is present)
    DWORD TotalItemCount;                       // Total number of items in the array
    DWORD ValidItemCount;                       // Number of present items in the array
    TGenericArray<BASEVALS> BaseValues;         // Array of base values for item indexes >= 0x200
    TGenericArray<DWORD> ArrayDwords_38;
    TGenericArray<DWORD> ArrayDwords_50;
};

//-----------------------------------------------------------------------------
// TStruct40 functions

class TStruct40
{
    public:

    TStruct40()
    {
        NodeIndex   = 0;
        ItemCount   = 0;
        PathLength  = 0;
        SearchPhase = MNDX_SEARCH_INITIALIZING;
    }

    // HOTS: 19586B0
    void BeginSearch()
    {
        // HOTS: 19586BD
        PathBuffer.ItemCount = 0;
        PathBuffer.SetMaxItemsIf(0x40);

        // HOTS: 19586E1
        // Set the new item count
        PathStops.GrowArray(0);
        PathStops.SetMaxItemsIf(4);

        PathLength = 0;
        NodeIndex = 0;
        ItemCount = 0;
        SearchPhase = MNDX_SEARCH_SEARCHING;
    }

    DWORD CalcHashValue(const char * szPath)
    {
        return (BYTE)(szPath[PathLength]) ^ (NodeIndex << 0x05) ^ NodeIndex;
    }

    TGenericArray<TPathStop> PathStops;         // Array of path checkpoints
    TGenericArray<char> PathBuffer;             // Buffer for building a file name
    DWORD NodeIndex;                            // ID of a path node being searched; starting with 0
    DWORD PathLength;                           // Length of the path in the PathBuffer
    DWORD ItemCount;
    DWORD SearchPhase;                          // 0 = initializing, 2 = searching, 4 = finished
};

//-----------------------------------------------------------------------------
// Local functions - TMndxSearch

class TMndxSearch
{
    public:

    // HOTS: 01956EE0
    TMndxSearch()
    {
        szSearchMask = NULL;
        cchSearchMask = 0;
        szFoundPath = NULL;
        cchFoundPath = 0;
        FileNameIndex = 0;
        pStruct40 = NULL;
    }

    // HOTS: 01956F00
    ~TMndxSearch()
    {
        FreeStruct40();
    }

    // HOTS: 01956F30
    int CreateStruct40()
    {
        if(pStruct40 != NULL)
            return ERROR_INVALID_PARAMETER;

        pStruct40 = new TStruct40();
        return (pStruct40 != NULL) ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }

    void FreeStruct40()
    {
        if(pStruct40 != NULL)
            delete pStruct40;
        pStruct40 = NULL;
    }

    // HOTS: 01956E70
    int SetSearchMask(
        const char * szNewSearchMask,
        size_t cchNewSearchMask)
    {
        if(szSearchMask == NULL && cchSearchMask != 0)
            return ERROR_INVALID_PARAMETER;

        if(pStruct40 != NULL)
            pStruct40->SearchPhase = MNDX_SEARCH_INITIALIZING;

        szSearchMask = szNewSearchMask;
        cchSearchMask = cchNewSearchMask;
        return ERROR_SUCCESS;
    }

    const char * szSearchMask;          // Search mask without wildcards
    size_t cchSearchMask;               // Length of the search mask
    const char * szFoundPath;           // Found path name
    size_t cchFoundPath;                // Length of the found path name
    DWORD FileNameIndex;                // Index of the file name
    TStruct40 * pStruct40;
};

//-----------------------------------------------------------------------------
// TPathFragmentTable class. This class implements table of the path fragments.
// These path fragments can either by terminated by zeros (ASCIIZ)
// or can be marked by the external "PathMarks" structure

class TPathFragmentTable
{
    public:

    // HOTS: 0195A290
    TPathFragmentTable()
    {}

    // HOTS: inlined
    ~TPathFragmentTable()
    {}

    // HOTS: 195A180
    bool ComparePathFragment(TMndxSearch * pSearch, size_t nFragmentOffset)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;
        const char * szPathFragment;
        const char * szSearchMask;

        // Do we have path fragment separators in an external structure?
        if(PathMarks.IsEmpty())
        {
            // Get the offset of the fragment to compare
            szPathFragment = (PathFragments.ItemArray + nFragmentOffset - pStruct40->PathLength);
            szSearchMask = pSearch->szSearchMask;

            // Keep searching as long as the name matches with the fragment
            while(szPathFragment[pStruct40->PathLength] == szSearchMask[pStruct40->PathLength])
            {
                // Move to the next character
                pStruct40->PathLength++;

                // Is it the end of the fragment or end of the path?
                if(szPathFragment[pStruct40->PathLength] == 0)
                    return true;
                if(pStruct40->PathLength >= pSearch->cchSearchMask)
                    return false;
            }

            return false;
        }
        else
        {
            // Get the offset of the fragment to compare.
            szPathFragment = PathFragments.ItemArray;
            szSearchMask = pSearch->szSearchMask;

            // Keep searching as long as the name matches with the fragment
            while(szPathFragment[nFragmentOffset] == szSearchMask[pStruct40->PathLength])
            {
                // Move to the next character
                pStruct40->PathLength++;

                // Is it the end of the path fragment?
                if(PathMarks.IsItemPresent(nFragmentOffset++))
                    return true;
                if(nFragmentOffset >= pSearch->cchSearchMask)
                    return false;
            }

            return false;
        }
    }

    // HOTS: 195A3F0
    void CopyPathFragment(TMndxSearch * pSearch, size_t nFragmentOffset)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;

        // Do we have path fragment separators in an external structure?
        if (PathMarks.IsEmpty())
        {
            // HOTS: 195A40C
            while (PathFragments.ItemArray[nFragmentOffset] != 0)
            {
                // Insert the character to the path being built
                pStruct40->PathBuffer.Insert(PathFragments.ItemArray[nFragmentOffset++]);
            }
        }
        else
        {
            // HOTS: 195A4B3
            while(!PathMarks.IsItemPresent(nFragmentOffset))
            {
                // Insert the character to the path being built
                pStruct40->PathBuffer.Insert(PathFragments.ItemArray[nFragmentOffset++]);
            }
        }
    }

    // HOTS: 195A570
    bool CompareAndCopyPathFragment(TMndxSearch * pSearch, size_t nFragmentOffset)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;
        const char * szPathFragment;
        const char * szSearchMask;

        // Do we have path fragment separators in an external structure?
        if(PathMarks.IsEmpty())
        {
            // Get the offset of the fragment to compare
            szPathFragment = PathFragments.ItemArray + nFragmentOffset - pStruct40->PathLength;
            szSearchMask = pSearch->szSearchMask;

            // Keep copying as long as we don't reach the end of the search mask
            while(pStruct40->PathLength < pSearch->cchSearchMask)
            {
                // HOTS: 195A5A0
                if(szPathFragment[pStruct40->PathLength] != szSearchMask[pStruct40->PathLength])
                    return false;

                // HOTS: 195A5B7
                pStruct40->PathBuffer.Insert(szPathFragment[pStruct40->PathLength++]);

                // If we found the end of the fragment, return success
                if(szPathFragment[pStruct40->PathLength] == 0)
                    return true;
            }

            // Fixup the address of the fragment
            szPathFragment += pStruct40->PathLength;

            // HOTS: 195A660
            // Now we need to copy the rest of the fragment
            while(szPathFragment[0] != 0)
            {
                pStruct40->PathBuffer.Insert(szPathFragment[0]);
                szPathFragment++;
            }
        }
        else
        {
            // Get the offset of the fragment to compare
            // HOTS: 195A6B7
            szPathFragment = PathFragments.ItemArray;
            szSearchMask = pSearch->szSearchMask;

            // Keep copying as long as we don't reach the end of the search mask
            while(nFragmentOffset < pSearch->cchSearchMask)
            {
                if(szPathFragment[nFragmentOffset] != szSearchMask[pStruct40->PathLength])
                    return false;

                pStruct40->PathBuffer.Insert(szPathFragment[nFragmentOffset]);
                pStruct40->PathLength++;

                // If we found the end of the fragment, return success
                if(PathMarks.IsItemPresent(nFragmentOffset++))
                    return true;
            }

            // Fixup the address of the fragment
            szPathFragment += nFragmentOffset;

            // Now we need to copy the rest of the fragment
            while(PathMarks.IsItemPresent(nFragmentOffset++) == 0)
            {
                // HOTS: 195A7A6
                pStruct40->PathBuffer.Insert(szPathFragment[0]);
                szPathFragment++;
            }
        }

        return true;
    }

    // HOTS: 0195A820
    int LoadFromStream(TByteStream & InStream)
    {
        int nError;

        nError = PathFragments.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        return PathMarks.LoadFromStream(InStream);
    }

    TGenericArray<char> PathFragments;
    TSparseArray PathMarks;
};

//-----------------------------------------------------------------------------
// TStruct10 functions

class TStruct10
{
    public:

    TStruct10()
    {
        field_0 = 0x03;
        field_4 = 0x200;
        field_8 = 0x1000;
        field_C = 0x20000;
    }

    // HOTS: 1956FD0
    int sub_1956FD0(DWORD dwBitMask)
    {
        switch(dwBitMask & 0xF80)
        {
            case 0x00:
                field_4 = 0x200;
                return ERROR_SUCCESS;

            case 0x80:
                field_4 = 0x80;
                return ERROR_SUCCESS;

            case 0x100:
                field_4 = 0x100;
                return ERROR_SUCCESS;

            case 0x200:
                field_4 = 0x200;
                return ERROR_SUCCESS;

            case 0x400:
                field_4 = 0x400;
                return ERROR_SUCCESS;

            case 0x800:
                field_4 = 0x800;
                return ERROR_SUCCESS;
        }

        return ERROR_INVALID_PARAMETER;
    }

    // HOTS: 1957050
    int sub_1957050(DWORD dwBitMask)
    {
        switch(dwBitMask & 0xF0000)
        {
            case 0x00:
                field_C = 0x20000;
                return ERROR_SUCCESS;

            case 0x10000:
                field_C = 0x10000;
                return ERROR_SUCCESS;

            case 0x20000:
                field_C = 0x20000;
                return ERROR_SUCCESS;
        }

        return ERROR_INVALID_PARAMETER;
    }

    // HOTS: 19572E0
    int sub_19572E0(DWORD dwBitMask)
    {
        DWORD dwSubMask;
        int nError;

        if(dwBitMask & 0xFFF00000)
            return ERROR_INVALID_PARAMETER;

        dwSubMask = dwBitMask & 0x7F;
        if(dwSubMask)
            field_0 = dwSubMask;

        nError = sub_1956FD0(dwBitMask);
        if(nError != ERROR_SUCCESS)
            return nError;

        dwSubMask = dwBitMask & 0xF000;
        if(dwSubMask == 0 || dwSubMask == 0x1000)
        {
            field_8 = 0x1000;
            return sub_1957050(dwBitMask);
        }

        if(dwSubMask == 0x2000)
        {
            field_8 = 0x2000;
            return sub_1957050(dwBitMask);
        }

        return ERROR_INVALID_PARAMETER;
    }

    // HOTS: 1957800
    int sub_1957800(DWORD dwBitMask)
    {
        return sub_19572E0(dwBitMask);
    }

    DWORD field_0;
    DWORD field_4;
    DWORD field_8;
    DWORD field_C;
};

//-----------------------------------------------------------------------------
// TFileNameDatabase interface/implementation

class TFileNameDatabase
{
    public:

    // HOTS: 01958730
    TFileNameDatabase()
    {
        HashTableMask = 0;
        field_214 = 0;
        pChildDB = NULL;
    }

    ~TFileNameDatabase()
    {
        delete pChildDB;
    }

    // Returns nonzero if the name fragment match is a single-char match
    bool IsPathFragmentSingleChar(HASH_ENTRY * pHashEntry)
    {
        return ((pHashEntry->FragmentOffset & 0xFFFFFF00) == 0xFFFFFF00);
    }

    // Returns true if the given collision path fragment is a string (aka more than 1 char)
    bool IsPathFragmentString(size_t index)
    {
        return CollisionHiBitsIndexes.IsItemPresent(index);
    }

    // HOTS: 1957350, inlined
    DWORD GetPathFragmentOffset1(DWORD index_lobits)
    {
        DWORD index_hibits = CollisionHiBitsIndexes.GetIntValueAt(index_lobits);

        return (HiBitsTable.GetItem(index_hibits) << 0x08) | LoBitsTable.ItemArray[index_lobits];
    }

    // Retrieves fragment_offset/subtable_index of the path fragment, with check for starting value
    DWORD GetPathFragmentOffset2(DWORD & index_hibits, DWORD index_lobits)
    {
        // If the hi-bits index is invalid, we need to get its starting value
        if (index_hibits == CASC_INVALID_INDEX)
        {
/*
            printf("\n");
            for (DWORD i = 0; i < CollisionHiBitsIndexes.TotalItemCount; i++)
            {
                if (CollisionHiBitsIndexes.IsItemPresent(i))
                    printf("[%02X] = %02X\n", i, CollisionHiBitsIndexes.GetIntValueAt(i));
                else
                    printf("[%02X] = NOT_PRESENT\n", i);
            }
*/
            index_hibits = CollisionHiBitsIndexes.GetIntValueAt(index_lobits);
        }
        else
        {
            index_hibits++;
        }

        // Now we use both NodeIndex and HiBits index for retrieving the path fragment index
        return (HiBitsTable.GetItem(index_hibits) << 0x08) | LoBitsTable.ItemArray[index_lobits];
    }

    // HOTS: 1956DA0
    int CreateDatabase(LPBYTE pbMarData, size_t cbMarData)
    {
        TByteStream ByteStream;
        DWORD dwSignature;
        int nError;

        if(pbMarData == NULL && cbMarData != 0)
            return ERROR_INVALID_PARAMETER;

        nError = ByteStream.SetByteBuffer(pbMarData, cbMarData);
        if(nError == ERROR_SUCCESS)
        {
            // Get pointer to MAR signature
            nError = ByteStream.GetValue<DWORD>(dwSignature);
            if(nError != ERROR_SUCCESS)
                return nError;

            // Verify the signature
            if(dwSignature != MNDX_MAR_SIGNATURE)
                return ERROR_BAD_FORMAT;

            // HOTS: 1956E11
            nError = LoadFromStream(ByteStream);
        }

        return nError;
    }

    // HOTS: 19584B0
    int SetChildDatabase(TFileNameDatabase * pNewDB)
    {
        if(pNewDB != NULL && pChildDB == pNewDB)
            return ERROR_INVALID_PARAMETER;

        if(pChildDB != NULL)
            delete pChildDB;
        pChildDB = pNewDB;
        return ERROR_SUCCESS;
    }

    // HOTS: 1957970
    bool ComparePathFragment(TMndxSearch * pSearch)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;
        PHASH_ENTRY pHashEntry;
        DWORD ColTableIndex;
        DWORD HiBitsIndex;
        DWORD NodeIndex;

        // Calculate the item hash from the current char and fragment ID
        NodeIndex = pStruct40->CalcHashValue(pSearch->szSearchMask) & HashTableMask;
        pHashEntry = HashTable.ItemArray + NodeIndex;

        // Does the hash value ID match?
        if(pHashEntry->NodeIndex == pStruct40->NodeIndex)
        {
            // Check if there is single character match
            if (!IsPathFragmentSingleChar(pHashEntry))
            {
                // Check if there is a name fragment match
                if (pChildDB != NULL)
                {
                    if (!pChildDB->ComparePathFragmentByIndex(pSearch, pHashEntry->ChildTableIndex))
                        return false;
                }
                else
                {
                    if (!PathFragmentTable.ComparePathFragment(pSearch, pHashEntry->FragmentOffset))
                        return false;
                }
            }
            else
            {
                pStruct40->PathLength++;
            }

            pStruct40->NodeIndex = pHashEntry->NextIndex;
            return true;
        }

        //
        // Conflict: Multiple node IDs give the same table index
        //

        // HOTS: 1957A0E
        ColTableIndex = CollisionTable.sub_1959CB0(pStruct40->NodeIndex) + 1;
        pStruct40->NodeIndex = (ColTableIndex - pStruct40->NodeIndex - 1);
        HiBitsIndex = CASC_INVALID_INDEX;

        // HOTS: 1957A41:
        while(CollisionTable.IsItemPresent(ColTableIndex))
        {
            // HOTS: 1957A41
            // Check if the low 8 bits if the fragment offset contain a single character
            // or an offset to a name fragment
            if(IsPathFragmentString(pStruct40->NodeIndex))
            {
                DWORD FragmentOffset = GetPathFragmentOffset2(HiBitsIndex, pStruct40->NodeIndex);
                DWORD SavePathLength = pStruct40->PathLength;       // HOTS: 1957A83

                // Do we have a child database?
                if(pChildDB != NULL)
                {
                    // HOTS: 1957AEC
                    if(pChildDB->ComparePathFragmentByIndex(pSearch, FragmentOffset))
                        return true;
                }
                else
                {
                    // HOTS: 1957AF7
                    if(PathFragmentTable.ComparePathFragment(pSearch, FragmentOffset))
                        return true;
                }

                // HOTS: 1957B0E
                // If there was partial match with the fragment, end the search
                if(pStruct40->PathLength != SavePathLength)
                    return false;
            }
            else
            {
                // HOTS: 1957B1C
                if(LoBitsTable.ItemArray[pStruct40->NodeIndex] == pSearch->szSearchMask[pStruct40->PathLength])
                {
                    pStruct40->PathLength++;
                    return true;
                }
            }

            // HOTS: 1957B32
            pStruct40->NodeIndex++;
            ColTableIndex++;
        }

        return false;
    }

    // HOTS: 1957B80
    bool ComparePathFragmentByIndex(TMndxSearch * pSearch, DWORD TableIndex)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;
        PHASH_ENTRY pHashEntry;
        DWORD eax;

        // HOTS: 1957B95
        for (;;)
        {
            // Get the hasn table item
            pHashEntry = HashTable.ItemArray + (TableIndex & HashTableMask);

            // 
            if (TableIndex == pHashEntry->NextIndex)
            {
                // HOTS: 01957BB4
                if (!IsPathFragmentSingleChar(pHashEntry))
                {
                    // HOTS: 1957BC7
                    if (pChildDB != NULL)
                    {
                        // HOTS: 1957BD3
                        if (!pChildDB->ComparePathFragmentByIndex(pSearch, pHashEntry->ChildTableIndex))
                            return false;
                    }
                    else
                    {
                        // HOTS: 1957BE0
                        if (!PathFragmentTable.ComparePathFragment(pSearch, pHashEntry->FragmentOffset))
                            return false;
                    }
                }
                else
                {
                    // HOTS: 1957BEE
                    if (pSearch->szSearchMask[pStruct40->PathLength] != pHashEntry->SingleChar)
                        return false;
                    pStruct40->PathLength++;
                }

                // HOTS: 1957C05
                TableIndex = pHashEntry->NodeIndex;
                if (TableIndex == 0)
                    return true;

                if (pStruct40->PathLength >= pSearch->cchSearchMask)
                    return false;
            }
            else
            {
                // HOTS: 1957C30
                if (IsPathFragmentString(TableIndex))
                {
                    DWORD FragmentOffset = GetPathFragmentOffset1(TableIndex);

                    // HOTS: 1957C4C
                    if (pChildDB != NULL)
                    {
                        // HOTS: 1957C58
                        if (!pChildDB->ComparePathFragmentByIndex(pSearch, FragmentOffset))
                            return false;
                    }
                    else
                    {
                        // HOTS: 1957350
                        if (!PathFragmentTable.ComparePathFragment(pSearch, FragmentOffset))
                            return false;
                    }
                }
                else
                {
                    // HOTS: 1957C8E
                    if (LoBitsTable.ItemArray[TableIndex] != pSearch->szSearchMask[pStruct40->PathLength])
                        return false;

                    pStruct40->PathLength++;
                }

                // HOTS: 1957CB2
                if (TableIndex <= field_214)
                    return true;

                if (pStruct40->PathLength >= pSearch->cchSearchMask)
                    return false;

                eax = CollisionTable.sub_1959F50(TableIndex);
                TableIndex = (eax - TableIndex - 1);
            }
        }
    }

    // HOTS: 1958D70
    void CopyPathFragmentByIndex(TMndxSearch * pSearch, DWORD TableIndex)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;
        PHASH_ENTRY pHashEntry;

        // HOTS: 1958D84
        for (;;)
        {
            pHashEntry = HashTable.ItemArray + (TableIndex & HashTableMask);
            if (TableIndex == pHashEntry->NextIndex)
            {
                // HOTS: 1958DA6
                if (!IsPathFragmentSingleChar(pHashEntry))
                {
                    // HOTS: 1958DBA
                    if (pChildDB != NULL)
                    {
                        pChildDB->CopyPathFragmentByIndex(pSearch, pHashEntry->ChildTableIndex);
                    }
                    else
                    {
                        PathFragmentTable.CopyPathFragment(pSearch, pHashEntry->FragmentOffset);
                    }
                }
                else
                {
                    // HOTS: 1958DE7
                    // Insert the low 8 bits to the path being built
                    pStruct40->PathBuffer.Insert(pHashEntry->SingleChar);
                }

                // HOTS: 1958E71
                TableIndex = pHashEntry->NodeIndex;
                if (TableIndex == 0)
                    return;
            }
            else
            {
                // HOTS: 1958E8E
                if (IsPathFragmentString(TableIndex))
                {
                    DWORD FragmentOffset = GetPathFragmentOffset1(TableIndex);

                    // HOTS: 1958EAF
                    if (pChildDB != NULL)
                    {
                        pChildDB->CopyPathFragmentByIndex(pSearch, FragmentOffset);
                    }
                    else
                    {
                        PathFragmentTable.CopyPathFragment(pSearch, FragmentOffset);
                    }
                }
                else
                {
                    // HOTS: 1958F50
                    // Insert one character to the path being built
                    pStruct40->PathBuffer.Insert(LoBitsTable.ItemArray[TableIndex]);
                }

                // HOTS: 1958FDE
                if (TableIndex <= field_214)
                    return;

                TableIndex = 0xFFFFFFFF - TableIndex + CollisionTable.sub_1959F50(TableIndex);
            }
        }
    }

    // HOTS: 1958B00
    bool CompareAndCopyPathFragment(TMndxSearch * pSearch)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;
        PHASH_ENTRY pHashEntry;
        DWORD HiBitsIndex;
        DWORD ColTableIndex;
        DWORD TableIndex;
/*
        FILE * fp = fopen("E:\\PathFragmentTable.txt", "wt");
        if (fp != NULL)
        {
            for (DWORD i = 0; i < HashTable.ItemCount; i++)
            {
                FragOffs = HashTable.ItemArray[i].FragOffs;
                fprintf(fp, "%02x ('%c') %08X %08X %08X", i, (0x20 <= i && i < 0x80) ? i : 0x20, HashTable.ItemArray[i].ItemIndex, HashTable.ItemArray[i].NextIndex, FragOffs);

                if(FragOffs != 0x00800000)
                {
                    if((FragOffs & 0xFFFFFF00) == 0xFFFFFF00)
                        fprintf(fp, " '%c'", (char)(FragOffs & 0xFF));
                    else
                        fprintf(fp, " %s", PathFragmentTable.PathFragments.ItemArray + FragOffs);
                }
                fprintf(fp, "\n");
            }

            fclose(fp);
        }
*/
        // Calculate the item hash from the current char and fragment ID
        TableIndex = pStruct40->CalcHashValue(pSearch->szSearchMask) & HashTableMask;
        pHashEntry = HashTable.ItemArray + TableIndex;

        // Does the hash value ID match?
        if(pStruct40->NodeIndex == pHashEntry->NodeIndex)
        {
            // If the higher 24 bits are set, then the fragment is just one letter,
            // contained directly in the table.
            if(!IsPathFragmentSingleChar(pHashEntry))
            {
                // HOTS: 1958B59
                if (pChildDB != NULL)
                {
                    if (!pChildDB->CompareAndCopyPathFragmentByIndex(pSearch, pHashEntry->ChildTableIndex))
                        return false;
                }
                else
                {
                    if (!PathFragmentTable.CompareAndCopyPathFragment(pSearch, pHashEntry->FragmentOffset))
                        return false;
                }
            }
            else
            {
                // HOTS: 1958B88
                pStruct40->PathBuffer.Insert(pHashEntry->SingleChar);
                pStruct40->PathLength++;
            }

            // HOTS: 1958BCA
            pStruct40->NodeIndex = pHashEntry->NextIndex;
            return true;
        }

        // HOTS: 1958BE5
        ColTableIndex = CollisionTable.sub_1959CB0(pStruct40->NodeIndex) + 1;
        pStruct40->NodeIndex = (ColTableIndex - pStruct40->NodeIndex - 1);
        HiBitsIndex = CASC_INVALID_INDEX;

        // Keep searching while we have a valid collision table entry
        while(CollisionTable.IsItemPresent(ColTableIndex))
        {
            // If we have high bits in the the bit at NodeIndex is set, it means that there is fragment offset
            // If not, the byte in LoBitsTable is the character
            if(IsPathFragmentString(pStruct40->NodeIndex))
            {
                DWORD FragmentOffset = GetPathFragmentOffset2(HiBitsIndex, pStruct40->NodeIndex);
                DWORD SavePathLength = pStruct40->PathLength;   // HOTS: 1958C62

                // Do we have a child database?
                if(pChildDB != NULL)
                {
                    // HOTS: 1958CCB
                    if(pChildDB->CompareAndCopyPathFragmentByIndex(pSearch, FragmentOffset))
                        return true;
                }
                else
                {
                    // HOTS: 1958CD6
                    if(PathFragmentTable.CompareAndCopyPathFragment(pSearch, FragmentOffset))
                        return true;
                }

                // HOTS: 1958CED
                if(SavePathLength != pStruct40->PathLength)
                    return false;
            }
            else
            {
                // HOTS: 1958CFB
                if(LoBitsTable.ItemArray[pStruct40->NodeIndex] == pSearch->szSearchMask[pStruct40->PathLength])
                {
                    // HOTS: 1958D11
                    pStruct40->PathBuffer.Insert(LoBitsTable.ItemArray[pStruct40->NodeIndex]);
                    pStruct40->PathLength++;
                    return true;
                }
            }

            // HOTS: 1958D11
            pStruct40->NodeIndex++;
            ColTableIndex++;
        }

        return false;
    }

    // HOTS: 1959010
    bool CompareAndCopyPathFragmentByIndex(TMndxSearch * pSearch, DWORD TableIndex)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;
        PHASH_ENTRY pHashEntry;

        // HOTS: 1959024
        for(;;)
        {
            pHashEntry = HashTable.ItemArray + (TableIndex & HashTableMask);
            if(TableIndex == pHashEntry->NextIndex)
            {
                // HOTS: 1959047
                if(!IsPathFragmentSingleChar(pHashEntry))
                {
                    // HOTS: 195905A
                    if(pChildDB != NULL)
                    {
                        if(!pChildDB->CompareAndCopyPathFragmentByIndex(pSearch, pHashEntry->ChildTableIndex))
                            return false;
                    }
                    else
                    {
                        if(!PathFragmentTable.CompareAndCopyPathFragment(pSearch, pHashEntry->FragmentOffset))
                            return false;
                    }
                }
                else
                {
                    // HOTS: 1959092
                    if(pHashEntry->SingleChar != pSearch->szSearchMask[pStruct40->PathLength])
                        return false;

                    // Insert the low 8 bits to the path being built
                    pStruct40->PathBuffer.Insert(pHashEntry->SingleChar);
                    pStruct40->PathLength++;
                }

                // HOTS: 195912E
                TableIndex = pHashEntry->NodeIndex;
                if(TableIndex == 0)
                    return true;
            }
            else
            {
                // HOTS: 1959147
                if(IsPathFragmentString(TableIndex))
                {
                    // HOTS: 195917C
                    DWORD FragmentOffset = GetPathFragmentOffset1(TableIndex);

                    if(pChildDB != NULL)
                    {
                        if(!pChildDB->CompareAndCopyPathFragmentByIndex(pSearch, FragmentOffset))
                            return false;
                    }
                    else
                    {
                        if(!PathFragmentTable.CompareAndCopyPathFragment(pSearch, FragmentOffset))
                            return false;
                    }
                }
                else
                {
                    // HOTS: 195920E
                    if(LoBitsTable.ItemArray[TableIndex] != pSearch->szSearchMask[pStruct40->PathLength])
                        return false;

                    // Insert one character to the path being built
                    pStruct40->PathBuffer.Insert(LoBitsTable.ItemArray[TableIndex]);
                    pStruct40->PathLength++;
                }

                // HOTS: 19592B6
                if(TableIndex <= field_214)
                    return true;

                TableIndex = 0xFFFFFFFF - TableIndex + CollisionTable.sub_1959F50(TableIndex);
            }

            // HOTS: 19592D5
            if(pStruct40->PathLength >= pSearch->cchSearchMask)
                break;
        }

        CopyPathFragmentByIndex(pSearch, TableIndex);
        return true;
    }

    // HOTS: 1959460
    bool DoSearch(TMndxSearch * pSearch)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;
        TPathStop * pPathStop;
        DWORD edi;

        // Perform action based on the search phase
        switch (pStruct40->SearchPhase)
        {
            case MNDX_SEARCH_INITIALIZING:
            {
                // HOTS: 1959489
                pStruct40->BeginSearch();

                // If the caller passed a part of the search path, we need to find that one
                while (pStruct40->PathLength < pSearch->cchSearchMask)
                {
                    if (!CompareAndCopyPathFragment(pSearch))
                    {
                        pStruct40->SearchPhase = MNDX_SEARCH_FINISHED;
                        return false;
                    }
                }

                // HOTS: 19594b0
                TPathStop PathStop(pStruct40->NodeIndex, 0, pStruct40->PathBuffer.ItemCount);
                pStruct40->PathStops.Insert(PathStop);
                pStruct40->ItemCount = 1;

                if (FileNameIndexes.IsItemPresent(pStruct40->NodeIndex))
                {
                    pSearch->szFoundPath = pStruct40->PathBuffer.ItemArray;
                    pSearch->cchFoundPath = pStruct40->PathBuffer.ItemCount;
                    pSearch->FileNameIndex = FileNameIndexes.GetIntValueAt(pStruct40->NodeIndex);
                    return true;
                }
            }
            // No break here, go straight to the MNDX_SEARCH_SEARCHING

            case MNDX_SEARCH_SEARCHING:
            {
                // HOTS: 1959522
                for (;;)
                {
                    // HOTS: 1959530
                    if (pStruct40->ItemCount == pStruct40->PathStops.ItemCount)
                    {
                        TPathStop * pLastStop;
                        DWORD ColTableIndex;

                        pLastStop = pStruct40->PathStops.ItemArray + pStruct40->PathStops.ItemCount - 1;
                        ColTableIndex = CollisionTable.sub_1959CB0(pLastStop->LoBitsIndex) + 1;

                        // Insert a new structure
                        TPathStop PathStop(ColTableIndex - pLastStop->LoBitsIndex - 1, ColTableIndex, 0);
                        pStruct40->PathStops.Insert(PathStop);
                    }

                    // HOTS: 19595BD
                    pPathStop = pStruct40->PathStops.ItemArray + pStruct40->ItemCount;

                    // HOTS: 19595CC
                    if (CollisionTable.IsItemPresent(pPathStop->field_4++))
                    {
                        // HOTS: 19595F2
                        pStruct40->ItemCount++;

                        if (IsPathFragmentString(pPathStop->LoBitsIndex))
                        {
                            DWORD FragmentOffset = GetPathFragmentOffset2(pPathStop->HiBitsIndex_PathFragment, pPathStop->LoBitsIndex);

                            // HOTS: 1959630
                            if (pChildDB != NULL)
                            {
                                // HOTS: 1959649
                                pChildDB->CopyPathFragmentByIndex(pSearch, FragmentOffset);
                            }
                            else
                            {
                                // HOTS: 1959654
                                PathFragmentTable.CopyPathFragment(pSearch, FragmentOffset);
                            }
                        }
                        else
                        {
                            // HOTS: 1959665
                            // Insert one character to the path being built
                            pStruct40->PathBuffer.Insert(LoBitsTable.ItemArray[pPathStop->LoBitsIndex]);
                        }

                        // HOTS: 19596AE
                        pPathStop->Count = pStruct40->PathBuffer.ItemCount;

                        // HOTS: 19596b6
                        if (FileNameIndexes.IsItemPresent(pPathStop->LoBitsIndex))
                        {
                            // HOTS: 19596D1
                            if (pPathStop->field_10 == 0xFFFFFFFF)
                            {
                                // HOTS: 19596D9
                                pPathStop->field_10 = FileNameIndexes.GetIntValueAt(pPathStop->LoBitsIndex);
                            }
                            else
                            {
                                pPathStop->field_10++;
                            }

                            // HOTS: 1959755
                            pSearch->szFoundPath = pStruct40->PathBuffer.ItemArray;
                            pSearch->cchFoundPath = pStruct40->PathBuffer.ItemCount;
                            pSearch->FileNameIndex = pPathStop->field_10;
                            return true;
                        }
                    }
                    else
                    {
                        // HOTS: 19596E9
                        if (pStruct40->ItemCount == 1)
                        {
                            pStruct40->SearchPhase = MNDX_SEARCH_FINISHED;
                            return false;
                        }

                        // HOTS: 19596F5
                        pPathStop = pStruct40->PathStops.ItemArray + pStruct40->ItemCount - 1;
                        pPathStop->LoBitsIndex++;

                        pPathStop = pStruct40->PathStops.ItemArray + pStruct40->ItemCount - 2;

                        edi = pPathStop->Count;
                        pStruct40->PathBuffer.SetMaxItemsIf(edi);

                        // HOTS: 1959749
                        pStruct40->PathBuffer.ItemCount = edi;
                        pStruct40->ItemCount--;
                    }
                }
            }

            case MNDX_SEARCH_FINISHED:
                break;
        }

        return false;
    }

    // HOTS: 1957EF0
    bool FindFileInDatabase(TMndxSearch * pSearch)
    {
        TStruct40 * pStruct40 = pSearch->pStruct40;

        pStruct40->NodeIndex = 0;
        pStruct40->PathLength = 0;
        pStruct40->SearchPhase = MNDX_SEARCH_INITIALIZING;

        if(pSearch->cchSearchMask > 0)
        {
            while(pStruct40->PathLength < pSearch->cchSearchMask)
            {
                // HOTS: 01957F12
                if(!ComparePathFragment(pSearch))
                    return false;
            }
        }

        // HOTS: 1957F26
        if(!FileNameIndexes.IsItemPresent(pStruct40->NodeIndex))
            return false;

        pSearch->szFoundPath   = pSearch->szSearchMask;
        pSearch->cchFoundPath  = pSearch->cchSearchMask;
        pSearch->FileNameIndex = FileNameIndexes.GetIntValueAt(pStruct40->NodeIndex);
        return true;
    }

    // HOTS: 1959790
    int LoadFromStream(TByteStream & InStream)
    {
        DWORD dwBitMask;
        int nError;

        nError = CollisionTable.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = FileNameIndexes.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = CollisionHiBitsIndexes.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        // HOTS: 019597CD
        nError = LoBitsTable.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = HiBitsTable.LoadBitsFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        // HOTS: 019597F5
        nError = PathFragmentTable.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        // HOTS: 0195980A
        if(CollisionHiBitsIndexes.ValidItemCount != 0 && PathFragmentTable.PathFragments.ItemCount == 0)
        {
            TFileNameDatabase * pNewDB;

            pNewDB = new TFileNameDatabase;
            if (pNewDB == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            nError = SetChildDatabase(pNewDB);
            if(nError != ERROR_SUCCESS)
                return nError;

            nError = pChildDB->LoadFromStream(InStream);
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        // HOTS: 0195986B
        nError = HashTable.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        HashTableMask = HashTable.ItemCount - 1;

        nError = InStream.GetValue<DWORD>(field_214);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = InStream.GetValue<DWORD>(dwBitMask);
        if(nError != ERROR_SUCCESS)
            return nError;

        return Struct10.sub_1957800(dwBitMask);
    }

    TSparseArray CollisionTable;                // Table of valid collisions, indexed by NodeIndex
    TSparseArray FileNameIndexes;               // Array of file name indexes
    TSparseArray CollisionHiBitsIndexes;        // Table of indexes of high bits (above 8 bits) for collisions 

    // This pair of arrays serves for fast conversion from node index to FragmentOffset / FragmentChar
    TGenericArray<BYTE> LoBitsTable;            // Array of lower 8 bits of name fragment offset
    TBitEntryArray      HiBitsTable;            // Array of upper x bits of name fragment offset

    TPathFragmentTable PathFragmentTable;
    TFileNameDatabase * pChildDB;

    TGenericArray<HASH_ENTRY> HashTable;         // Hash table for searching name fragments

    DWORD HashTableMask;                        // Mask to get hash table index from hash value
    DWORD field_214;
    TStruct10 Struct10;
};

//-----------------------------------------------------------------------------
// Local functions - MAR file

class TMndxMarFile
{
    public:

    TMndxMarFile()
    {
        pDatabase = NULL;
        pbMarData = NULL;
        cbMarData = 0;
    }

    ~TMndxMarFile()
    {
        if(pDatabase != NULL)
            delete pDatabase;
        if(pbMarData != NULL)
            CASC_FREE(pbMarData);
    }

    // HOTS: 00E94180
    int LoadRootData(FILE_MAR_INFO & MarInfo, LPBYTE pbRootFile, LPBYTE pbRootEnd)
    {
        // Allocate the MAR data
        pbMarData = CASC_ALLOC(BYTE, MarInfo.MarDataSize);
        cbMarData = MarInfo.MarDataSize;
        if(pbMarData == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Capture the MAR data
        if(!CaptureData(pbRootFile + MarInfo.MarDataOffset, pbRootEnd, pbMarData, cbMarData))
            return ERROR_FILE_CORRUPT;

        // Create the file name database
        pDatabase = new TFileNameDatabase();
        if(pDatabase == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        return pDatabase->CreateDatabase(pbMarData, cbMarData);
    }

    // HOTS: 1956C60
    int SearchFile(TMndxSearch * pSearch)
    {
        int nError = ERROR_SUCCESS;

        if(pDatabase == NULL)
            return ERROR_INVALID_PARAMETER;

        nError = pSearch->CreateStruct40();
        if(nError != ERROR_SUCCESS)
            return nError;

        if(!pDatabase->FindFileInDatabase(pSearch))
            nError = ERROR_FILE_NOT_FOUND;

        pSearch->FreeStruct40();
        return nError;
    }

    // HOTS: 1956CE0
    int DoSearch(TMndxSearch * pSearch, bool * pbFindResult)
    {
        int nError = ERROR_SUCCESS;

        if(pDatabase == NULL)
            return ERROR_INVALID_PARAMETER;

        // Create the pStruct40, if not initialized yet
        if(pSearch->pStruct40 == NULL)
        {
            nError = pSearch->CreateStruct40();
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        *pbFindResult = pDatabase->DoSearch(pSearch);
        return nError;
    }

    // HOTS: 1956D20
    int GetFileNameCount(PDWORD PtrFileNameCount)
    {
        if(pDatabase == NULL)
            return ERROR_INVALID_PARAMETER;

        PtrFileNameCount[0] = pDatabase->FileNameIndexes.ValidItemCount;
        return ERROR_SUCCESS;
    }

//  protected:

    TFileNameDatabase * pDatabase;
    LPBYTE pbMarData;
    size_t cbMarData;
};

//-----------------------------------------------------------------------------
// Implementation of root file functions

typedef struct _FILE_MNDX_INFO
{
    BYTE  RootFileName[MD5_HASH_SIZE];              // Name (aka MD5) of the root file
    DWORD HeaderVersion;                            // Must be <= 2
    DWORD FormatVersion;
    DWORD field_1C;
    DWORD field_20;
    DWORD MarInfoOffset;                            // Offset of the first MAR entry info
    DWORD MarInfoCount;                             // Number of the MAR info entries
    DWORD MarInfoSize;                              // Size of the MAR info entry
    DWORD MndxEntriesOffset;
    DWORD MndxEntriesTotal;                         // Total number of MNDX root entries
    DWORD MndxEntriesValid;                         // Number of valid MNDX root entries
    DWORD MndxEntrySize;                            // Size of one MNDX root entry
    TMndxMarFile * MarFiles[MAX_MAR_FILES];         // File name list for the packages
//  PMNDX_ROOT_ENTRY pMndxEntries;
//  PMNDX_ROOT_ENTRY * ppValidEntries;
    bool bRootFileLoaded;                           // true if the root info file was properly loaded

} FILE_MNDX_INFO, *PFILE_MNDX_INFO;

struct TRootHandler_MNDX : public TRootHandler
{
    public:

    //
    //  Constructor and destructor
    //

    TRootHandler_MNDX()
    {
        memset(&MndxInfo, 0, sizeof(TRootHandler_MNDX) - FIELD_OFFSET(TRootHandler_MNDX, MndxInfo));
        dwRootFlags |= ROOT_FLAG_HAS_NAMES;
    }

    ~TRootHandler_MNDX()
    {
        PMNDX_PACKAGE pPackage;
        size_t i;

        for(i = 0; i < MAX_MAR_FILES; i++)
            delete MndxInfo.MarFiles[i];
        if(ppValidEntries != NULL)
            CASC_FREE(ppValidEntries);
        if(pMndxEntries != NULL)
            CASC_FREE(pMndxEntries);

        for(i = 0; i < Packages.ItemCount(); i++)
        {
            pPackage = (PMNDX_PACKAGE)Packages.ItemAt(i);
            CASC_FREE(pPackage->szFileName);
        }
        Packages.Free();
    }

    //
    //  Helper functions
    //

    static LPBYTE CaptureRootHeader(FILE_MNDX_HEADER & MndxHeader, LPBYTE pbRootPtr, LPBYTE pbRootEnd)
    {
        // Capture the root header
        pbRootPtr = CaptureData(pbRootPtr, pbRootEnd, &MndxHeader, sizeof(FILE_MNDX_HEADER));
        if(pbRootPtr == NULL)
            return NULL;

        // Check signature and version
        if(MndxHeader.Signature != CASC_MNDX_ROOT_SIGNATURE || MndxHeader.FormatVersion > 2 || MndxHeader.FormatVersion < 1)
            return NULL;

        // Passed
        return pbRootPtr + sizeof(FILE_MNDX_HEADER);
    }

    PMNDX_PACKAGE FindMndxPackage(const char * szFileName)
    {
        PMNDX_PACKAGE pMatching = NULL;
        size_t nMaxLength = 0;
        size_t nLength = strlen(szFileName);

        // Packages must be loaded
        assert(Packages.ItemCount() != 0);

        //FILE * fp = fopen("E:\\packages.txt", "wt");
        //for(size_t i = 0; i < hs->pPackages->NameEntries; i++, pPackage++)
        //{
        //    if(pPackage->szFileName != NULL)
        //        fprintf(fp, "%s\n", pPackage->szFileName);
        //}
        //fclose(fp);

        // Find the longest matching name
        for(size_t i = 0; i < Packages.ItemCount(); i++)
        {
            PMNDX_PACKAGE pPackage = (PMNDX_PACKAGE)Packages.ItemAt(i);

            if(pPackage->nLength < nLength && pPackage->nLength > nMaxLength)
            {
                // Compare the package name
                if(!strncmp(szFileName, pPackage->szFileName, pPackage->nLength))
                {
                    nMaxLength = pPackage->nLength;
                    pMatching = pPackage;
                }
            }
        }

        // Give the package pointer or NULL if not found
        return pMatching;
    }

    int SearchMndxInfo(const char * szFileName, DWORD dwPackage, PMNDX_ROOT_ENTRY * ppRootEntry)
    {
        PMNDX_ROOT_ENTRY pRootEntry;
        TMndxSearch Search;

        // Search the database for the file name
        if(MndxInfo.bRootFileLoaded)
        {
            Search.SetSearchMask(szFileName, strlen(szFileName));

            // Search the file name in the second MAR info (the one with stripped package names)
            if(MndxInfo.MarFiles[1]->SearchFile(&Search) != ERROR_SUCCESS)
                return ERROR_FILE_NOT_FOUND;

            // The found MNDX index must fall into range of valid MNDX entries
            if(Search.FileNameIndex < MndxInfo.MndxEntriesValid)
            {
                // HOTS: E945F4
                pRootEntry = ppValidEntries[Search.FileNameIndex];
                while((pRootEntry->Flags & 0x00FFFFFF) != dwPackage)
                {
                    // The highest bit serves as a terminator if set
                    if(pRootEntry->Flags & 0x80000000)
                        return ERROR_FILE_NOT_FOUND;

                    pRootEntry++;
                }

                // Give the root entry pointer to the caller
                if(ppRootEntry != NULL)
                    ppRootEntry[0] = pRootEntry;
                return ERROR_SUCCESS;
            }
        }

        return ERROR_FILE_NOT_FOUND;
    }

    LPBYTE FillFindData(TCascSearch * hs, TMndxSearch * pSearch)
    {
        PMNDX_ROOT_ENTRY pRootEntry = NULL;
        PMNDX_PACKAGE pPackage;
        char * szStrippedPtr;
        char szStrippedName[MAX_PATH+1];
        int nError;

        // Sanity check
        assert(pSearch->cchFoundPath < MAX_PATH);
        CASCLIB_UNUSED(pSearch);

        // Fill the file name
        memcpy(hs->szFileName, pSearch->szFoundPath, pSearch->cchFoundPath);
        hs->szFileName[pSearch->cchFoundPath] = 0;

        // Fill the file size
        pPackage = FindMndxPackage(hs->szFileName);
        if(pPackage == NULL)
            return NULL;

        // Cut the package name off the full path
        szStrippedPtr = hs->szFileName + pPackage->nLength;
        while(szStrippedPtr[0] == '/')
            szStrippedPtr++;

        // We need to convert the stripped name to lowercase, replacing backslashes with slashes
        NormalizeFileName_LowerSlash(szStrippedName, szStrippedPtr, MAX_PATH);

        // Search the package
        nError = SearchMndxInfo(szStrippedName, pPackage->nIndex, &pRootEntry);
        if(nError != ERROR_SUCCESS)
            return NULL;

        // Give the file size
        hs->dwFileSize = pRootEntry->ContentSize;
        return pRootEntry->CKey;
    }

    int LoadPackageNames()
    {
        TMndxSearch Search;
        int nError;

        // Prepare the file name search in the top level directory
        Search.SetSearchMask("", 0);

#ifdef _DEBUG
//      Search.SetSearchMask("mods/heroes.stormmod/base.stormmaps/maps/heroes/builtin/startingexperience/practicemode01.stormmap/dede.stormdata", 113);
#endif
        // Allocate initial name list structure
        nError = Packages.Create<MNDX_PACKAGE>(0x40);
        if(nError != ERROR_SUCCESS)
            return nError;

        // Keep searching as long as we find something
        for(;;)
        {
            PMNDX_PACKAGE pPackage;
            char * szFileName;
            bool bFindResult = false;

            // Search the next file name
            MndxInfo.MarFiles[0]->DoSearch(&Search, &bFindResult);
            if(bFindResult == false)
                break;

            // Create file name
            szFileName = CASC_ALLOC(char, Search.cchFoundPath + 1);
            if(szFileName == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            // Insert the found name to the top level directory list
            pPackage = (PMNDX_PACKAGE)Packages.Insert(NULL, 1);
            if(pPackage == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            // Fill the file name
            memcpy(szFileName, Search.szFoundPath, Search.cchFoundPath);
            szFileName[Search.cchFoundPath] = 0;

            // Fill the package structure
            pPackage->szFileName = szFileName;
            pPackage->nLength = Search.cchFoundPath;
            pPackage->nIndex = Search.FileNameIndex;
        }

        // Give the packages to the caller
        return ERROR_SUCCESS;
    }

    int Load(FILE_MNDX_HEADER MndxHeader, LPBYTE pbRootFile, LPBYTE pbRootEnd)
    {
        TMndxMarFile * pMarFile;
        FILE_MAR_INFO MarInfo;
        size_t cbToAllocate;
        DWORD dwFilePointer = 0;
        DWORD i;
        int nError = ERROR_SUCCESS;

        // Copy the header into the MNDX info
        MndxInfo.HeaderVersion = MndxHeader.HeaderVersion;
        MndxInfo.FormatVersion = MndxHeader.FormatVersion;
        dwFilePointer += sizeof(FILE_MNDX_HEADER);

        // Header version 2 has 2 extra fields that we need to load
        if(MndxInfo.HeaderVersion == 2)
        {
            if(!CaptureData(pbRootFile + dwFilePointer, pbRootEnd, &MndxInfo.field_1C, sizeof(DWORD) + sizeof(DWORD)))
                return ERROR_FILE_CORRUPT;
            dwFilePointer += sizeof(DWORD) + sizeof(DWORD);
        }

        // Load the rest of the file header
        if(!CaptureData(pbRootFile + dwFilePointer, pbRootEnd, &MndxInfo.MarInfoOffset, 0x1C))
            return ERROR_FILE_CORRUPT;

        // Verify the structure
        if(MndxInfo.MarInfoCount > MAX_MAR_FILES || MndxInfo.MarInfoSize != sizeof(FILE_MAR_INFO))
            return ERROR_FILE_CORRUPT;

        // Load all MAR infos
        for(i = 0; i < MndxInfo.MarInfoCount; i++)
        {
            // Capture the n-th MAR info
            dwFilePointer = MndxInfo.MarInfoOffset + (MndxInfo.MarInfoSize * i);
            if(!CaptureData(pbRootFile + dwFilePointer, pbRootEnd, &MarInfo, sizeof(FILE_MAR_INFO)))
                return ERROR_FILE_CORRUPT;

            // Allocate MAR_FILE structure
            pMarFile = new TMndxMarFile();
            if(pMarFile == NULL)
            {
                nError = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }

            // Create the database from the MAR data
            nError = pMarFile->LoadRootData(MarInfo, pbRootFile, pbRootEnd);
            if(nError != ERROR_SUCCESS)
                break;

            // Assign the MAR file to the MNDX info structure
            MndxInfo.MarFiles[i] = pMarFile;
        }

        // All three MAR files must be loaded
        // HOTS: 00E9503B
        if(nError == ERROR_SUCCESS)
        {
            if(MndxInfo.MarFiles[0] == NULL || MndxInfo.MarFiles[1] == NULL || MndxInfo.MarFiles[2] == NULL)
                nError = ERROR_BAD_FORMAT;
            if(MndxInfo.MndxEntrySize != sizeof(MNDX_ROOT_ENTRY))
                nError = ERROR_BAD_FORMAT;
        }

        // Load the complete array of MNDX entries
        if(nError == ERROR_SUCCESS)
        {
            pMarFile = MndxInfo.MarFiles[1];
            DWORD FileNameCount;

            nError = pMarFile->GetFileNameCount(&FileNameCount);
            if(nError == ERROR_SUCCESS && FileNameCount == MndxInfo.MndxEntriesValid)
            {
                cbToAllocate = MndxInfo.MndxEntriesTotal * MndxInfo.MndxEntrySize;
                pMndxEntries = (PMNDX_ROOT_ENTRY)CASC_ALLOC(BYTE, cbToAllocate);
                if(pMndxEntries != NULL)
                {
                    if(!CaptureData(pbRootFile + MndxInfo.MndxEntriesOffset, pbRootEnd, pMndxEntries, cbToAllocate))
                        nError = ERROR_FILE_CORRUPT;
                }
                else
                    nError = ERROR_NOT_ENOUGH_MEMORY;
            }
            else
                nError = ERROR_FILE_CORRUPT;
        }

        // Pick the valid MNDX entries and put them to a separate array
        if(nError == ERROR_SUCCESS)
        {
            assert(MndxInfo.MndxEntriesValid <= MndxInfo.MndxEntriesTotal);
            ppValidEntries = CASC_ALLOC(PMNDX_ROOT_ENTRY, MndxInfo.MndxEntriesValid + 1);
            if(ppValidEntries != NULL)
            {
                PMNDX_ROOT_ENTRY pRootEntry = pMndxEntries;
                DWORD ValidEntryCount = 1; // edx
                DWORD nIndex1 = 0;

                // The first entry is always valid
                ppValidEntries[nIndex1++] = pRootEntry;

                // Put the remaining entries
                for(i = 0; i < MndxInfo.MndxEntriesTotal; i++, pRootEntry++)
                {
                    if (ValidEntryCount > MndxInfo.MndxEntriesValid)
                        break;

                    if (pRootEntry->Flags & 0x80000000)
                    {
                        ppValidEntries[nIndex1++] = pRootEntry + 1;
                        ValidEntryCount++;
                    }
                }

                // Verify the final number of valid entries
                if ((ValidEntryCount - 1) != MndxInfo.MndxEntriesValid)
                    nError = ERROR_BAD_FORMAT;
            }
            else
                nError = ERROR_NOT_ENOUGH_MEMORY;
        }

        // Load the MNDX packages
        if(nError == ERROR_SUCCESS)
        {
            nError = LoadPackageNames();
            MndxInfo.bRootFileLoaded = (nError == ERROR_SUCCESS);
        }

        return nError;
    }

    //
    //  Virtual root functions
    //

    LPBYTE Search(TCascSearch * hs)
    {
        TMndxMarFile * pMarFile = MndxInfo.MarFiles[2];
        TMndxSearch * pSearch = NULL;
        bool bFindResult = false;

        // If the first time, allocate the structure for the search result
        if(hs->pRootContext == NULL)
        {
            // Create the new search structure
            pSearch = new TMndxSearch;
            if(pSearch == NULL)
                return NULL;

            // Setup the search mask
            pSearch->SetSearchMask("", 0);
            hs->pRootContext = pSearch;
        }

        // Make shortcut for the search structure
        assert(hs->pRootContext != NULL);
        pSearch = (TMndxSearch *)hs->pRootContext;

        // Keep searching
        for(;;)
        {
            // Search the next file name
            pMarFile->DoSearch(pSearch, &bFindResult);
            if (bFindResult == false)
                return NULL;

            // If we have no wild mask, we found it
            if (hs->szMask == NULL || hs->szMask[0] == 0)
                break;

            // Copy the found name to the buffer
            memcpy(hs->szFileName, pSearch->szFoundPath, pSearch->cchFoundPath);
            hs->szFileName[pSearch->cchFoundPath] = 0;
            if (CheckWildCard(hs->szFileName, hs->szMask))
                break;
        }

        // Give the file size and CKey
        return FillFindData(hs, pSearch);
    }

    void EndSearch(TCascSearch * pSearch)
    {
        if(pSearch != NULL)
        {
            delete (TMndxSearch *)pSearch->pRootContext;
            pSearch->pRootContext = NULL;
        }
    }

    LPBYTE GetKey(const char * szFileName, PDWORD /* PtrFileSize */)
    {
        PMNDX_ROOT_ENTRY pRootEntry = NULL;
        PMNDX_PACKAGE pPackage;
        char * szStrippedName;
        char szNormName[MAX_PATH+1];
        int nError;

        // Convert the file name to lowercase + slashes
        NormalizeFileName_LowerSlash(szNormName, szFileName, MAX_PATH);

        // Find the package number
        pPackage = FindMndxPackage(szNormName);
        if(pPackage == NULL)
            return NULL;

        // Cut the package name off the full path
        szStrippedName = szNormName + pPackage->nLength;
        while(szStrippedName[0] == '/')
            szStrippedName++;

        // Find the root entry
        nError = SearchMndxInfo(szStrippedName, (DWORD)(pPackage->nIndex), &pRootEntry);
        if(nError != ERROR_SUCCESS || pRootEntry == NULL)
            return NULL;

        // Return the CKey
        return pRootEntry->CKey;
    }

    protected:

    FILE_MNDX_INFO MndxInfo;

    PMNDX_ROOT_ENTRY * ppValidEntries;
    PMNDX_ROOT_ENTRY pMndxEntries;
    CASC_ARRAY Packages;                        // Linear list of present packages
};

//-----------------------------------------------------------------------------
// Public functions - MNDX info

int RootHandler_CreateMNDX(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_MNDX * pRootHandler = NULL;
    FILE_MNDX_HEADER MndxHeader;
    LPBYTE pbRootEnd = pbRootFile + cbRootFile;
    int nError = ERROR_BAD_FORMAT;

    // Verify the header of the ROOT file
    if(TRootHandler_MNDX::CaptureRootHeader(MndxHeader, pbRootFile, pbRootEnd) != NULL)
    {
        // Allocate the root handler object
        pRootHandler = new TRootHandler_MNDX();
        if(pRootHandler != NULL)
        {
            // Load the root directory. If load failed, we free the object
            nError = pRootHandler->Load(MndxHeader, pbRootFile, pbRootEnd);
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
