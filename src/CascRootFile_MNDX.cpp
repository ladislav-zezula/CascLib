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

typedef struct _TRIPLET
{
    DWORD BaseValue;
    DWORD Value2;
    DWORD Value3;
} TRIPLET, *PTRIPLET;

typedef struct _NAME_FRAG
{
    DWORD ItemIndex;                                // Back index to various tables
    DWORD NextIndex;                                // The following item index
    DWORD FragOffs;                                 // Higher 24 bits are 0xFFFFFF00 --> A single matching character
                                                    // Otherwise --> Offset to the name fragment table
} NAME_FRAG, *PNAME_FRAG;

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
// The PATH_STOP structure

struct PATH_STOP
{
    PATH_STOP()
    {
        LoBitsIndex = 0;
        field_4 = 0;
        Count = 0;
        field_C = 0xFFFFFFFF;
        field_10 = 0xFFFFFFFF;
    }

    PATH_STOP(DWORD arg_0, DWORD arg_4, DWORD arg_8)
    {
        LoBitsIndex = arg_0;
        field_4 = arg_4;
        Count = arg_8;
        field_C = 0xFFFFFFFF;
        field_10 = 0xFFFFFFFF;
    }

    DWORD LoBitsIndex;                              // Index of the 
    DWORD field_4;
    DWORD Count;
    DWORD field_C;
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
    // HOTS: 19571E0: <TRIPLET>
    // HOTS: 1957230: <BYTE>
    // HOTS: 1957280: <NAME_FRAG>
    template <typename T>
    int GetArray(T ** Pointer, size_t ItemCount)
    {
        // Verify parameters
        if(Pointer == NULL && ItemCount != 0)
            return ERROR_INVALID_PARAMETER;
        if(ItemCount > MNDX_MAX_ENTRIES(T))
            return ERROR_NOT_ENOUGH_MEMORY;

        // Allocate bytes for the array
        Pointer[0] = CASC_ALLOC(T, ItemCount);
        if(Pointer[0] == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Get the pointer to the array
        return CopyBytes(Pointer[0], sizeof(T) * ItemCount);
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
    // HOTS: 19570B0 (SetTripletsValid)
    // HOTS: 19570D0 (? SetBitsValid ?)
    // HOTS: 19570F0 (SetNameFragmentsValid)
    int SetArrayValid()
    {
        if(bIsValidArray != 0)
            return ERROR_ALREADY_EXISTS;

        bIsValidArray = true;
        return ERROR_SUCCESS;
    }

    // HOTS: 19575A0 (char)
    // HOTS: 1957600 (PATH_STOP)
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
    // HOTS: 1957600 (PATH_STOP)
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
    // HOTS: 1958330 <PATH_STOP>
    void Insert(T NewItem)
    {
        // Make sure we have enough capacity for the new item
        SetMaxItemsIf(ItemCount + 1);

        // Put the character to the slot that has been reserved
        ItemArray[ItemCount++] = NewItem;
    }

    // HOTS: 19583A0 <PATH_STOP>
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
    // HOTS: 19574E0 <TRIPLET>
    // HOTS: 1957690 <BYTE>
    // HOTS: 1957700 <NAME_FRAG>
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

    DWORD GetBitEntry(DWORD EntryIndex)
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

        nError = HaveBits.LoadFromStream(InStream);
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

    // Returns true if the item at n-th position is present
    DWORD IsItemPresent(DWORD ItemIndex)
    {
        return (HaveBits.ItemArray[ItemIndex >> 0x05] & (1 << (ItemIndex & 0x1F)));
    }

    // HOTS: 1959B60
    DWORD GetItemValue(DWORD ItemIndex)
    {
        PTRIPLET pTriplet;
        DWORD DwordIndex;
        DWORD BaseValue;
        DWORD BitMask;

        //
        // Divide the low-8-bits index to four parts:
        //
        // |-----------------------|---|------------|
        // |       A (23 bits)     | B |      C     |
        // |-----------------------|---|------------|
        //
        // A (23-bits): Index to the table (60 bits per entry)
        //
        //    Layout of the table entry:
        //   |--------------------------------|-------|--------|--------|---------|---------|---------|---------|-----|
        //   |  Base Value                    | val[0]| val[1] | val[2] | val[3]  | val[4]  | val[5]  | val[6]  |  -  |
        //   |  32 bits                       | 7 bits| 8 bits | 8 bits | 9 bits  | 9 bits  | 9 bits  | 9 bits  |5bits|
        //   |--------------------------------|-------|--------|--------|---------|---------|---------|---------|-----|
        //
        // B (3 bits) : Index of the variable-bit value in the array (val[#], see above)
        //
        // C (32 bits): Number of bits to be checked (up to 0x3F bits).
        //              Number of set bits is then added to the values obtained from A and B

        // Upper 23 bits contain index to the table
        pTriplet = BaseValues.ItemArray + (ItemIndex >> 0x09);
        BaseValue = pTriplet->BaseValue;

        // Next 3 bits contain the index to the VBR
        switch(((ItemIndex >> 0x06) & 0x07) - 1)
        {
            case 0:     // Add the 1st value (7 bits)
                BaseValue += (pTriplet->Value2 & 0x7F);
                break;

            case 1:     // Add the 2nd value (8 bits)
                BaseValue += (pTriplet->Value2 >> 0x07) & 0xFF;
                break;

            case 2:     // Add the 3rd value (8 bits)
                BaseValue += (pTriplet->Value2 >> 0x0F) & 0xFF;
                break;

            case 3:     // Add the 4th value (9 bits)
                BaseValue += (pTriplet->Value2 >> 0x17);
                break;

            case 4:     // Add the 5th value (9 bits)
                BaseValue += (pTriplet->Value3 & 0x1FF);
                break;

            case 5:     // Add the 6th value (9 bits)
                BaseValue += (pTriplet->Value3 >> 0x09) & 0x1FF;
                break;

            case 6:     // Add the 7th value (9 bits)
                BaseValue += (pTriplet->Value3 >> 0x12) & 0x1FF;
                break;
        }

        //
        // Take the upper 27 bits as an index to DWORD array, take lower 5 bits
        // as number of bits to mask. Then calculate number of set bits in the value
        // masked value.
        //

        // Get the index into the array of DWORDs
        DwordIndex = (ItemIndex >> 0x05);

        // Add number of set bits in the masked value up to 0x3F bits
        if(ItemIndex & 0x20)
            BaseValue += GetNumbrOfSetBits32(HaveBits.ItemArray[DwordIndex - 1]);

        BitMask = (1 << (ItemIndex & 0x1F)) - 1;
        return BaseValue + GetNumbrOfSetBits32(HaveBits.ItemArray[DwordIndex] & BitMask);
    }

    TGenericArray<DWORD> HaveBits;              // Bit array for each item (1 = item is present)
    DWORD TotalItemCount;                       // Total number of items in the array
    DWORD ValidItemCount;                       // Number of present items in the array
    TGenericArray<TRIPLET> BaseValues;          // Array of base values for item indexes >= 0x200
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
        ItemIndex   = 0;
        CharIndex   = 0;
        ItemCount   = 0;
        SearchPhase = MNDX_SEARCH_INITIALIZING;
    }

    // HOTS: 19586B0
    void InitSearchBuffers()
    {
        // HOTS: 19586BD
        PathBuffer.ItemCount = 0;
        PathBuffer.SetMaxItemsIf(0x40);

        // HOTS: 19586E1
        // Set the new item count
        PathStops.GrowArray(0);
        PathStops.SetMaxItemsIf(4);

        ItemIndex = 0;
        CharIndex = 0;
        ItemCount = 0;
        SearchPhase = MNDX_SEARCH_SEARCHING;
    }

    TGenericArray<char> PathBuffer;             // Buffer for building a file name
    TGenericArray<PATH_STOP> PathStops;         // Array of path checkpoints
    DWORD ItemIndex;                            // Current name fragment: Index to various tables
    DWORD CharIndex;
    DWORD ItemCount;
    DWORD SearchPhase;                          // 0 = initializing, 2 = searching, 4 = finished
};

//-----------------------------------------------------------------------------
// Local functions - TMndxFindResult

class TMndxFindResult
{
    public:

    // HOTS: 01956EE0
    TMndxFindResult()
    {
        szSearchMask = NULL;
        cchSearchMask = 0;
        field_8 = 0;
        szFoundPath = NULL;
        cchFoundPath = 0;
        FileNameIndex = 0;
        pStruct40 = NULL;
    }

    // HOTS: 01956F00
    ~TMndxFindResult()
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
    int SetSearchPath(
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
    DWORD field_8;
    const char * szFoundPath;           // Found path name
    size_t cchFoundPath;                // Length of the found path name
    DWORD FileNameIndex;                // Index of the file name
    TStruct40 * pStruct40;
};

//-----------------------------------------------------------------------------
// TNameIndexStruct interface / implementation

class TNameIndexStruct
{
    public:

    // HOTS: 0195A290
    TNameIndexStruct()
    {}

    // HOTS: inlined
    ~TNameIndexStruct()
    {}

    // HOTS: 195A180
    bool CheckNameFragment(TMndxFindResult * pStruct1C, DWORD dwFragOffs)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        const char * szPathFragment;
        const char * szSearchMask;

        if(!Struct68.TotalItemCount)
        {
            // Get the offset of the fragment to compare. For convenience with pStruct40->CharIndex,
            // subtract the CharIndex from the fragment offset
            szPathFragment = (NameFragments.ItemArray + dwFragOffs - pStruct40->CharIndex);
            szSearchMask = pStruct1C->szSearchMask;

            // Keep searching as long as the name matches with the fragment
            while(szPathFragment[pStruct40->CharIndex] == szSearchMask[pStruct40->CharIndex])
            {
                // Move to the next character
                pStruct40->CharIndex++;

                // Is it the end of the fragment or end of the path?
                if(szPathFragment[pStruct40->CharIndex] == 0)
                    return true;
                if(pStruct40->CharIndex >= pStruct1C->cchSearchMask)
                    return false;
            }

            return false;
        }
        else
        {
            // Get the offset of the fragment to compare.
            szPathFragment = NameFragments.ItemArray;
            szSearchMask = pStruct1C->szSearchMask;

            // Keep searching as long as the name matches with the fragment
            while(szPathFragment[dwFragOffs] == szSearchMask[pStruct40->CharIndex])
            {
                // Move to the next character
                pStruct40->CharIndex++;

                // Is it the end of the fragment or end of the path?
                if(Struct68.IsItemPresent(dwFragOffs++))
                    return true;
                if(dwFragOffs >= pStruct1C->cchSearchMask)
                    return false;
            }

            return false;
        }
    }

    // HOTS: 195A570
    bool CheckAndCopyNameFragment(TMndxFindResult * pStruct1C, DWORD dwFragOffs)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        const char * szPathFragment;
        const char * szSearchMask;

        if(!Struct68.TotalItemCount)
        {
            // Get the offset of the fragment to compare. For convenience with pStruct40->CharIndex,
            // subtract the CharIndex from the fragment offset
            szPathFragment = NameFragments.ItemArray + dwFragOffs - pStruct40->CharIndex;
            szSearchMask = pStruct1C->szSearchMask;

            // Keep copying as long as we don't reach the end of the search mask
            while(pStruct40->CharIndex < pStruct1C->cchSearchMask)
            {
                // HOTS: 195A5A0
                if(szPathFragment[pStruct40->CharIndex] != szSearchMask[pStruct40->CharIndex])
                    return false;

                // HOTS: 195A5B7
                pStruct40->PathBuffer.Insert(szPathFragment[pStruct40->CharIndex]);
                pStruct40->CharIndex++;

                if(szPathFragment[pStruct40->CharIndex] == 0)
                    return true;
            }

            // Fixup the address of the fragment
            szPathFragment += pStruct40->CharIndex;

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
            szPathFragment = NameFragments.ItemArray;
            szSearchMask = pStruct1C->szSearchMask;

            // Keep copying as long as we don't reach the end of the search mask
            while(dwFragOffs < pStruct1C->cchSearchMask)
            {
                if(szPathFragment[dwFragOffs] != szSearchMask[pStruct40->CharIndex])
                    return false;

                pStruct40->PathBuffer.Insert(szPathFragment[dwFragOffs]);
                pStruct40->CharIndex++;

                // Keep going as long as the given bit is not set
                if(Struct68.IsItemPresent(dwFragOffs++))
                    return true;
            }

            // Fixup the address of the fragment
            szPathFragment += dwFragOffs;

            // Now we need to copy the rest of the fragment
            while(Struct68.IsItemPresent(dwFragOffs++) == 0)
            {
                // HOTS: 195A7A6
                pStruct40->PathBuffer.Insert(szPathFragment[0]);
                szPathFragment++;
            }
        }

        return true;
    }

    // HOTS: 195A3F0
    void CopyNameFragment(TMndxFindResult * pStruct1C, DWORD dwFragOffs)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        const char * szPathFragment;

        // HOTS: 195A3FA
        if(!Struct68.TotalItemCount)
        {
            // HOTS: 195A40C
            szPathFragment = NameFragments.ItemArray + dwFragOffs;
            while(szPathFragment[0] != 0)
            {
                // Insert the character to the path being built
                pStruct40->PathBuffer.Insert(*szPathFragment++);
            }
        }
        else
        {
            // HOTS: 195A4B3
            for(;;)
            {
                // Insert the character to the path being built
                pStruct40->PathBuffer.Insert(NameFragments.ItemArray[dwFragOffs]);

                // Keep going as long as the given bit is not set
                if(Struct68.IsItemPresent(dwFragOffs++))
                    break;
            }
        }
    }

    // HOTS: 0195A820
    int LoadFromStream(TByteStream & InStream)
    {
        int nError;

        nError = NameFragments.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        return Struct68.LoadFromStream(InStream);
    }

    TGenericArray<char> NameFragments;
    TSparseArray Struct68;
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
        NameFragIndexMask = 0;
        field_214 = 0;
        pNextDB = NULL;
    }

    ~TFileNameDatabase()
    {
        delete pNextDB;
    }

    // Retrieves the name fragment distance
    // HOTS: 19573D0/inlined
    DWORD GetNameFragmentOffsetEx(DWORD LoBitsIndex, DWORD HiBitsIndex)
    {
        return (FrgmDist_HiBits.GetBitEntry(HiBitsIndex) << 0x08) | FrgmDist_LoBits.ItemArray[LoBitsIndex];
    }

    // HOTS: 1957350, inlined
    DWORD GetNameFragmentOffset(DWORD LoBitsIndex)
    {
        return GetNameFragmentOffsetEx(LoBitsIndex, Struct68_D0.GetItemValue(LoBitsIndex));
    }

    // Returns nonzero if the name fragment match is a single-char match
    bool IS_SINGLE_CHAR_MATCH(TGenericArray<NAME_FRAG> & Table, DWORD ItemIndex)
    {
        return ((Table.ItemArray[ItemIndex].FragOffs & 0xFFFFFF00) == 0xFFFFFF00);
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
    int SetNextDatabase(TFileNameDatabase * pNewDB)
    {
        if(pNewDB != NULL && pNextDB == pNewDB)
            return ERROR_INVALID_PARAMETER;

        if(pNextDB != NULL)
            delete pNextDB;
        pNextDB = pNewDB;
        return ERROR_SUCCESS;
    }

    // HOTS: 1959CB0
    DWORD sub_1959CB0(DWORD dwItemIndex)
    {
        PTRIPLET pTriplet;
        DWORD dwKeyShifted = (dwItemIndex >> 9);
        DWORD eax, ebx, ecx, edx, esi, edi;

        // If lower 9 is zero
        edx = dwItemIndex;
        if((edx & 0x1FF) == 0)
            return Struct68_00.ArrayDwords_38.ItemArray[dwKeyShifted];

        eax = Struct68_00.ArrayDwords_38.ItemArray[dwKeyShifted] >> 9;
        esi = (Struct68_00.ArrayDwords_38.ItemArray[dwKeyShifted + 1] + 0x1FF) >> 9;
        dwItemIndex = esi;

        if((eax + 0x0A) >= esi)
        {
            // HOTS: 1959CF7
            pTriplet = Struct68_00.BaseValues.ItemArray + eax + 1;
            edi = (eax << 0x09);
            ebx = edi - pTriplet->BaseValue + 0x200;
            while(edx >= ebx)
            {
                // HOTS: 1959D14
                edi += 0x200;
                pTriplet++;

                ebx = edi - pTriplet->BaseValue + 0x200;
                eax++;
            }
        }
        else
        {
            // HOTS: 1959D2E
            while((eax + 1) < esi)
            {
                // HOTS: 1959D38
                // ecx = Struct68_00.BaseValues.ItemArray;
                esi = (esi + eax) >> 1;
                ebx = (esi << 0x09) - Struct68_00.BaseValues.ItemArray[esi].BaseValue;
                if(edx < ebx)
                {
                    // HOTS: 01959D4B
                    dwItemIndex = esi;
                }
                else
                {
                    // HOTS: 1959D50
                    eax = esi;
                    esi = dwItemIndex;
                }
            }
        }

        // HOTS: 1959D5F
        pTriplet = Struct68_00.BaseValues.ItemArray + eax;
        edx += pTriplet->BaseValue - (eax << 0x09);
        edi = (eax << 4);

        eax = pTriplet->Value2;
        ecx = (eax >> 0x17);
        ebx = 0x100 - ecx;
        if(edx < ebx)
        {
            // HOTS: 1959D8C
            ecx = ((eax >> 0x07) & 0xFF);
            esi = 0x80 - ecx;
            if(edx < esi)
            {
                // HOTS: 01959DA2
                eax = eax & 0x7F;
                ecx = 0x40 - eax;
                if(edx >= ecx)
                {
                    // HOTS: 01959DB7
                    edi += 2;
                    edx = edx + eax - 0x40;
                }
            }
            else
            {
                // HOTS: 1959DC0
                eax = (eax >> 0x0F) & 0xFF;
                esi = 0xC0 - eax;
                if(edx < esi)
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
            esi = pTriplet->Value3;
            eax = ((esi >> 0x09) & 0x1FF);
            ebx = 0x180 - eax;
            if(edx < ebx)
            {
                // HOTS: 01959E00
                esi = esi & 0x1FF;
                eax = (0x140 - esi);
                if(edx < eax)
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
                esi = (esi >> 0x12) & 0x1FF;
                ecx = (0x1C0 - esi);
                if(edx < ecx)
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
        ecx = ~Struct68_00.HaveBits.ItemArray[edi];
        eax = GetNumberOfSetBits(ecx);
        esi = eax >> 0x18;

        if(edx >= esi)
        {
            // HOTS: 1959ea4
            ecx = ~Struct68_00.HaveBits.ItemArray[++edi];
            edx = edx - esi;
            eax = GetNumberOfSetBits(ecx);
        }

        // HOTS: 1959eea
        // ESI gets the number of set bits in the lower 16 bits of ECX
        esi = (eax >> 0x08) & 0xFF;
        edi = edi << 0x05;
        if(edx < esi)
        {
            // HOTS: 1959EFC
            eax = eax & 0xFF;
            if(edx >= eax)
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
            if(edx < eax)
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
        PTRIPLET pTriplet;
        PDWORD ItemArray;
        DWORD eax, ebx, ecx, edx, esi, edi;

        edx = arg_0;
        eax = arg_0 >> 0x09;
        if((arg_0 & 0x1FF) == 0)
            return Struct68_00.ArrayDwords_50.ItemArray[eax];

        ItemArray = Struct68_00.ArrayDwords_50.ItemArray + eax;
        eax = (ItemArray[0] >> 0x09);
        edi = (ItemArray[1] + 0x1FF) >> 0x09;

        if((eax + 0x0A) > edi)
        {
            // HOTS: 01959F94
            pTriplet = Struct68_00.BaseValues.ItemArray + eax + 1;
            while(edx >= pTriplet->BaseValue)
            {
                // HOTS: 1959FA3
                pTriplet++;
                eax++;
            }
        }
        else
        {
            // Binary search
            // HOTS: 1959FAD
            if((eax + 1) < edi)
            {
                // HOTS: 1959FB4
                esi = (edi + eax) >> 1;
                if(edx < Struct68_00.BaseValues.ItemArray[esi].BaseValue)
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
        pTriplet = Struct68_00.BaseValues.ItemArray + eax;
        edx = edx - pTriplet->BaseValue;
        edi = eax << 0x04;
        eax = pTriplet->Value2;
        ebx = (eax >> 0x17);
        if(edx < ebx)
        {
            // HOTS: 1959FF1
            esi = (eax >> 0x07) & 0xFF;
            if(edx < esi)
            {
                // HOTS: 0195A000
                eax = eax & 0x7F;
                if(edx >= eax)
                {
                    // HOTS: 195A007
                    edi = edi + 2;
                    edx = edx - eax;
                }
            }
            else
            {
                // HOTS: 195A00E
                eax = (eax >> 0x0F) & 0xFF;
                if(edx < eax)
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
            esi = pTriplet->Value3;
            eax = (pTriplet->Value3 >> 0x09) & 0x1FF;
            if(edx < eax)
            {
                // HOTS: 195A037
                esi = esi & 0x1FF;
                if(edx < esi)
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
                esi = (esi >> 0x12) & 0x1FF;
                if(edx < esi)
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
        esi = Struct68_00.HaveBits.ItemArray[edi];
        eax = GetNumberOfSetBits(esi);
        ecx = eax >> 0x18;

        if(edx >= ecx)
        {
            // HOTS: 195A0B2
            esi = Struct68_00.HaveBits.ItemArray[++edi];
            edx = edx - ecx;
            eax = GetNumberOfSetBits(esi);
        }

        // HOTS: 195A0F6
        ecx = (eax >> 0x08) & 0xFF;

        edi = (edi << 0x05);
        if(edx < ecx)
        {
            // HOTS: 195A111
            eax = eax & 0xFF;
            if(edx >= eax)
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
            if(edx < eax)
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

    // HOTS: 1957970
    bool CheckNextPathFragment(TMndxFindResult * pStruct1C)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        LPBYTE pbPathName = (LPBYTE)pStruct1C->szSearchMask;
        DWORD CollisionIndex;
        DWORD NameFragIndex;
        DWORD SaveCharIndex;
        DWORD HiBitsIndex;
        DWORD FragOffs;

        // Calculate index of the next name fragment in the name fragment table
        NameFragIndex = ((pStruct40->ItemIndex << 0x05) ^ pStruct40->ItemIndex ^ pbPathName[pStruct40->CharIndex]) & NameFragIndexMask;

        // Does the hash value match?
        if(NameFragTable.ItemArray[NameFragIndex].ItemIndex == pStruct40->ItemIndex)
        {
            // Check if there is single character match
            if(IS_SINGLE_CHAR_MATCH(NameFragTable, NameFragIndex))
            {
                pStruct40->ItemIndex = NameFragTable.ItemArray[NameFragIndex].NextIndex;
                pStruct40->CharIndex++;
                return true;
            }

            // Check if there is a name fragment match
            if(pNextDB != NULL)
            {
                if(!pNextDB->sub_1957B80(pStruct1C, NameFragTable.ItemArray[NameFragIndex].FragOffs))
                    return false;
            }
            else
            {
                if(!IndexStruct_174.CheckNameFragment(pStruct1C, NameFragTable.ItemArray[NameFragIndex].FragOffs))
                    return false;
            }

            pStruct40->ItemIndex = NameFragTable.ItemArray[NameFragIndex].NextIndex;
            return true;
        }

        //
        // Conflict: Multiple hashes give the same table index
        //

        // HOTS: 1957A0E
        CollisionIndex = sub_1959CB0(pStruct40->ItemIndex) + 1;
        if(!Struct68_00.IsItemPresent(CollisionIndex))
            return false;

        pStruct40->ItemIndex = (CollisionIndex - pStruct40->ItemIndex - 1);
        HiBitsIndex = 0xFFFFFFFF;

    //  CascDumpSparseArray("E:\\casc-array-68.txt", &FileNameIndexes);
    //  CascDumpSparseArray("E:\\casc-array-D0.txt", &Struct68_D0);

        // HOTS: 1957A41:
        do
        {
            // HOTS: 1957A41
            // Check if the low 8 bits if the fragment offset contain a single character
            // or an offset to a name fragment
            if(Struct68_D0.IsItemPresent(pStruct40->ItemIndex))
            {
                if(HiBitsIndex == 0xFFFFFFFF)
                {
                    // HOTS: 1957A6C
                    HiBitsIndex = Struct68_D0.GetItemValue(pStruct40->ItemIndex);
                }
                else
                {
                    // HOTS: 1957A7F
                    HiBitsIndex++;
                }

                // HOTS: 1957A83
                SaveCharIndex = pStruct40->CharIndex;

                // Get the name fragment offset as combined value from lower 8 bits and upper bits
                FragOffs = GetNameFragmentOffsetEx(pStruct40->ItemIndex, HiBitsIndex);

                // Compare the string with the fragment name database
                if(pNextDB != NULL)
                {
                    // HOTS: 1957AEC
                    if(pNextDB->sub_1957B80(pStruct1C, FragOffs))
                        return true;
                }
                else
                {
                    // HOTS: 1957AF7
                    if(IndexStruct_174.CheckNameFragment(pStruct1C, FragOffs))
                        return true;
                }

                // HOTS: 1957B0E
                // If there was partial match with the fragment, end the search
                if(pStruct40->CharIndex != SaveCharIndex)
                    return false;
            }
            else
            {
                // HOTS: 1957B1C
                if(FrgmDist_LoBits.ItemArray[pStruct40->ItemIndex] == pStruct1C->szSearchMask[pStruct40->CharIndex])
                {
                    pStruct40->CharIndex++;
                    return true;
                }
            }

            // HOTS: 1957B32
            pStruct40->ItemIndex++;
            CollisionIndex++;
        }
        while(Struct68_00.IsItemPresent(CollisionIndex));
        return false;
    }

    // HOTS: 1957B80
    bool sub_1957B80(TMndxFindResult * pStruct1C, DWORD arg_4)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        PNAME_FRAG pNameEntry;
        DWORD FragOffs;
        DWORD eax, edi;

        edi = arg_4;

        // HOTS: 1957B95
        for(;;)
        {
            pNameEntry = NameFragTable.ItemArray + (edi & NameFragIndexMask);
            if(edi == pNameEntry->NextIndex)
            {
                // HOTS: 01957BB4
                if((pNameEntry->FragOffs & 0xFFFFFF00) != 0xFFFFFF00)
                {
                    // HOTS: 1957BC7
                    if(pNextDB != NULL)
                    {
                        // HOTS: 1957BD3
                        if(!pNextDB->sub_1957B80(pStruct1C, pNameEntry->FragOffs))
                            return false;
                    }
                    else
                    {
                        // HOTS: 1957BE0
                        if(!IndexStruct_174.CheckNameFragment(pStruct1C, pNameEntry->FragOffs))
                            return false;
                    }
                }
                else
                {
                    // HOTS: 1957BEE
                    if(pStruct1C->szSearchMask[pStruct40->CharIndex] != (char)pNameEntry->FragOffs)
                        return false;
                    pStruct40->CharIndex++;
                }

                // HOTS: 1957C05
                edi = pNameEntry->ItemIndex;
                if(edi == 0)
                    return true;

                if(pStruct40->CharIndex >= pStruct1C->cchSearchMask)
                    return false;
            }
            else
            {
                // HOTS: 1957C30
                if(Struct68_D0.IsItemPresent(edi))
                {
                    // HOTS: 1957C4C
                    if(pNextDB != NULL)
                    {
                        // HOTS: 1957C58
                        FragOffs = GetNameFragmentOffset(edi);
                        if(!pNextDB->sub_1957B80(pStruct1C, FragOffs))
                            return false;
                    }
                    else
                    {
                        // HOTS: 1957350
                        FragOffs = GetNameFragmentOffset(edi);
                        if(!IndexStruct_174.CheckNameFragment(pStruct1C, FragOffs))
                            return false;
                    }
                }
                else
                {
                    // HOTS: 1957C8E
                    if(FrgmDist_LoBits.ItemArray[edi] != pStruct1C->szSearchMask[pStruct40->CharIndex])
                        return false;

                    pStruct40->CharIndex++;
                }

                // HOTS: 1957CB2
                if(edi <= field_214)
                    return true;

                if(pStruct40->CharIndex >= pStruct1C->cchSearchMask)
                    return false;

                eax = sub_1959F50(edi);
                edi = (eax - edi - 1);
            }
        }
    }

    // HOTS: 1958D70
    void sub_1958D70(TMndxFindResult * pStruct1C, DWORD arg_4)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        PNAME_FRAG pNameEntry;

        // HOTS: 1958D84
        for(;;)
        {
            pNameEntry = NameFragTable.ItemArray + (arg_4 & NameFragIndexMask);
            if(arg_4 == pNameEntry->NextIndex)
            {
                // HOTS: 1958DA6
                if((pNameEntry->FragOffs & 0xFFFFFF00) != 0xFFFFFF00)
                {
                    // HOTS: 1958DBA
                    if(pNextDB != NULL)
                    {
                        pNextDB->sub_1958D70(pStruct1C, pNameEntry->FragOffs);
                    }
                    else
                    {
                        IndexStruct_174.CopyNameFragment(pStruct1C, pNameEntry->FragOffs);
                    }
                }
                else
                {
                    // HOTS: 1958DE7
                    // Insert the low 8 bits to the path being built
                    pStruct40->PathBuffer.Insert((char)(pNameEntry->FragOffs & 0xFF));
                }

                // HOTS: 1958E71
                arg_4 = pNameEntry->ItemIndex;
                if(arg_4 == 0)
                    return;
            }
            else
            {
                // HOTS: 1958E8E
                if(Struct68_D0.IsItemPresent(arg_4))
                {
                    DWORD FragOffs;

                    // HOTS: 1958EAF
                    FragOffs = GetNameFragmentOffset(arg_4);
                    if(pNextDB != NULL)
                    {
                        pNextDB->sub_1958D70(pStruct1C, FragOffs);
                    }
                    else
                    {
                        IndexStruct_174.CopyNameFragment(pStruct1C, FragOffs);
                    }
                }
                else
                {
                    // HOTS: 1958F50
                    // Insert one character to the path being built
                    pStruct40->PathBuffer.Insert(FrgmDist_LoBits.ItemArray[arg_4]);
                }

                // HOTS: 1958FDE
                if(arg_4 <= field_214)
                    return;

                arg_4 = 0xFFFFFFFF - arg_4 + sub_1959F50(arg_4);
            }
        }
    }

    // HOTS: 1959010
    bool sub_1959010(TMndxFindResult * pStruct1C, DWORD arg_4)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        PNAME_FRAG pNameEntry;

        // HOTS: 1959024
        for(;;)
        {
            pNameEntry = NameFragTable.ItemArray + (arg_4 & NameFragIndexMask);
            if(arg_4 == pNameEntry->NextIndex)
            {
                // HOTS: 1959047
                if((pNameEntry->FragOffs & 0xFFFFFF00) != 0xFFFFFF00)
                {
                    // HOTS: 195905A
                    if(pNextDB != NULL)
                    {
                        if(!pNextDB->sub_1959010(pStruct1C, pNameEntry->FragOffs))
                            return false;
                    }
                    else
                    {
                        if(!IndexStruct_174.CheckAndCopyNameFragment(pStruct1C, pNameEntry->FragOffs))
                            return false;
                    }
                }
                else
                {
                    // HOTS: 1959092
                    if((char)(pNameEntry->FragOffs & 0xFF) != pStruct1C->szSearchMask[pStruct40->CharIndex])
                        return false;

                    // Insert the low 8 bits to the path being built
                    pStruct40->PathBuffer.Insert((char)(pNameEntry->FragOffs & 0xFF));
                    pStruct40->CharIndex++;
                }

                // HOTS: 195912E
                arg_4 = pNameEntry->ItemIndex;
                if(arg_4 == 0)
                    return true;
            }
            else
            {
                // HOTS: 1959147
                if(Struct68_D0.IsItemPresent(arg_4))
                {
                    DWORD FragOffs;

                    // HOTS: 195917C
                    FragOffs = GetNameFragmentOffset(arg_4);
                    if(pNextDB != NULL)
                    {
                        if(!pNextDB->sub_1959010(pStruct1C, FragOffs))
                            return false;
                    }
                    else
                    {
                        if(!IndexStruct_174.CheckAndCopyNameFragment(pStruct1C, FragOffs))
                            return false;
                    }
                }
                else
                {
                    // HOTS: 195920E
                    if(FrgmDist_LoBits.ItemArray[arg_4] != pStruct1C->szSearchMask[pStruct40->CharIndex])
                        return false;

                    // Insert one character to the path being built
                    pStruct40->PathBuffer.Insert(FrgmDist_LoBits.ItemArray[arg_4]);
                    pStruct40->CharIndex++;
                }

                // HOTS: 19592B6
                if(arg_4 <= field_214)
                    return true;

                arg_4 = 0xFFFFFFFF - arg_4 + sub_1959F50(arg_4);
            }

            // HOTS: 19592D5
            if(pStruct40->CharIndex >= pStruct1C->cchSearchMask)
                break;
        }

        sub_1958D70(pStruct1C, arg_4);
        return true;
    }

    // HOTS: 1959460
    bool sub_1959460(TMndxFindResult * pStruct1C)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        PATH_STOP * pPathStop;
        DWORD FragOffs;
        DWORD edi;

        if(pStruct40->SearchPhase == MNDX_SEARCH_FINISHED)
            return false;

        if(pStruct40->SearchPhase != MNDX_SEARCH_SEARCHING)
        {
            // HOTS: 1959489
            pStruct40->InitSearchBuffers();

            // If the caller passed a part of the search path, we need to find that one
            while(pStruct40->CharIndex < pStruct1C->cchSearchMask)
            {
                if(!sub_1958B00(pStruct1C))
                {
                    pStruct40->SearchPhase = MNDX_SEARCH_FINISHED;
                    return false;
                }
            }

            // HOTS: 19594b0
            PATH_STOP PathStop(pStruct40->ItemIndex, 0, pStruct40->PathBuffer.ItemCount);
            pStruct40->PathStops.Insert(PathStop);
            pStruct40->ItemCount = 1;

            if(FileNameIndexes.IsItemPresent(pStruct40->ItemIndex))
            {
                pStruct1C->szFoundPath   = pStruct40->PathBuffer.ItemArray;
                pStruct1C->cchFoundPath  = pStruct40->PathBuffer.ItemCount;
                pStruct1C->FileNameIndex = FileNameIndexes.GetItemValue(pStruct40->ItemIndex);
                return true;
            }
        }

        // HOTS: 1959522
        for(;;)
        {
            // HOTS: 1959530
            if(pStruct40->ItemCount == pStruct40->PathStops.ItemCount)
            {
                PATH_STOP * pLastStop;
                DWORD CollisionIndex;

                pLastStop = pStruct40->PathStops.ItemArray + pStruct40->PathStops.ItemCount - 1;
                CollisionIndex = sub_1959CB0(pLastStop->LoBitsIndex) + 1;

                // Insert a new structure
                PATH_STOP PathStop(CollisionIndex - pLastStop->LoBitsIndex - 1, CollisionIndex, 0);
                pStruct40->PathStops.Insert(PathStop);
            }

            // HOTS: 19595BD
            pPathStop = pStruct40->PathStops.ItemArray + pStruct40->ItemCount;

            // HOTS: 19595CC
            if(Struct68_00.IsItemPresent(pPathStop->field_4++))
            {
                // HOTS: 19595F2
                pStruct40->ItemCount++;

                if(Struct68_D0.IsItemPresent(pPathStop->LoBitsIndex))
                {
                    // HOTS: 1959617
                    if(pPathStop->field_C == 0xFFFFFFFF)
                        pPathStop->field_C = Struct68_D0.GetItemValue(pPathStop->LoBitsIndex);
                    else
                        pPathStop->field_C++;

                    // HOTS: 1959630
                    FragOffs = GetNameFragmentOffsetEx(pPathStop->LoBitsIndex, pPathStop->field_C);
                    if(pNextDB != NULL)
                    {
                        // HOTS: 1959649
                        pNextDB->sub_1958D70(pStruct1C, FragOffs);
                    }
                    else
                    {
                        // HOTS: 1959654
                        IndexStruct_174.CopyNameFragment(pStruct1C, FragOffs);
                    }
                }
                else
                {
                    // HOTS: 1959665
                    // Insert one character to the path being built
                    pStruct40->PathBuffer.Insert(FrgmDist_LoBits.ItemArray[pPathStop->LoBitsIndex]);
                }

                // HOTS: 19596AE
                pPathStop->Count = pStruct40->PathBuffer.ItemCount;

                // HOTS: 19596b6
                if(FileNameIndexes.IsItemPresent(pPathStop->LoBitsIndex))
                {
                    // HOTS: 19596D1
                    if(pPathStop->field_10 == 0xFFFFFFFF)
                    {
                        // HOTS: 19596D9
                        pPathStop->field_10 = FileNameIndexes.GetItemValue(pPathStop->LoBitsIndex);
                    }
                    else
                    {
                        pPathStop->field_10++;
                    }

                    // HOTS: 1959755
                    pStruct1C->szFoundPath = pStruct40->PathBuffer.ItemArray;
                    pStruct1C->cchFoundPath = pStruct40->PathBuffer.ItemCount;
                    pStruct1C->FileNameIndex = pPathStop->field_10;
                    return true;
                }
            }
            else
            {
                // HOTS: 19596E9
                if(pStruct40->ItemCount == 1)
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

    // HOTS: 1958B00
    bool sub_1958B00(TMndxFindResult * pStruct1C)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;
        LPBYTE pbPathName = (LPBYTE)pStruct1C->szSearchMask;
        DWORD CollisionIndex;
        DWORD FragmentOffset;
        DWORD SaveCharIndex;
        DWORD ItemIndex;
        DWORD FragOffs;
        DWORD var_4;

        ItemIndex = pbPathName[pStruct40->CharIndex] ^ (pStruct40->ItemIndex << 0x05) ^ pStruct40->ItemIndex;
        ItemIndex = ItemIndex & NameFragIndexMask;
        if(pStruct40->ItemIndex == NameFragTable.ItemArray[ItemIndex].ItemIndex)
        {
            // HOTS: 1958B45
            FragmentOffset = NameFragTable.ItemArray[ItemIndex].FragOffs;
            if((FragmentOffset & 0xFFFFFF00) == 0xFFFFFF00)
            {
                // HOTS: 1958B88
                pStruct40->PathBuffer.Insert((char)FragmentOffset);
                pStruct40->ItemIndex = NameFragTable.ItemArray[ItemIndex].NextIndex;
                pStruct40->CharIndex++;
                return true;
            }

            // HOTS: 1958B59
            if(pNextDB != NULL)
            {
                if(!pNextDB->sub_1959010(pStruct1C, FragmentOffset))
                    return false;
            }
            else
            {
                if(!IndexStruct_174.CheckAndCopyNameFragment(pStruct1C, FragmentOffset))
                    return false;
            }

            // HOTS: 1958BCA
            pStruct40->ItemIndex = NameFragTable.ItemArray[ItemIndex].NextIndex;
            return true;
        }

        // HOTS: 1958BE5
        CollisionIndex = sub_1959CB0(pStruct40->ItemIndex) + 1;
        if(!Struct68_00.IsItemPresent(CollisionIndex))
            return false;

        pStruct40->ItemIndex = (CollisionIndex - pStruct40->ItemIndex - 1);
        var_4 = 0xFFFFFFFF;

        // HOTS: 1958C20
        for(;;)
        {
            if(Struct68_D0.IsItemPresent(pStruct40->ItemIndex))
            {
                // HOTS: 1958C0E
                if(var_4 == 0xFFFFFFFF)
                {
                    // HOTS: 1958C4B
                    var_4 = Struct68_D0.GetItemValue(pStruct40->ItemIndex);
                }
                else
                {
                    var_4++;
                }

                // HOTS: 1958C62
                SaveCharIndex = pStruct40->CharIndex;

                FragOffs = GetNameFragmentOffsetEx(pStruct40->ItemIndex, var_4);
                if(pNextDB != NULL)
                {
                    // HOTS: 1958CCB
                    if(pNextDB->sub_1959010(pStruct1C, FragOffs))
                        return true;
                }
                else
                {
                    // HOTS: 1958CD6
                    if(IndexStruct_174.CheckAndCopyNameFragment(pStruct1C, FragOffs))
                        return true;
                }

                // HOTS: 1958CED
                if(SaveCharIndex != pStruct40->CharIndex)
                    return false;
            }
            else
            {
                // HOTS: 1958CFB
                if(FrgmDist_LoBits.ItemArray[pStruct40->ItemIndex] == pStruct1C->szSearchMask[pStruct40->CharIndex])
                {
                    // HOTS: 1958D11
                    pStruct40->PathBuffer.Insert(FrgmDist_LoBits.ItemArray[pStruct40->ItemIndex]);
                    pStruct40->CharIndex++;
                    return true;
                }
            }

            // HOTS: 1958D11
            pStruct40->ItemIndex++;
            CollisionIndex++;

            if(!Struct68_00.IsItemPresent(CollisionIndex))
                break;
        }

        return false;
    }

    // HOTS: 1957EF0
    bool FindFileInDatabase(TMndxFindResult * pStruct1C)
    {
        TStruct40 * pStruct40 = pStruct1C->pStruct40;

        pStruct40->ItemIndex = 0;
        pStruct40->CharIndex = 0;
        pStruct40->SearchPhase = MNDX_SEARCH_INITIALIZING;

        if(pStruct1C->cchSearchMask > 0)
        {
            while(pStruct40->CharIndex < pStruct1C->cchSearchMask)
            {
                // HOTS: 01957F12
                if(!CheckNextPathFragment(pStruct1C))
                    return false;
            }
        }

        // HOTS: 1957F26
        if(!FileNameIndexes.IsItemPresent(pStruct40->ItemIndex))
            return false;

        pStruct1C->szFoundPath   = pStruct1C->szSearchMask;
        pStruct1C->cchFoundPath  = pStruct1C->cchSearchMask;
        pStruct1C->FileNameIndex = FileNameIndexes.GetItemValue(pStruct40->ItemIndex);
        return true;
    }

    // HOTS: 1959790
    int LoadFromStream(TByteStream & InStream)
    {
        DWORD dwBitMask;
        int nError;

        nError = Struct68_00.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = FileNameIndexes.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = Struct68_D0.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        // HOTS: 019597CD
        nError = FrgmDist_LoBits.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = FrgmDist_HiBits.LoadBitsFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        // HOTS: 019597F5
        nError = IndexStruct_174.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        // HOTS: 0195980A
        if(Struct68_D0.ValidItemCount != 0 && IndexStruct_174.NameFragments.ItemCount == 0)
        {
            TFileNameDatabase * pNewDB;

            pNewDB = new TFileNameDatabase;
            if (pNewDB == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            nError = SetNextDatabase(pNewDB);
            if(nError != ERROR_SUCCESS)
                return nError;

            nError = pNextDB->LoadFromStream(InStream);
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        // HOTS: 0195986B
        nError = NameFragTable.LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;

        NameFragIndexMask = NameFragTable.ItemCount - 1;

        nError = InStream.GetValue<DWORD>(field_214);
        if(nError != ERROR_SUCCESS)
            return nError;

        nError = InStream.GetValue<DWORD>(dwBitMask);
        if(nError != ERROR_SUCCESS)
            return nError;

        return Struct10.sub_1957800(dwBitMask);
    }

    TSparseArray Struct68_00;
    TSparseArray FileNameIndexes;               // Array of file name indexes
    TSparseArray Struct68_D0;

    // This pair of arrays serves for fast conversion from name hash to fragment offset
    TGenericArray<BYTE> FrgmDist_LoBits;        // Array of lower 8 bits of name fragment offset
    TBitEntryArray FrgmDist_HiBits;             // Array of upper x bits of name fragment offset

    TNameIndexStruct IndexStruct_174;
    TFileNameDatabase * pNextDB;

    TGenericArray<NAME_FRAG> NameFragTable;

    DWORD NameFragIndexMask;
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
    int SearchFile(TMndxFindResult * pStruct1C)
    {
        int nError = ERROR_SUCCESS;

        if(pDatabase == NULL)
            return ERROR_INVALID_PARAMETER;

        nError = pStruct1C->CreateStruct40();
        if(nError != ERROR_SUCCESS)
            return nError;

        if(!pDatabase->FindFileInDatabase(pStruct1C))
            nError = ERROR_FILE_NOT_FOUND;

        pStruct1C->FreeStruct40();
        return nError;
    }

    // HOTS: 1956CE0
    int sub_1956CE0(TMndxFindResult * pStruct1C, bool * pbFindResult)
    {
        int nError = ERROR_SUCCESS;

        if(pDatabase == NULL)
            return ERROR_INVALID_PARAMETER;

        // Create the pStruct40, if not initialized yet
        if(pStruct1C->pStruct40 == NULL)
        {
            nError = pStruct1C->CreateStruct40();
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        *pbFindResult = pDatabase->sub_1959460(pStruct1C);
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

        for(i = 0; i < Packages.ItemCount; i++)
        {
            pPackage = (PMNDX_PACKAGE)Array_ItemAt(&Packages, i);
            CASC_FREE(pPackage->szFileName);
        }
        Array_Free(&Packages);
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
        assert(Packages.ItemCount != 0);

        //FILE * fp = fopen("E:\\packages.txt", "wt");
        //for(size_t i = 0; i < hs->pPackages->NameEntries; i++, pPackage++)
        //{
        //    if(pPackage->szFileName != NULL)
        //        fprintf(fp, "%s\n", pPackage->szFileName);
        //}
        //fclose(fp);

        // Find the longest matching name
        for(size_t i = 0; i < Packages.ItemCount; i++)
        {
            PMNDX_PACKAGE pPackage = (PMNDX_PACKAGE)Array_ItemAt(&Packages, i);

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
        TMndxFindResult Struct1C;

        // Search the database for the file name
        if(MndxInfo.bRootFileLoaded)
        {
            Struct1C.SetSearchPath(szFileName, strlen(szFileName));

            // Search the file name in the second MAR info (the one with stripped package names)
            if(MndxInfo.MarFiles[1]->SearchFile(&Struct1C) != ERROR_SUCCESS)
                return ERROR_FILE_NOT_FOUND;

            // The found MNDX index must fall into range of valid MNDX entries
            if(Struct1C.FileNameIndex < MndxInfo.MndxEntriesValid)
            {
                // HOTS: E945F4
                pRootEntry = ppValidEntries[Struct1C.FileNameIndex];
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

    LPBYTE FillFindData(TCascSearch * pSearch, TMndxFindResult * pStruct1C)
    {
        PMNDX_ROOT_ENTRY pRootEntry = NULL;
        PMNDX_PACKAGE pPackage;
        char * szStrippedPtr;
        char szStrippedName[MAX_PATH+1];
        int nError;

        // Sanity check
        assert(pStruct1C->cchFoundPath < MAX_PATH);

        // Fill the file name
        memcpy(pSearch->szFileName, pStruct1C->szFoundPath, pStruct1C->cchFoundPath);
        pSearch->szFileName[pStruct1C->cchFoundPath] = 0;

        // Fill the file size
        pPackage = FindMndxPackage(pSearch->szFileName);
        if(pPackage == NULL)
            return NULL;

        // Cut the package name off the full path
        szStrippedPtr = pSearch->szFileName + pPackage->nLength;
        while(szStrippedPtr[0] == '/')
            szStrippedPtr++;

        // We need to convert the stripped name to lowercase, replacing backslashes with slashes
        NormalizeFileName_LowerSlash(szStrippedName, szStrippedPtr, MAX_PATH);

        // Search the package
        nError = SearchMndxInfo(szStrippedName, pPackage->nIndex, &pRootEntry);
        if(nError != ERROR_SUCCESS)
            return NULL;

        // Give the file size
        pSearch->dwFileSize = pRootEntry->ContentSize;
        return pRootEntry->CKey;
    }

    int LoadPackageNames()
    {
        TMndxFindResult Struct1C;
        int nError;

        // Prepare the file name search in the top level directory
        Struct1C.SetSearchPath("", 0);

        // Allocate initial name list structure
        nError = Array_Create(&Packages, MNDX_PACKAGE, 0x40);
        if(nError != ERROR_SUCCESS)
            return nError;

        // Keep searching as long as we find something
        for(;;)
        {
            TMndxMarFile * pMarFile = MndxInfo.MarFiles[0];
            PMNDX_PACKAGE pPackage;
            char * szFileName;
            bool bFindResult = false;

            // Search the next file name
            pMarFile->sub_1956CE0(&Struct1C, &bFindResult);
            if(bFindResult == false)
                break;

            // Create file name
            szFileName = CASC_ALLOC(char, Struct1C.cchFoundPath + 1);
            if(szFileName == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            // Insert the found name to the top level directory list
            pPackage = (PMNDX_PACKAGE)Array_Insert(&Packages, NULL, 1);
            if(pPackage == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            // Fill the file name
            memcpy(szFileName, Struct1C.szFoundPath, Struct1C.cchFoundPath);
            szFileName[Struct1C.cchFoundPath] = 0;

            // Fill the package structure
            pPackage->szFileName = szFileName;
            pPackage->nLength = Struct1C.cchFoundPath;
            pPackage->nIndex = Struct1C.FileNameIndex;
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
                ppValidEntries[nIndex1++] = pMndxEntries;

                // Put the remaining entries
                for(i = 0; i < MndxInfo.MndxEntriesTotal; i++, pRootEntry++)
                {
                    if(ValidEntryCount > MndxInfo.MndxEntriesValid)
                        break;

                    if(pRootEntry->Flags & 0x80000000)
                    {
                        ppValidEntries[nIndex1++] = pRootEntry + 1;
                        ValidEntryCount++;
                    }
                }

                // Verify the final number of valid entries
                if((ValidEntryCount - 1) != MndxInfo.MndxEntriesValid)
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

    LPBYTE Search(TCascSearch * pSearch)
    {
        TMndxFindResult * pStruct1C = NULL;
        TMndxMarFile * pMarFile = MndxInfo.MarFiles[2];
        bool bFindResult = false;

        // If the first time, allocate the structure for the search result
        if(pSearch->pRootContext == NULL)
        {
            // Create the new search structure
            pStruct1C = new TMndxFindResult;
            if(pStruct1C == NULL)
                return NULL;

            // Setup the search mask
            pStruct1C->SetSearchPath("", 0);
            pSearch->pRootContext = pStruct1C;
        }

        // Make shortcut for the search structure
        assert(pSearch->pRootContext != NULL);
        pStruct1C = (TMndxFindResult *)pSearch->pRootContext;

        // Keep searching
        for(;;)
        {
            // Search the next file name
            pMarFile->sub_1956CE0(pStruct1C, &bFindResult);
            if (bFindResult == false)
                return NULL;

            // If we have no wild mask, we found it
            if (pSearch->szMask == NULL || pSearch->szMask[0] == 0)
                break;

            // Copy the found name to the buffer
            memcpy(pSearch->szFileName, pStruct1C->szFoundPath, pStruct1C->cchFoundPath);
            pSearch->szFileName[pStruct1C->cchFoundPath] = 0;
            if (CheckWildCard(pSearch->szFileName, pSearch->szMask))
                break;
        }

        // Give the file size and CKey
        return FillFindData(pSearch, pStruct1C);
    }

    void EndSearch(TCascSearch * pSearch)
    {
        if(pSearch != NULL)
            delete (TMndxFindResult *)pSearch->pRootContext;
        pSearch->pRootContext = NULL;
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

#if defined(_DEBUG) && defined(_X86_) && defined(CASCLIB_TEST)
//  CascDumpNameFragTable("E:\\casc-name-fragment-table.txt", MndxInfo.MarFiles[0]);
//  CascDumpFileNames("E:\\casc-listfile.txt", MndxInfo.MarFiles[0]);
//  TestMndxRootFile(pRootHandler);
#endif
}

//----------------------------------------------------------------------------
// Unit tests

#if defined(_DEBUG) && defined(_X86_) && defined(CASCLIB_TEST)
/*
extern "C" {
    bool  _cdecl sub_1958B00_x86(TFileNameDatabase * pDB, TMndxFindResult * pStruct1C);
    DWORD _cdecl sub_19573D0_x86(TFileNameDatabase * pDB, DWORD arg_0, DWORD arg_4);
    DWORD _cdecl sub_1957EF0_x86(TFileNameDatabase * pDB, TMndxFindResult * pStruct1C);
    bool  _cdecl sub_1959460_x86(TFileNameDatabase * pDB, TMndxFindResult * pStruct1C);
    DWORD _cdecl GetItemValue_x86(TSparseArray * pStruct, DWORD dwKey);
    DWORD _cdecl sub_1959CB0_x86(TFileNameDatabase * pDB, DWORD dwKey);
    DWORD _cdecl sub_1959F50_x86(TFileNameDatabase * pDB, DWORD arg_0);
}

extern "C" void * allocate_zeroed_memory_x86(size_t bytes)
{
    void * ptr = CASC_ALLOC(BYTE, bytes);

    if(ptr != NULL)
        memset(ptr, 0, bytes);
    return ptr;
}

extern "C" void free_memory_x86(void * ptr)
{
    if(ptr != NULL)
    {
        CASC_FREE(ptr);
    }
}

static int sub_1956CE0_x86(TFileNameDatabasePtr * pDatabasePtr, TMndxFindResult * pStruct1C, bool * pbFindResult)
{
    int nError = ERROR_SUCCESS;

    if(pDatabasePtr->pDB == NULL)
        return ERROR_INVALID_PARAMETER;

    // Create the pStruct40, if not initialized yet
    if(pStruct1C->pStruct40 == NULL)
    {
        nError = pStruct1C->CreateStruct40();
        if(nError != ERROR_SUCCESS)
            return nError;
    }

    *pbFindResult = sub_1959460_x86(pDatabasePtr->pDB, pStruct1C);
    return nError;
}

static void TestFileSearch_SubStrings(PMAR_FILE pMarFile, char * szFileName, size_t nLength)
{
    TMndxFindResult Struct1C_1;
    TMndxFindResult Struct1C_2;

//  if(strcmp(szFileName, "mods/heroes.stormmod/base.stormassets/assets/textures/storm_temp_war3_btnstatup.dds"))
//      return;

    // Perform search on anything, that is longer than 4 chars
    while(nLength >= 4)
    {
        // Set a substring as search name
        Struct1C_1.SetSearchPath(szFileName, nLength);
        Struct1C_2.SetSearchPath(szFileName, nLength);
        szFileName[nLength] = 0;

        // Keep searching
        for(;;)
        {
            bool bFindResult1 = false;
            bool bFindResult2 = false;

            // Search the next file name (orig HOTS code)
            sub_1956CE0_x86(pMarFile->pDatabasePtr, &Struct1C_1, &bFindResult1);

            // Search the next file name (our code)
            pMarFile->pDatabasePtr->sub_1956CE0(&Struct1C_2, &bFindResult2);

            // Check the result
            assert(bFindResult1 == bFindResult2);
            assert(Struct1C_1.cchFoundPath == Struct1C_1.cchFoundPath);
            assert(Struct1C_1.FileNameIndex == Struct1C_2.FileNameIndex);
            assert(strncmp(Struct1C_1.szFoundPath, Struct1C_2.szFoundPath, Struct1C_1.cchFoundPath) == 0);
            assert(Struct1C_1.cchFoundPath < MAX_PATH);

            // Stop the search in case of failure
            if(bFindResult1 == false || bFindResult2 == false)
                break;
        }

        // Free the search structures
        Struct1C_1.FreeStruct40();
        Struct1C_2.FreeStruct40();
        nLength--;
    }
}

static void TestFindPackage(PMAR_FILE pMarFile, const char * szPackageName)
{
    TMndxFindResult Struct1C;

    // Search the database for the file name
    Struct1C.SetSearchPath(szPackageName, strlen(szPackageName));

    // Search the file name in the second MAR info (the one with stripped package names)
    MAR_FILE_SearchFile(pMarFile, &Struct1C);
}

static void TestFileSearch(PMAR_FILE pMarFile, const char * szFileName)
{
    TMndxFindResult Struct1C_1;
    TMndxFindResult Struct1C_2;
    size_t nLength = strlen(szFileName);
    char szNameBuff[MAX_PATH + 1];

    // Set an empty path as search mask (?)
    Struct1C_1.SetSearchPath(szFileName, nLength);
    Struct1C_2.SetSearchPath(szFileName, nLength);

    // Keep searching
    for(;;)
    {
        bool bFindResult1 = false;
        bool bFindResult2 = false;

        // Search the next file name (orig HOTS code)
        sub_1956CE0_x86(pMarFile->pDatabasePtr, &Struct1C_1, &bFindResult1);

        // Search the next file name (our code)
        pMarFile->pDatabasePtr->sub_1956CE0(&Struct1C_2, &bFindResult2);

        assert(bFindResult1 == bFindResult2);
        assert(Struct1C_1.cchFoundPath == Struct1C_1.cchFoundPath);
        assert(Struct1C_1.FileNameIndex == Struct1C_2.FileNameIndex);
        assert(strncmp(Struct1C_1.szFoundPath, Struct1C_2.szFoundPath, Struct1C_1.cchFoundPath) == 0);
        assert(Struct1C_1.cchFoundPath < MAX_PATH);

        // Stop the search in case of failure
        if(bFindResult1 == false || bFindResult2 == false)
            break;

        // Printf the found file name
        memcpy(szNameBuff, Struct1C_2.szFoundPath, Struct1C_2.cchFoundPath);
        szNameBuff[Struct1C_2.cchFoundPath] = 0;
//      printf("%s        \r", szNameBuff);

        // Perform sub-searches on this string and its substrings that are longer than 4 chars
//      TestFileSearch_SubStrings(pMarFile, szNameBuff, Struct1C_2.cchFoundPath);
    }

    // Free the search structures
    Struct1C_1.FreeStruct40();
    Struct1C_2.FreeStruct40();
}

static void TestMarFile(PMAR_FILE pMarFile, const char * szFileName, size_t nLength)
{
    TFileNameDatabase * pDB = pMarFile->pDatabasePtr->pDB;
    DWORD dwFileNameIndex1 = 0xFFFFFFFF;
    DWORD dwFileNameIndex2 = 0xFFFFFFFF;

    // Perform the search using original HOTS code
    {
        TMndxFindResult Struct1C;

        Struct1C.CreateStruct40();
        Struct1C.SetSearchPath(szFileName, nLength);

        // Call the original HOTS function
        sub_1957EF0_x86(pDB, &Struct1C);
        dwFileNameIndex1 = Struct1C.FileNameIndex;
    }

    // Perform the search using our code
    {
        TMndxFindResult Struct1C;

        Struct1C.CreateStruct40();
        Struct1C.SetSearchPath(szFileName, nLength);

        // Call our function
        pDB->FindFileInDatabase(&Struct1C);
        dwFileNameIndex2 = Struct1C.FileNameIndex;
    }

    // Compare both
    assert(dwFileNameIndex1 == dwFileNameIndex2);
}

static void TestMndxFunctions(PMAR_FILE pMarFile)
{
    TFileNameDatabase * pDB = pMarFile->pDatabasePtr->pDB;

    // Exercise function sub_19573D0
    for(DWORD arg_0 = 0; arg_0 < 0x100; arg_0++)
    {
        for(DWORD arg_4 = 0; arg_4 < 0x100; arg_4++)
        {
            DWORD dwResult1 = sub_19573D0_x86(pDB, arg_0, arg_4);
            DWORD dwResult2 = pDB->GetNameFragmentOffsetEx(arg_0, arg_4);

            assert(dwResult1 == dwResult2);
        }
    }

    // Exercise function GetItemValue
    for(DWORD i = 0; i < 0x10000; i++)
    {
        DWORD dwResult1 = GetItemValue_x86(&pDB->Struct68_D0, i);
        DWORD dwResult2 = pDB->Struct68_D0.GetItemValue(i);

        assert(dwResult1 == dwResult2);
    }

    // Exercise function sub_1959CB0
    for(DWORD i = 0; i < 0x9C; i++)
    {
        DWORD dwResult1 = sub_1959CB0_x86(pDB, i);
        DWORD dwResult2 = pDB->sub_1959CB0(i);

        assert(dwResult1 == dwResult2);
    }

    // Exercise function sub_1959F50
    for(DWORD i = 0; i < 0x40; i++)
    {
        DWORD dwResult1 = sub_1959F50_x86(pDB, i);
        DWORD dwResult2 = pDB->sub_1959F50(i);

        assert(dwResult1 == dwResult2);
    }
}

void TestMndxRootFile(PCASC_MNDX_INFO pMndxInfo)
{
    size_t nLength;
    char szFileName[MAX_PATH+1];
    void * pvListFile;

    // Exercise low level functions and compare their results
    // with original code from Heroes of the Storm
    TestMndxFunctions(pMndxInfo->MarFiles[0]);
    TestMndxFunctions(pMndxInfo->MarFiles[1]);
    TestMndxFunctions(pMndxInfo->MarFiles[2]);

    // Find a "mods" in the package array
    TestFindPackage(pMndxInfo->MarFiles[2], "mods/heroes.stormmod/base.stormassets/assets/textures/glow_green2.dds");
    TestMarFile(pMndxInfo->MarFiles[2], "mods/heroes.stormmod/base.stormassets/assets/textures/glow_green2.dds", 69);

    // Search the package MAR file aith a path shorter than a fragment
    TestFileSearch(pMndxInfo->MarFiles[0], "mods/heroes.s");

    // Test the file search
    TestFileSearch(pMndxInfo->MarFiles[0], "");
    TestFileSearch(pMndxInfo->MarFiles[1], "");
    TestFileSearch(pMndxInfo->MarFiles[2], "");

    // False file search
    TestFileSearch(pMndxInfo->MarFiles[1], "assets/textures/storm_temp_hrhu");

    // Open the listfile stream and initialize the listfile cache
    pvListFile = ListFile_OpenExternal(_T("e:\\Ladik\\Appdir\\CascLib\\listfile\\listfile-hots-29049.txt"));
    if(pvListFile != NULL)
    {
        // Check every file in the database
        while((nLength = ListFile_GetNext(pvListFile, "*", szFileName, MAX_PATH)) != 0)
        {
            // Normalize the file name: ToLower + BackSlashToSlash
            NormalizeFileName_LowerSlash(szFileName, szFileName, MAX_PATH);

            // Check the file with all three MAR files
            TestMarFile(pMndxInfo->MarFiles[0], szFileName, nLength);
            TestMarFile(pMndxInfo->MarFiles[1], szFileName, nLength);
            TestMarFile(pMndxInfo->MarFiles[2], szFileName, nLength);
        }

        ListFile_Free(pvListFile);
    }
}
*/
#endif  // defined(_DEBUG) && defined(_X86_) && defined(CASCLIB_TEST)
