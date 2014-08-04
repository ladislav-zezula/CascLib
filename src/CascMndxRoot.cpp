/*****************************************************************************/
/* CascMndxRoot.cpp                       Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Common functions for CascLib                                              */
/* Note: "HOTS" refers to Play.exe, v2.5.0.29049 (Heroes of the Storm Alpha) */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 18.05.14  1.00  Lad  The first version of CascMndxRoot.cpp                */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"
#include "CascMndxRoot.h"

//-----------------------------------------------------------------------------
// Local defines

#define CASC_MAR_SIGNATURE 0x0052414d           //  'MAR\0'

//-----------------------------------------------------------------------------
// Local structures

typedef struct _FILE_MNDX_HEADER
{
    DWORD Signature;                            // 'MNDX'
    DWORD HeaderVersion;                        // Must be <= 2
    DWORD FormatVersion;

} FILE_MNDX_HEADER, *PFILE_MNDX_HEADER;

typedef struct _FILE_MAR_INFO
{
    DWORD MarIndex;
    DWORD MarDataSize;
    DWORD MarDataSizeHi;
    DWORD MarDataOffset;
    DWORD MarDataOffsetHi;
} FILE_MAR_INFO, *PFILE_MAR_INFO;

//-----------------------------------------------------------------------------
// Testing functions prototypes

#if defined(_DEBUG) && defined(_X86_) && defined(CASCLIB_TEST)
extern "C" bool _cdecl sub_1958B00_x86(TFileNameDatabase * pDB, TMndxFindResult * pStruct1C);
void TestMndxRootFile(PCASC_MNDX_INFO pMndxInfo);
#endif

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
// Local functions - common

// HOTS: inlined
DWORD GetNumberOfSetBits(DWORD Value32)
{
    Value32 = ((Value32 >> 1) & 0x55555555) + (Value32 & 0x55555555);
    Value32 = ((Value32 >> 2) & 0x33333333) + (Value32 & 0x33333333);
    Value32 = ((Value32 >> 4) & 0x0F0F0F0F) + (Value32 & 0x0F0F0F0F);
    
    return (Value32 * 0x01010101);
}

static bool RootFileRead(LPBYTE pbFilePointer, LPBYTE pbFileEnd, void * pvBuffer, size_t dwBytesToRead)
{
    if((size_t)(pbFileEnd - pbFilePointer) < dwBytesToRead)
        return false;

    memcpy(pvBuffer, pbFilePointer, dwBytesToRead);
    return true;
}

//-----------------------------------------------------------------------------
// Local functions - TMndxFindResult

// HOTS: 01956EE0
TMndxFindResult::TMndxFindResult()
{
    szSearchMask = NULL;
    cchSearchMask = 0;
    field_8 = 0;
    szFoundPath = NULL;
    cchFoundPath = 0;
    MndxIndex = 0;
    pStruct40 = NULL;
}

// HOTS: 01956F00
TMndxFindResult::~TMndxFindResult()
{
    FreeStruct40();
}

// HOTS: 01956F30
int TMndxFindResult::CreateStruct40()
{
    if(pStruct40 != NULL)
        return ERROR_INVALID_PARAMETER;

    pStruct40 = new TStruct40();
    return (pStruct40 != NULL) ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

void TMndxFindResult::FreeStruct40()
{
    if(pStruct40 != NULL)
        delete pStruct40;
    pStruct40 = NULL;
}

// HOTS: 01956E70
int TMndxFindResult::SetSearchPath(
    const char * szNewSearchMask,
    size_t cchNewSearchMask)
{
    if(szSearchMask == NULL && cchSearchMask != 0)
        return ERROR_INVALID_PARAMETER;

    if(pStruct40 != NULL)
        pStruct40->SearchPhase = CASC_SEARCH_INITIALIZING;
    
    szSearchMask = szNewSearchMask;
    cchSearchMask = cchNewSearchMask;
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// TByteStream functions               
                                       
// HOTS: 01959990                      
TByteStream::TByteStream()             
{                                      
    pbByteData = NULL;                 
    pvMappedFile = NULL;               
    cbByteData = 0;                    
    field_C  = 0;                      
    hFile    = 0;                      
    hMap     = 0;                      
}                                      
                                       
// HOTS: 19599F0                       
void TByteStream::ExchangeWith(TByteStream & Target)
{                                      
    TByteStream WorkBuff;              
                                       
    WorkBuff = *this;                  
    *this    = Target;                 
    Target   = WorkBuff;               
}                                      
                                       
// HOTS: 19599F0                       
int TByteStream::GetBytes(DWORD cbByteCount, PVARIANT_POINTER PtrArray)
{                                      
    if(cbByteData < cbByteCount)       
        return ERROR_BAD_FORMAT;

    // Give the buffer to the caller
    PtrArray->BytePtr = pbByteData;

    // Move pointers
    pbByteData += cbByteCount;
    cbByteData -= cbByteCount;
    return ERROR_SUCCESS;
}

// HOTS: 1957190
int TByteStream::GetArray_DWORDs(PVARIANT_POINTER PtrArray, DWORD ItemCount)
{
    if(PtrArray == NULL && ItemCount != 0)
        return ERROR_INVALID_PARAMETER;
    if(ItemCount > CASC_MAX_ENTRIES(DWORD))
        return ERROR_NOT_ENOUGH_MEMORY;

    return GetBytes(ItemCount * 4, PtrArray);
}

// HOTS: 19571E0
int TByteStream::GetArray_Triplets(PVARIANT_POINTER PtrArray, DWORD ItemCount)
{
    if(PtrArray == NULL && ItemCount != 0)
        return ERROR_INVALID_PARAMETER;
    if(ItemCount > CASC_MAX_ENTRIES(TRIPLET))
        return ERROR_NOT_ENOUGH_MEMORY;

    return GetBytes(ItemCount * sizeof(TRIPLET), PtrArray);
}

// HOTS: 1957230
int TByteStream::GetArray_BYTES(PVARIANT_POINTER PtrArray, DWORD ItemCount)
{
    if(PtrArray == NULL && ItemCount != 0)
        return ERROR_INVALID_PARAMETER;
    if(ItemCount > CASC_MAX_ENTRIES(BYTE))
        return ERROR_NOT_ENOUGH_MEMORY;

    return GetBytes(ItemCount, PtrArray);
}

// HOTS: 1957280
int TByteStream::GetArray_NameTable(PVARIANT_POINTER PtrArray, DWORD ItemCount)
{
    if(PtrArray == NULL && ItemCount != 0)
        return ERROR_INVALID_PARAMETER;
    if(ItemCount > CASC_MAX_ENTRIES(NAME_ENTRY))
        return ERROR_NOT_ENOUGH_MEMORY;

    return GetBytes(ItemCount * sizeof(NAME_ENTRY), PtrArray);
}

// HOTS: 1959A60
int TByteStream::SkipBytes(DWORD cbByteCount)
{
    VARIANT_POINTER Dummy;

    return GetBytes(cbByteCount, &Dummy);
}

// HOTS: 1959AF0
int TByteStream::SetByteBuffer(LPBYTE pbNewByteData, DWORD cbNewByteData)
{
    if(pbNewByteData != NULL || cbNewByteData == 0)
    {
        pbByteData = pbNewByteData;
        cbByteData = cbNewByteData;
        return ERROR_SUCCESS;
    }

    return ERROR_INVALID_PARAMETER;
}


// HOTS: 1957160
int TByteStream::GetValue_DWORD(DWORD & Value)
{
    VARIANT_POINTER Pointer;
    int nError;

    nError = GetBytes(sizeof(DWORD), &Pointer);
    if(nError != ERROR_SUCCESS)
        return nError;

    Value = Pointer.DwordPtr[0];
    return ERROR_SUCCESS;
}

int TByteStream::GetValue_ItemCount(DWORD & NumberOfBytes, DWORD & ItemCount, DWORD ItemSize)
{
    VARIANT_POINTER Pointer;
    ULONGLONG ByteCount;
    int nError;

    // Verify if there is at least - 8 bytes
    nError = GetBytes(sizeof(ULONGLONG), &Pointer);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Extract the number of bytes
    ByteCount = Pointer.Int64Ptr[0];
    if(ByteCount > 0xFFFFFFFF || (ByteCount % ItemSize) != 0)
        return ERROR_BAD_FORMAT;
    
    // Give the result to the caller
    NumberOfBytes = (DWORD)ByteCount;
    ItemCount = (DWORD)(ByteCount / ItemSize);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// TGenericArray functions

TGenericArray::TGenericArray()
{
    DataBuffer.BytePtr = NULL;
    FirstValid.BytePtr = NULL;
    Array.BytePtr = NULL;
    ItemCount = 0;
    MaxItemCount = 0;
    bIsValidArray = false;
}

TGenericArray::~TGenericArray()
{
    if(DataBuffer.CharPtr != NULL)
        CASC_FREE(DataBuffer.CharPtr);
}

// HOTS: inlined
void TGenericArray::ExchangeWith(TGenericArray & Target)
{
    TGenericArray WorkBuff;

    WorkBuff = *this;
    *this    = Target;
    Target   = WorkBuff;
}

// HOTS: Inlined
void TGenericArray::CopyFrom(TGenericArray & Source)
{
    if(DataBuffer.BytePtr != NULL)
        CASC_FREE(DataBuffer.BytePtr);
    *this = Source;
}

// HOTS: 1957090 (SetDwordsValid)
// HOTS: 19570B0 (SetTripletsValid)
// HOTS: 19570D0 (? SetBitsValid ?)
// HOTS: 19570F0 (SetNameFragmentsValid)
int TGenericArray::SetArrayValid()
{
    if(bIsValidArray != 0)
        return 1;

    bIsValidArray = true;
    return ERROR_SUCCESS;
}


// HOTS: 19575A0
void TGenericArray::SetMaxItems_CHARS(DWORD NewMaxItemCount)
{
    VARIANT_POINTER OldDataBuffer = DataBuffer;
    VARIANT_POINTER NewDataBuffer;

    // Allocate new data buffer
    NewDataBuffer.CharPtr = CASC_ALLOC(char, NewMaxItemCount);
    if(NewDataBuffer.CharPtr != NULL)
    {
        // Copy the old items to the buffer
        for(DWORD i = 0; i < ItemCount; i++)
        {
            NewDataBuffer.CharPtr[i] = FirstValid.CharPtr[i];
        }
    }

    DataBuffer = NewDataBuffer;
    FirstValid = NewDataBuffer;
    Array = NewDataBuffer;
    MaxItemCount = NewMaxItemCount;
    CASC_FREE(OldDataBuffer.CharPtr);
}

// HOTS: 1957600
void TGenericArray::SetMaxItems_PATH_STOP(DWORD NewMaxItemCount)
{
    VARIANT_POINTER OldDataBuffer = DataBuffer;
    VARIANT_POINTER NewDataBuffer;

    // Allocate new data buffer
    NewDataBuffer.PathStopPtr = CASC_ALLOC(PATH_STOP, NewMaxItemCount);
    if(NewDataBuffer.PathStopPtr != NULL)
    {
        // Copy the old items to the buffer
        for(DWORD i = 0; i < ItemCount; i++)
        {
            NewDataBuffer.PathStopPtr[i] = FirstValid.PathStopPtr[i];
        }
    }

    DataBuffer = NewDataBuffer;
    FirstValid = NewDataBuffer;
    Array = NewDataBuffer;
    MaxItemCount = NewMaxItemCount;
    CASC_FREE(OldDataBuffer.PathStopPtr);
}

// HOTS: inline
void TGenericArray::InsertOneItem_CHAR(char NewItem)
{
    DWORD NewMaxItemCount;
    DWORD NewItemCount;

    NewItemCount = ItemCount + 1;
    if(NewItemCount > MaxItemCount)
    {
        NewMaxItemCount = NewItemCount;

        if(MaxItemCount > (NewItemCount / 2))
        {
            if(MaxItemCount <= (CASC_MAX_ENTRIES(BYTE) / 2))
                NewMaxItemCount = MaxItemCount + MaxItemCount;
            else
                NewMaxItemCount = CASC_MAX_ENTRIES(BYTE);
        }

        SetMaxItems_CHARS(NewMaxItemCount);
    }

    // Put the character to the slot that has been reserved
    FirstValid.CharPtr[ItemCount++] = NewItem;
}

// HOTS: 1958330, inline
void TGenericArray::InsertOneItem_PATH_STOP(PATH_STOP & NewItem)
{
    DWORD NewMaxItemCount;
    DWORD NewItemCount;

    NewItemCount = ItemCount + 1;
    if(NewItemCount > MaxItemCount)
    {
        NewMaxItemCount = NewItemCount;

        if(MaxItemCount > (NewItemCount / 2))
        {
            if(MaxItemCount <= (CASC_MAX_ENTRIES(PATH_STOP) / 2))
                NewMaxItemCount = MaxItemCount + MaxItemCount;
            else
                NewMaxItemCount = CASC_MAX_ENTRIES(PATH_STOP);
        }

        SetMaxItems_PATH_STOP(NewMaxItemCount);
    }

    // Put the structure to the slot that has been reserved
    FirstValid.PathStopPtr[ItemCount++] = NewItem;
}

// HOTS: 19583A0
void TGenericArray::sub_19583A0(DWORD NewItemCount)
{
    DWORD OldMaxItemCount = MaxItemCount;

    if(NewItemCount > MaxItemCount)
    {
        DWORD NewMaxItemCount = NewItemCount;
        
        if(MaxItemCount > (NewItemCount / 2))
        {
            if(MaxItemCount <= (CASC_MAX_ENTRIES(PATH_STOP) / 2))
                NewMaxItemCount = MaxItemCount + MaxItemCount;
            else
                NewMaxItemCount = CASC_MAX_ENTRIES(PATH_STOP);
        }

        SetMaxItems_PATH_STOP(NewMaxItemCount);
    }

    // Initialize the newly inserted items
    for(DWORD i = OldMaxItemCount; i < NewItemCount; i++)
    {
        FirstValid.PathStopPtr[i].HashValue = 0;
        FirstValid.PathStopPtr[i].field_4   = 0;
        FirstValid.PathStopPtr[i].field_8   = 0;
        FirstValid.PathStopPtr[i].field_C   = 0xFFFFFFFF;
        FirstValid.PathStopPtr[i].field_10  = 0xFFFFFFFF;
    }

    ItemCount = NewItemCount;
}

// HOTS: 1957440
int TGenericArray::LoadDwordsArray(TByteStream & InStream)
{
    DWORD NumberOfBytes;
    int nError;

    // Get and verify the number of items
    nError = InStream.GetValue_ItemCount(NumberOfBytes, ItemCount, sizeof(DWORD));
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.GetArray_DWORDs(&Array, ItemCount);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.SkipBytes((0 - (DWORD)NumberOfBytes) & 0x07);
    if(nError != ERROR_SUCCESS)
        return nError;

    return SetArrayValid();
}

// HOTS: 19574E0
int TGenericArray::LoadTripletsArray(TByteStream & InStream)
{
    DWORD NumberOfBytes;
    int nError;

    // Get and verify the number of items
    nError = InStream.GetValue_ItemCount(NumberOfBytes, ItemCount, sizeof(TRIPLET));
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.GetArray_Triplets(&Array, ItemCount);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.SkipBytes((0 - (DWORD)NumberOfBytes) & 0x07);
    if(nError != ERROR_SUCCESS)
        return nError;

    return SetArrayValid();
}

// HOTS: 1957690
int TGenericArray::LoadByteArray(TByteStream & InStream)
{
    DWORD NumberOfBytes;
    int nError;

    // Get and verify the number of items
    nError = InStream.GetValue_ItemCount(NumberOfBytes, ItemCount, sizeof(BYTE));
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.GetArray_BYTES(&Array, ItemCount);
    if(nError != ERROR_SUCCESS)
        return nError;
    
    nError = InStream.SkipBytes((0 - (DWORD)NumberOfBytes) & 0x07);
    if(nError != ERROR_SUCCESS)
        return nError;

    return SetArrayValid();
}

// HOTS: 1957700
int TGenericArray::LoadFragmentInfos(TByteStream & InStream)
{
    DWORD NumberOfBytes;
    int nError;

    // Get and verify the number of items
    nError = InStream.GetValue_ItemCount(NumberOfBytes, ItemCount, sizeof(NAME_ENTRY));
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.GetArray_NameTable(&Array, ItemCount);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.SkipBytes((0 - (DWORD)NumberOfBytes) & 0x07);
    if(nError != ERROR_SUCCESS)
        return nError;

    return SetArrayValid();
}

// HOTS: 195A220
int TGenericArray::LoadStrings(TByteStream & InStream)
{
    DWORD NumberOfBytes;
    int nError;

    // Get and verify the number of items
    nError = InStream.GetValue_ItemCount(NumberOfBytes, ItemCount, sizeof(char));
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.GetArray_BYTES(&Array, ItemCount);
    if(nError != ERROR_SUCCESS)
        return nError;
    
    nError = InStream.SkipBytes((0 - (DWORD)NumberOfBytes) & 0x07);
    if(nError != ERROR_SUCCESS)
        return nError;

    return SetArrayValid();
}

// HOTS: 19581C0
int TGenericArray::LoadDwordsArray_Copy(TByteStream & InStream)
{
    TGenericArray TempArray;
    int nError;

    nError = TempArray.LoadDwordsArray(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    CopyFrom(TempArray);
    return ERROR_SUCCESS;
}

// HOTS: 1958250
int TGenericArray::LoadTripletsArray_Copy(TByteStream & InStream)
{
    TGenericArray TempArray;
    int nError;

    nError = TempArray.LoadTripletsArray(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    CopyFrom(TempArray);
    return ERROR_SUCCESS;
}

// HOTS: 1958420
int TGenericArray::LoadBytes_Copy(TByteStream & InStream)
{
    TGenericArray TempArray;
    int nError;

    nError = TempArray.LoadByteArray(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    CopyFrom(TempArray);
    return 0;
}

// HOTS: 19584F0
int TGenericArray::LoadFragmentInfos_Copy(TByteStream & InStream)
{
    TGenericArray TempArray;
    int nError;

    nError = TempArray.LoadFragmentInfos(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    CopyFrom(TempArray);
    return ERROR_SUCCESS;
}

// HOTS: 195A360
int TGenericArray::LoadStringsWithCopy(TByteStream & InStream)
{
    TGenericArray TempArray;
    int nError;

    nError = TempArray.LoadStrings(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    CopyFrom(TempArray);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// TBitEntryArray functions

TBitEntryArray::TBitEntryArray()
{
    BitsPerEntry = 0;
    EntryBitMask = 0;
    TotalEntries = 0;
}

TBitEntryArray::~TBitEntryArray()
{}

// HOTS: 01957D20
void TBitEntryArray::ExchangeWith(TBitEntryArray & Target)
{
    TBitEntryArray WorkBuff;

    WorkBuff = *this;
    *this    = Target;
    Target   = WorkBuff;
}

// HOTS: 1958580
int TBitEntryArray::LoadFromStream(TByteStream & InStream)
{
    VARIANT_POINTER Pointer;
    ULONGLONG Value = 0;
    int nError;

    nError = LoadDwordsArray_Copy(InStream);    
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.GetBytes(sizeof(DWORD), &Pointer);
    if(nError != ERROR_SUCCESS)
        return nError;
    
    BitsPerEntry = Pointer.DwordPtr[0];
    if(BitsPerEntry > 0x20)
        return ERROR_BAD_FORMAT;

    nError = InStream.GetBytes(sizeof(DWORD), &Pointer);
    if(nError != ERROR_SUCCESS)
        return nError;
    EntryBitMask = Pointer.DwordPtr[0];

    nError = InStream.GetBytes(sizeof(ULONGLONG), &Pointer);
    if(nError == ERROR_SUCCESS)
        Value = Pointer.Int64Ptr[0];
    if(Value > 0xFFFFFFFF)
        return ERROR_BAD_FORMAT;
    TotalEntries = (DWORD)Value;

    assert((BitsPerEntry * TotalEntries) / 32 <= ItemCount);
    return ERROR_SUCCESS;
}

// HOTS: 1959300
int TBitEntryArray::LoadFromStream_Exchange(TByteStream & InStream)
{
    TBitEntryArray TempArray;
    int nError;

    nError = TempArray.LoadFromStream(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    ExchangeWith(TempArray);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// TStruct40 functions

TStruct40::TStruct40()
{
    HashValue   = 0;
    CharIndex   = 0;
    ItemCount   = 0;
    SearchPhase = CASC_SEARCH_INITIALIZING;
}

// HOTS: 19586B0
void TStruct40::sub_19586B0()
{
    DWORD NewMaxItemCount;

    array_00.ItemCount = 0;
    
    // HOTS: 19586BD
    if(array_00.MaxItemCount < 0x40)
    {
        // HOTS: 19586C2
        NewMaxItemCount = 0x40;
        
        if(array_00.MaxItemCount > 0x20)
        {
            if(array_00.MaxItemCount <= 0x7FFFFFFF)
                NewMaxItemCount = array_00.MaxItemCount + array_00.MaxItemCount;
            else
                NewMaxItemCount = CASC_MAX_ENTRIES(BYTE);
        }

        array_00.SetMaxItems_CHARS(NewMaxItemCount);
    }

    // HOTS: 19586E1
    // Set the new item count
    PathStops.sub_19583A0(0);

    if(PathStops.MaxItemCount < 4)
    {
        // HOTS: 19586F2
        NewMaxItemCount = 4;

        // HOTS: 19586EA
        if(PathStops.MaxItemCount > 2)
        {
            if(PathStops.MaxItemCount <= 0x6666666)
                NewMaxItemCount = PathStops.MaxItemCount + PathStops.MaxItemCount;
            else
                NewMaxItemCount = CASC_MAX_ENTRIES(PATH_STOP);
        }
                
        // HOTS: 195870B
        PathStops.SetMaxItems_PATH_STOP(NewMaxItemCount);
    }

    HashValue = 0;
    CharIndex = 0;
    ItemCount = 0;
    SearchPhase = CASC_SEARCH_SEARCHING;
}

//-----------------------------------------------------------------------------
// TStruct68 functions

TStruct68::TStruct68()
{
    TotalItemCount = 0;
    PresentItems = 0;
}

// HOTS: 1957DA0
void TStruct68::ExchangeWith(TStruct68 & Target)
{
    TStruct68 WorkBuff;

    WorkBuff = *this;
    *this    = Target;
    Target   = WorkBuff;
}

// HOTS: 1958630
int TStruct68::LoadFromStream(TByteStream & InStream)
{
    VARIANT_POINTER Pointer;
    int nError;

    nError = ItemBits.LoadDwordsArray_Copy(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.GetBytes(sizeof(DWORD), &Pointer);
    if(nError != ERROR_SUCCESS)
        return nError;
    TotalItemCount = Pointer.DwordPtr[0];

    nError = InStream.GetBytes(sizeof(DWORD), &Pointer);
    if(nError != ERROR_SUCCESS)
        return nError;
    PresentItems = Pointer.DwordPtr[0];
    
    if(PresentItems > TotalItemCount)
        return ERROR_FILE_CORRUPT;

    nError = ArrayTriplets_20.LoadTripletsArray_Copy(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = ArrayDwords_38.LoadDwordsArray_Copy(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = ArrayDwords_50.LoadDwordsArray_Copy(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    return ERROR_SUCCESS;
}

// HOTS: 1959380
int TStruct68::LoadFromStream_Exchange(TByteStream & InStream)
{
    TStruct68 NewStruct68;
    int nError;

    nError = NewStruct68.LoadFromStream(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;
              
    ExchangeWith(NewStruct68);
    return ERROR_SUCCESS;
}

// HOTS: 1959B60
DWORD TStruct68::GetExtraBitsIndex(DWORD Low8BitIndex)
{
    PTRIPLET pTriplet;
    DWORD VarBitsValue = 0;
    DWORD DwordIndex;
    DWORD BaseValue;
    DWORD eax, ecx;

    // 
    // Divide the low-8-bits index to four parts:
    //
    // |-----------------------|---|------|-----|
    // |       A (23 bits)     | B |   C  |  D  |
    // |-----------------------|---|------|-----|
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
    
    // Upper 23 bits contain index to the table
    pTriplet = ArrayTriplets_20.Array.TripletPtr + (Low8BitIndex >> 0x09);

    // Next 3 bits contain the index to the VBR
    switch(((Low8BitIndex >> 0x06) & 0x07) - 1)
    {
        case 0:     // Take the 1st VBR value (7 bits)
            VarBitsValue = (pTriplet->Value2 & 0x7F);
            break;

        case 1:     // Take the 2nd VBR value (8 bits)
            VarBitsValue = (pTriplet->Value2 >> 0x07) & 0xFF;
            break;

        case 2:     // Take the 3rd VBR value (8 bits)
            VarBitsValue = (pTriplet->Value2 >> 0x0F) & 0xFF;
            break;

        case 3:     // Take the 4th VBR value (9 bits)
            VarBitsValue = (pTriplet->Value2 >> 0x17);
            break;

        case 4:     // Take the 5th VBR value (9 bits)
            VarBitsValue = (pTriplet->Value3 & 0x1FF);
            break;

        case 5:     // Take the 6th VBR value (9 bits)
            VarBitsValue = (pTriplet->Value3 >> 0x09) & 0x1FF;
            break;

        case 6:     // Take the 7th VBR value (9 bits)
            VarBitsValue = (pTriplet->Value3 >> 0x12) & 0x1FF;
            break;
    }

    // Add the VBR value to the base
    BaseValue = pTriplet->BaseValue + VarBitsValue;

    // Get the DWORD index
    DwordIndex = (Low8BitIndex >> 0x05);

    // If the DWORD index is odd, we need to add the number of set bits in the previous DWORD
    if(DwordIndex & 0x01)
        BaseValue += GetNumberOfSetBits(ItemBits.Array.DwordPtr[DwordIndex - 1]) >> 0x18;

    // Calculate number of set bits in the value
    ecx = ItemBits.Array.DwordPtr[DwordIndex] & ((1 << (Low8BitIndex & 0x1F)) - 1);
    eax = GetNumberOfSetBits(ecx) >> 0x18;
    return (BaseValue + eax);
}


//-----------------------------------------------------------------------------
// TNameIndexStruct functions

// HOTS: 0195A290
TNameIndexStruct::TNameIndexStruct()
{}

// HOTS: inlined
TNameIndexStruct::~TNameIndexStruct()
{}

// HOTS: 195A180
bool TNameIndexStruct::CheckNameFragment(TMndxFindResult * pStruct1C, DWORD dwDistance)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    const char * szPathFragment;
    const char * szSearchMask;

    if(!Struct68.TotalItemCount)
    {
        // Get the offset of the fragment to compare. For convenience with pStruct40->CharIndex,
        // subtract the CharIndex from the fragment offset
        szPathFragment = (const char *)(NameFragments.Array.CharPtr + dwDistance - pStruct40->CharIndex);
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
        szPathFragment = (const char *)(NameFragments.Array.CharPtr);
        szSearchMask = pStruct1C->szSearchMask;

        // Keep searching as long as the name matches with the fragment
        while(szPathFragment[dwDistance] == szSearchMask[pStruct40->CharIndex])
        {
            // Move to the next character
            pStruct40->CharIndex++;

            // Is it the end of the fragment or end of the path?
            if(Struct68.ItemBits.IsBitSet(dwDistance++))
                return true;
            if(dwDistance >= pStruct1C->cchSearchMask)
                return false;
        }
        
        return false;
    }
}

// HOTS: 195A570
bool TNameIndexStruct::CheckAndCopyNameFragment(TMndxFindResult * pStruct1C, DWORD dwDistance)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    const char * szPathFragment;
    const char * szSearchMask;

    if(!Struct68.TotalItemCount)
    {
        // Get the offset of the fragment to compare. For convenience with pStruct40->CharIndex,
        // subtract the CharIndex from the fragment offset
        szPathFragment = (const char *)(NameFragments.Array.CharPtr + dwDistance - pStruct40->CharIndex);
        szSearchMask = pStruct1C->szSearchMask;

        // Keep copying as long as we don't reach the end of the search mask
        while(pStruct40->CharIndex < pStruct1C->cchSearchMask)
        {
            // HOTS: 195A5A0
            if(szPathFragment[pStruct40->CharIndex] != szSearchMask[pStruct40->CharIndex])
                return false;

            // HOTS: 195A5B7
            pStruct40->array_00.InsertOneItem_CHAR(szPathFragment[pStruct40->CharIndex]);
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
            pStruct40->array_00.InsertOneItem_CHAR(szPathFragment[0]);
            szPathFragment++;
        }
    }
    else
    {
        // Get the offset of the fragment to compare
        // HOTS: 195A6B7
        szPathFragment = NameFragments.Array.CharPtr;
        szSearchMask = pStruct1C->szSearchMask;

        // Keep copying as long as we don't reach the end of the search mask
        while(dwDistance < pStruct1C->cchSearchMask)
        {
            if(szPathFragment[dwDistance] != szSearchMask[pStruct40->CharIndex])
                return false;

            pStruct40->array_00.InsertOneItem_CHAR(szPathFragment[dwDistance]);
            pStruct40->CharIndex++;

            // Keep going as long as the given bit is not set
            if(Struct68.ItemBits.IsBitSet(dwDistance++))
                return true;
        }

        // Fixup the address of the fragment
        szPathFragment += dwDistance;

        // Now we need to copy the rest of the fragment
        while(Struct68.ItemBits.IsBitSet(dwDistance++) == 0)
        {
            // HOTS: 195A7A6
            pStruct40->array_00.InsertOneItem_CHAR(szPathFragment[0]);
            szPathFragment++;
        }
    }

    return true;
}

// HOTS: 195A3F0
void TNameIndexStruct::CopyNameFragment(TMndxFindResult * pStruct1C, DWORD dwDistance)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    const char * szPathFragment;

    // HOTS: 195A3FA
    if(!Struct68.TotalItemCount)
    {
        // HOTS: 195A40C
        szPathFragment = NameFragments.Array.CharPtr + dwDistance;
        while(szPathFragment[0] != 0)
        {
            // Insert the character to the path being built
            pStruct40->array_00.InsertOneItem_CHAR(*szPathFragment++);
        }
    }
    else
    {
        // HOTS: 195A4B3
        for(;;)
        {
            // Insert the character to the path being built
            pStruct40->array_00.InsertOneItem_CHAR(NameFragments.Array.CharPtr[dwDistance]);

            // Keep going as long as the given bit is not set
            if(Struct68.ItemBits.IsBitSet(dwDistance++))
                break;
        }
    }
}

// HOTS: 0195A300
void TNameIndexStruct::ExchangeWith(TNameIndexStruct & Target)
{
    TNameIndexStruct WorkBuff;

    WorkBuff = *this;
    *this    = Target;
    Target   = WorkBuff;
}

// HOTS: 0195A820
int TNameIndexStruct::LoadFromStream(TByteStream & InStream)
{
    int nError;

    nError = NameFragments.LoadStringsWithCopy(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    return Struct68.LoadFromStream_Exchange(InStream);
}

// HOTS: 195A850
int TNameIndexStruct::LoadFromStream_Exchange(TByteStream & InStream)
{
    TNameIndexStruct TempIndexStruct;
    int nError;

    nError = TempIndexStruct.LoadFromStream(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    ExchangeWith(TempIndexStruct);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// TStruct10 functions

TStruct10::TStruct10()
{
    field_0 = 0x03;
    field_4 = 0x200;
    field_8 = 0x1000;
    field_C = 0x20000;
}

// HOTS: inline
void TStruct10::CopyFrom(TStruct10 & Target)
{
    field_0 = Target.field_0;
    field_4 = Target.field_4;
    field_8 = Target.field_8;
    field_C = Target.field_C;
}

// HOTS: 1956FD0
int TStruct10::sub_1956FD0(DWORD dwBitMask)
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
int TStruct10::sub_1957050(DWORD dwBitMask)
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
int TStruct10::sub_19572E0(DWORD dwBitMask)
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
int TStruct10::sub_1957800(DWORD dwBitMask)
{
    TStruct10 TempStruct;
    int nError;

    nError = TempStruct.sub_19572E0(dwBitMask);
    if(nError != ERROR_SUCCESS)
        return nError;

    CopyFrom(TempStruct);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// TFileNameDatabase functions

// HOTS: 01958730
TFileNameDatabase::TFileNameDatabase()
{
    dwKeyMask = 0;
    field_214 = 0;
}

// HOTS: inlined
void TFileNameDatabase::ExchangeWith(TFileNameDatabase & Target)
{
    TFileNameDatabasePtr TempPtr;
    DWORD dwTemp;

    Struct68_00.ExchangeWith(Target.Struct68_00);
    Struct68_68.ExchangeWith(Target.Struct68_68);
    Struct68_D0.ExchangeWith(Target.Struct68_D0);

    FrgmDist_LoBits.ExchangeWith(Target.FrgmDist_LoBits);
    FrgmDist_HiBits.ExchangeWith(Target.FrgmDist_HiBits);

    IndexStruct_174.ExchangeWith(Target.IndexStruct_174);

    TempPtr = NextDB;
    NextDB = Target.NextDB;
    Target.NextDB = TempPtr;

    NameTable.ExchangeWith(Target.NameTable);

    dwTemp = dwKeyMask;
    dwKeyMask = Target.dwKeyMask;
    Target.dwKeyMask = dwTemp;

    dwTemp = field_214;
    field_214 = Target.field_214;
    Target.field_214 = dwTemp;

    Struct10.CopyFrom(Target.Struct10);
}

// HOTS: 1959CB0
DWORD TFileNameDatabase::sub_1959CB0(DWORD dwHashValue)
{
    PTRIPLET pTriplet;
    DWORD dwKeyShifted = (dwHashValue >> 9);
    DWORD eax, ebx, ecx, edx, esi, edi;

    // If lower 9 is zero
    edx = dwHashValue;
    if((edx & 0x1FF) == 0)
        return Struct68_00.ArrayDwords_38.Array.DwordPtr[dwKeyShifted];

    eax = Struct68_00.ArrayDwords_38.Array.DwordPtr[dwKeyShifted] >> 9;
    esi = (Struct68_00.ArrayDwords_38.Array.DwordPtr[dwKeyShifted + 1] + 0x1FF) >> 9;
    dwHashValue = esi;
    
    if((eax + 0x0A) >= esi)
    {
        // HOTS: 1959CF7
        pTriplet = Struct68_00.ArrayTriplets_20.Array.TripletPtr + eax + 1;
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
            // ecx = Struct68_00.ArrayTriplets_20.Array.TripletPtr;
            esi = (esi + eax) >> 1;
            ebx = (esi << 0x09) - Struct68_00.ArrayTriplets_20.Array.TripletPtr[esi].BaseValue;
            if(edx < ebx)
            {
                // HOTS: 01959D4B
                dwHashValue = esi;
            }
            else
            {
                // HOTS: 1959D50
                eax = esi;
                esi = dwHashValue;
            }
        }
    }

    // HOTS: 1959D5F
    pTriplet = Struct68_00.ArrayTriplets_20.Array.TripletPtr + eax;
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
    ecx = ~Struct68_00.ItemBits.Array.DwordPtr[edi];
    eax = GetNumberOfSetBits(ecx);
    esi = eax >> 0x18;

    if(edx >= esi)
    {
        // HOTS: 1959ea4
        ecx = ~Struct68_00.ItemBits.Array.DwordPtr[++edi];
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

    // BUGBUG: Possible buffer overflow here. Happens when dwHashValue >= 0x9C.
    // The same happens in Heroes of the Storm (build 29049), so I am not sure
    // if this is a bug or a case that never happens
    assert((ecx + edx) < sizeof(table_1BA1818));
    return table_1BA1818[ecx + edx] + edi;
}

DWORD TFileNameDatabase::sub_1959F50(DWORD arg_0)
{
    PTRIPLET pTriplet;
    PDWORD ItemArray;
    DWORD eax, ebx, ecx, edx, esi, edi;

    edx = arg_0;
    eax = arg_0 >> 0x09;
    if((arg_0 & 0x1FF) == 0)
        return Struct68_00.ArrayDwords_50.Array.DwordPtr[eax];

    ItemArray = Struct68_00.ArrayDwords_50.Array.DwordPtr + eax;
    eax = (ItemArray[0] >> 0x09);
    edi = (ItemArray[1] + 0x1FF) >> 0x09;
    
    if((eax + 0x0A) > edi)
    {
        // HOTS: 01959F94
        pTriplet = Struct68_00.ArrayTriplets_20.Array.TripletPtr + eax + 1;
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
            if(edx < Struct68_00.ArrayTriplets_20.Array.TripletPtr[esi].BaseValue)
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
    pTriplet = Struct68_00.ArrayTriplets_20.Array.TripletPtr + eax;
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
    esi = Struct68_00.ItemBits.Array.DwordPtr[edi];
    eax = GetNumberOfSetBits(esi);
    ecx = eax >> 0x18;

    if(edx >= ecx)
    {
        // HOTS: 195A0B2
        esi = Struct68_00.ItemBits.Array.DwordPtr[++edi];
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
bool TFileNameDatabase::sub_1957970(TMndxFindResult * pStruct1C)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    LPBYTE pbPathName = (LPBYTE)pStruct1C->szSearchMask;
    DWORD ExtraBitsIndex;
    DWORD SaveCharIndex;
    DWORD HashValue;
    DWORD ItemIndex;
    DWORD BitIndex;
    DWORD Distance;

    // Calculate hash value and index to the hash table
    HashValue = (pStruct40->HashValue << 0x05) ^ pStruct40->HashValue ^ pbPathName[pStruct40->CharIndex];
    ItemIndex = HashValue & dwKeyMask;
    
    // Does the hash value match?
    if(pStruct40->HashValue == NameTable.Array.NameEntryPtr[ItemIndex].HashValue)
    {
        // If the upper 24 bits are set, then we found a single character match
        // and we move on to the next hash value
        if(NameTable.IsSingleCharMatch(ItemIndex))
        {
            pStruct40->HashValue = NameTable.Array.NameEntryPtr[ItemIndex].NextHash;
            pStruct40->CharIndex++;
            return true;
        }

        // If the upper 24 bits are not set, we found a fragment match
        if(NextDB.pDB != NULL)
        {
            if(!NextDB.pDB->sub_1957B80(pStruct1C, NameTable.Array.NameEntryPtr[ItemIndex].Distance))
                return false;
        }
        else
        {
            if(!IndexStruct_174.CheckNameFragment(pStruct1C, NameTable.Array.NameEntryPtr[ItemIndex].Distance))
                return false;
        }

        pStruct40->HashValue = NameTable.Array.NameEntryPtr[ItemIndex].NextHash;
        return true;
    }

    //
    // Conflict: Multiple hashes give the same table index
    //

    // HOTS: 1957A0E
    BitIndex = sub_1959CB0(pStruct40->HashValue) + 1;
    if(!Struct68_00.ItemBits.IsBitSet(BitIndex))
        return false;

    pStruct40->HashValue = (BitIndex - pStruct40->HashValue - 1);
    ExtraBitsIndex = 0xFFFFFFFF;

    //= Debug Code - Delete !!! ==================================
/*
    {
        FILE * fp = fopen("E:\\extra_bits.txt", "wt");

        for(HashValue = 0; HashValue < 0x80; HashValue++)
        {
            DWORD ExtraBitIndex = 0;
            DWORD BitValue; 

            BitValue = (Struct68_68.ItemBits.IsBitSet(HashValue) ? 1 : 0);
            if(BitValue)
            {
                ExtraBitIndex = Struct68_68.GetExtraBitsIndex(HashValue);
            }

            fprintf(fp, "%02X  %02X  %04X\n", HashValue+1, BitValue, ExtraBitIndex);
        }

        fclose(fp);
    }
*/
    //============================================================

    // HOTS: 1957A41:
    for(;;)
    {
        // HOTS: 1957A41
        // Check if the low 8 bits if the fragment distance contain a single character
        // or an offset to a name fragment 
        if(Struct68_D0.ItemBits.IsBitSet(pStruct40->HashValue))
        {
            if(ExtraBitsIndex == 0xFFFFFFFF)
            {
                // HOTS: 1957A6C
                ExtraBitsIndex = Struct68_D0.GetExtraBitsIndex(pStruct40->HashValue);
            }
            else
            {
                // HOTS: 1957A7F
                ExtraBitsIndex++;
            }

            // HOTS: 1957A83
            SaveCharIndex = pStruct40->CharIndex;
            
            // Get the name fragment distance as combined value from lower 8 bits and upper bits
            Distance = GetNameFragmentDistanceEx(pStruct40->HashValue, ExtraBitsIndex);

            // Compare the string with the fragment name database
            if(NextDB.pDB != NULL)
            {
                // HOTS: 1957AEC
                if(NextDB.pDB->sub_1957B80(pStruct1C, Distance))
                    return true;
            }
            else
            {
                // HOTS: 1957AF7
                if(IndexStruct_174.CheckNameFragment(pStruct1C, Distance))
                    return true;
            }

            // HOTS: 1957B0E
            if(pStruct40->CharIndex != SaveCharIndex)
                return false;
        }
        else
        {
            // HOTS: 1957B1C
            if(FrgmDist_LoBits.Array.BytePtr[pStruct40->HashValue] == pStruct1C->szSearchMask[pStruct40->CharIndex])
            {
                pStruct40->CharIndex++;
                return true;
            }
        }

        // HOTS: 1957B32
        pStruct40->HashValue++;
        BitIndex++;

        if(!Struct68_00.ItemBits.IsBitSet(BitIndex))
            break;
    }

    return false;
}

// HOTS: 1957B80
bool TFileNameDatabase::sub_1957B80(TMndxFindResult * pStruct1C, DWORD arg_4)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    PNAME_ENTRY pNameEntry;
    DWORD Distance;
    DWORD eax, edi;

    edi = arg_4;

    // HOTS: 1957B95
    for(;;)
    {
        pNameEntry = NameTable.Array.NameEntryPtr + (edi & dwKeyMask);
        if(edi == pNameEntry->NextHash)
        {
            // HOTS: 01957BB4
            if((pNameEntry->Distance & 0xFFFFFF00) != 0xFFFFFF00)
            {
                // HOTS: 1957BC7
                if(NextDB.pDB != NULL)
                {
                    // HOTS: 1957BD3
                    if(!NextDB.pDB->sub_1957B80(pStruct1C, pNameEntry->Distance))
                        return false;
                }
                else
                {
                    // HOTS: 1957BE0
                    if(!IndexStruct_174.CheckNameFragment(pStruct1C, pNameEntry->Distance))
                        return false;
                }
            }
            else
            {
                // HOTS: 1957BEE
                if(pStruct1C->szSearchMask[pStruct40->CharIndex] != (char)pNameEntry->Distance)
                    return false;
                pStruct40->CharIndex++;
            }

            // HOTS: 1957C05
            edi = pNameEntry->HashValue;
            if(edi == 0) 
                return true;

            if(pStruct40->CharIndex >= pStruct1C->cchSearchMask)
                return false;
        }
        else
        {
            // HOTS: 1957C30
            if(Struct68_D0.ItemBits.IsBitSet(edi))
            {
                // HOTS: 1957C4C
                if(NextDB.pDB != NULL)
                {
                    // HOTS: 1957C58
                    Distance = GetNameFragmentDistance(edi);
                    if(!NextDB.pDB->sub_1957B80(pStruct1C, Distance))
                        return false;
                }
                else
                {
                    // HOTS: 1957350
                    Distance = GetNameFragmentDistance(edi);
                    if(!IndexStruct_174.CheckNameFragment(pStruct1C, Distance))
                        return false;
                }
            }
            else
            {
                // HOTS: 1957C8E
                if(FrgmDist_LoBits.Array.BytePtr[edi] != pStruct1C->szSearchMask[pStruct40->CharIndex])
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
void TFileNameDatabase::sub_1958D70(TMndxFindResult * pStruct1C, DWORD arg_4)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    PNAME_ENTRY pNameEntry;

    // HOTS: 1958D84
    for(;;)
    {
        pNameEntry = NameTable.Array.NameEntryPtr + (arg_4 & dwKeyMask);
        if(arg_4 == pNameEntry->NextHash)
        {
            // HOTS: 1958DA6
            if((pNameEntry->Distance & 0xFFFFFF00) != 0xFFFFFF00)
            {
                // HOTS: 1958DBA
                if(NextDB.pDB != NULL)
                {
                    NextDB.pDB->sub_1958D70(pStruct1C, pNameEntry->Distance);
                }
                else
                {
                    IndexStruct_174.CopyNameFragment(pStruct1C, pNameEntry->Distance);
                }
            }
            else
            {
                // HOTS: 1958DE7
                // Insert the low 8 bits to the path being built
                pStruct40->array_00.InsertOneItem_CHAR((char)(pNameEntry->Distance & 0xFF));
            }

            // HOTS: 1958E71
            arg_4 = pNameEntry->HashValue;
            if(arg_4 == 0)
                return;
        }
        else
        {
            // HOTS: 1958E8E
            if(Struct68_D0.ItemBits.IsBitSet(arg_4))
            {
                DWORD Distance;

                // HOTS: 1958EAF
                Distance = GetNameFragmentDistance(arg_4);
                if(NextDB.pDB != NULL)
                {
                    NextDB.pDB->sub_1958D70(pStruct1C, Distance);
                }
                else
                {
                    IndexStruct_174.CopyNameFragment(pStruct1C, Distance);
                }
            }
            else
            {
                // HOTS: 1958F50
                // Insert one character to the path being built
                pStruct40->array_00.InsertOneItem_CHAR(FrgmDist_LoBits.Array.CharPtr[arg_4]);
            }

            // HOTS: 1958FDE
            if(arg_4 <= field_214)
                return;

            arg_4 = 0xFFFFFFFF - arg_4 + sub_1959F50(arg_4);
        }
    }
}

// HOTS: 1959010
bool TFileNameDatabase::sub_1959010(TMndxFindResult * pStruct1C, DWORD arg_4)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    PNAME_ENTRY pNameEntry;

    // HOTS: 1959024
    for(;;)
    {
        pNameEntry = NameTable.Array.NameEntryPtr + (arg_4 & dwKeyMask);
        if(arg_4 == pNameEntry->NextHash)
        {
            // HOTS: 1959047
            if((pNameEntry->Distance & 0xFFFFFF00) != 0xFFFFFF00)
            {
                // HOTS: 195905A
                if(NextDB.pDB != NULL)
                {
                    if(!NextDB.pDB->sub_1959010(pStruct1C, pNameEntry->Distance))
                        return false;
                }
                else
                {
                    if(!IndexStruct_174.CheckAndCopyNameFragment(pStruct1C, pNameEntry->Distance))
                        return false;
                }
            }
            else
            {
                // HOTS: 1959092
                if((char)(pNameEntry->Distance & 0xFF) != pStruct1C->szSearchMask[pStruct40->CharIndex])
                    return false;

                // Insert the low 8 bits to the path being built
                pStruct40->array_00.InsertOneItem_CHAR((char)(pNameEntry->Distance & 0xFF));
                pStruct40->CharIndex++;
            }

            // HOTS: 195912E
            arg_4 = pNameEntry->HashValue;
            if(arg_4 == 0)
                return true;
        }
        else
        {
            // HOTS: 1959147
            if(Struct68_D0.ItemBits.IsBitSet(arg_4))
            {
                DWORD Distance;

                // HOTS: 195917C
                Distance = GetNameFragmentDistance(arg_4);
                if(NextDB.pDB != NULL)
                {
                    if(!NextDB.pDB->sub_1959010(pStruct1C, Distance))
                        return false;
                }
                else
                {
                    if(!IndexStruct_174.CheckAndCopyNameFragment(pStruct1C, Distance))
                        return false;
                }
            }
            else
            {
                // HOTS: 195920E
                if(FrgmDist_LoBits.Array.CharPtr[arg_4] != pStruct1C->szSearchMask[pStruct40->CharIndex])
                    return false;

                // Insert one character to the path being built
                pStruct40->array_00.InsertOneItem_CHAR(FrgmDist_LoBits.Array.CharPtr[arg_4]);
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
bool TFileNameDatabase::sub_1959460(TMndxFindResult * pStruct1C)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    PPATH_STOP pPathStop;
    PATH_STOP PathStop;
    DWORD NewMaxItemCount;
    DWORD Distance;
    DWORD ebx, edi;

    if(pStruct40->SearchPhase == CASC_SEARCH_FINISHED)
        return false;

    if(pStruct40->SearchPhase != CASC_SEARCH_SEARCHING)
    {
        // HOTS: 1959489
        pStruct40->sub_19586B0();

        while(pStruct40->CharIndex < pStruct1C->cchSearchMask)
        {
            if(!sub_1958B00(pStruct1C))
            {
                pStruct40->SearchPhase = CASC_SEARCH_FINISHED;
                return false;
            }
        }

        // HOTS: 19594b0
        PathStop.HashValue = pStruct40->HashValue;
        PathStop.field_4   = 0;
        PathStop.field_8   = pStruct40->array_00.ItemCount;
        PathStop.field_C   = 0xFFFFFFFF;
        PathStop.field_10  = 0xFFFFFFFF;
        pStruct40->PathStops.InsertOneItem_PATH_STOP(PathStop);
        pStruct40->ItemCount = 1;

        if(Struct68_68.ItemBits.IsBitSet(pStruct40->HashValue))
        {
            pStruct1C->szFoundPath  = (char *)pStruct40->array_00.FirstValid.CharPtr;
            pStruct1C->cchFoundPath = pStruct40->array_00.ItemCount;
            pStruct1C->MndxIndex = Struct68_68.GetExtraBitsIndex(pStruct40->HashValue);
            return true;
        }
    }

    // HOTS: 1959522
    for(;;)
    {
        // HOTS: 1959530
        if(pStruct40->ItemCount == pStruct40->PathStops.ItemCount)
        {
            PPATH_STOP pLastStop;
            DWORD field_4;
            
            pLastStop = pStruct40->PathStops.FirstValid.PathStopPtr + pStruct40->PathStops.ItemCount - 1;
            field_4 = sub_1959CB0(pLastStop->HashValue) + 1;
            ebx = field_4 - pLastStop->HashValue - 1;

            // Insert a new structure
            PathStop.HashValue = ebx;
            PathStop.field_4   = field_4;
            PathStop.field_8   = 0;
            PathStop.field_C   = 0xFFFFFFFF;
            PathStop.field_10  = 0xFFFFFFFF;
            pStruct40->PathStops.InsertOneItem_PATH_STOP(PathStop);
        }

        // HOTS: 19595BD
        pPathStop = pStruct40->PathStops.FirstValid.PathStopPtr + pStruct40->ItemCount;

        // HOTS: 19595CC
        if(Struct68_00.ItemBits.IsBitSet(pPathStop->field_4++))
        {
            // HOTS: 19595F2
            pStruct40->ItemCount++;

            if(Struct68_D0.ItemBits.IsBitSet(pPathStop->HashValue))
            {
                // HOTS: 1959617
                if(pPathStop->field_C == 0xFFFFFFFF)
                    pPathStop->field_C = Struct68_D0.GetExtraBitsIndex(pPathStop->HashValue);
                else
                    pPathStop->field_C++;

                // HOTS: 1959630
                Distance = GetNameFragmentDistanceEx(pPathStop->HashValue, pPathStop->field_C);
                if(NextDB.pDB != NULL)
                {
                    // HOTS: 1959649
                    NextDB.pDB->sub_1958D70(pStruct1C, Distance);
                }
                else
                {
                    // HOTS: 1959654
                    IndexStruct_174.CopyNameFragment(pStruct1C, Distance);
                }
            }
            else
            {
                // HOTS: 1959665
                // Insert one character to the path being built
                pStruct40->array_00.InsertOneItem_CHAR(FrgmDist_LoBits.Array.CharPtr[pPathStop->HashValue]);
            }

            // HOTS: 19596AE
            pPathStop->field_8 = pStruct40->array_00.ItemCount;

            // HOTS: 19596b6
            if(Struct68_68.ItemBits.IsBitSet(pPathStop->HashValue))
            {
                // HOTS: 19596D1
                if(pPathStop->field_10 == 0xFFFFFFFF)
                {
                    // HOTS: 19596D9
                    pPathStop->field_10 = Struct68_68.GetExtraBitsIndex(pPathStop->HashValue);
                }
                else
                {
                    pPathStop->field_10++;
                }

                // HOTS: 1959755
                pStruct1C->szFoundPath = pStruct40->array_00.FirstValid.CharPtr;
                pStruct1C->cchFoundPath = pStruct40->array_00.ItemCount;
                pStruct1C->MndxIndex = pPathStop->field_10;
                return true;
            }
        }
        else
        {
            // HOTS: 19596E9
            if(pStruct40->ItemCount == 1)
            {
                pStruct40->SearchPhase = CASC_SEARCH_FINISHED;
                return false;
            }

            // HOTS: 19596F5
            pPathStop = pStruct40->PathStops.FirstValid.PathStopPtr + pStruct40->ItemCount - 1;
            pPathStop->HashValue++;

            pPathStop = pStruct40->PathStops.FirstValid.PathStopPtr + pStruct40->ItemCount - 2;
            edi = pPathStop->field_8;
            
            if(edi > pStruct40->array_00.MaxItemCount)
            {
                // HOTS: 1959717
                NewMaxItemCount = edi;

                if(pStruct40->array_00.MaxItemCount > (edi / 2))
                {
                    if(pStruct40->array_00.MaxItemCount > 0x7FFFFFFF)
                    {
                        NewMaxItemCount = 0xFFFFFFFF;
                    }
                    else
                    {
                        NewMaxItemCount = pStruct40->array_00.MaxItemCount + pStruct40->array_00.MaxItemCount;
                    }
                }

                pStruct40->array_00.SetMaxItems_CHARS(NewMaxItemCount);
            }

            // HOTS: 1959749
            pStruct40->array_00.ItemCount = edi;
            pStruct40->ItemCount--;
        }
    }
}

// HOTS: 1958B00
bool TFileNameDatabase::sub_1958B00(TMndxFindResult * pStruct1C)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;
    LPBYTE pbPathName = (LPBYTE)pStruct1C->szSearchMask;
    DWORD FragmentOffset;
    DWORD SaveCharIndex;
    DWORD HashValue;
    DWORD Distance;
    DWORD BitIndex;
    DWORD var_4;

    HashValue = pbPathName[pStruct40->CharIndex] ^ (pStruct40->HashValue << 0x05) ^ pStruct40->HashValue;
    HashValue = HashValue & dwKeyMask;
    if(pStruct40->HashValue == NameTable.Array.NameEntryPtr[HashValue].HashValue)
    {
        // HOTS: 1958B45
        FragmentOffset = NameTable.Array.NameEntryPtr[HashValue].Distance;
        if((FragmentOffset & 0xFFFFFF00) == 0xFFFFFF00)
        {
            // HOTS: 1958B88
            pStruct40->array_00.InsertOneItem_CHAR((char)FragmentOffset);
            pStruct40->HashValue = NameTable.Array.NameEntryPtr[HashValue].NextHash;
            pStruct40->CharIndex++;
            return true;
        }

        // HOTS: 1958B59
        if(NextDB.pDB != NULL)
        {
            if(!NextDB.pDB->sub_1959010(pStruct1C, FragmentOffset))
                return false;
        }
        else
        {
            if(!IndexStruct_174.CheckAndCopyNameFragment(pStruct1C, FragmentOffset))
                return false;
        }

        // HOTS: 1958BCA
        pStruct40->HashValue = NameTable.Array.NameEntryPtr[HashValue].NextHash;
        return true;
    }

    // HOTS: 1958BE5
    BitIndex = sub_1959CB0(pStruct40->HashValue) + 1;
    if(!Struct68_00.ItemBits.IsBitSet(BitIndex))
        return false;

    pStruct40->HashValue = (BitIndex - pStruct40->HashValue - 1);
    var_4 = 0xFFFFFFFF;

    // HOTS: 1958C20
    for(;;)
    {
        if(Struct68_D0.ItemBits.IsBitSet(pStruct40->HashValue))
        {
            // HOTS: 1958C0E
            if(var_4 == 0xFFFFFFFF)
            {
                // HOTS: 1958C4B
                var_4 = Struct68_D0.GetExtraBitsIndex(pStruct40->HashValue);
            }
            else
            {
                var_4++;
            }

            // HOTS: 1958C62
            SaveCharIndex = pStruct40->CharIndex;
            
            Distance = GetNameFragmentDistanceEx(pStruct40->HashValue, var_4);
            if(NextDB.pDB != NULL)
            {
                // HOTS: 1958CCB
                if(NextDB.pDB->sub_1959010(pStruct1C, Distance))
                    return true;
            }
            else
            {
                // HOTS: 1958CD6
                if(IndexStruct_174.CheckAndCopyNameFragment(pStruct1C, Distance))
                    return true;
            }

            // HOTS: 1958CED
            if(SaveCharIndex != pStruct40->CharIndex)
                return false;
        }
        else
        {
            // HOTS: 1958CFB
            if(FrgmDist_LoBits.Array.BytePtr[pStruct40->HashValue] == pStruct1C->szSearchMask[pStruct40->CharIndex])
            {
                // HOTS: 1958D11
                pStruct40->array_00.InsertOneItem_CHAR(FrgmDist_LoBits.Array.BytePtr[pStruct40->HashValue]);
                pStruct40->CharIndex++;
                return true;
            }
        }

        // HOTS: 1958D11
        pStruct40->HashValue++;
        BitIndex++;

        if(!Struct68_00.ItemBits.IsBitSet(BitIndex))
            break;
    }

    return false;
}

// HOTS: 1957EF0
bool TFileNameDatabase::FindFileInDatabase(TMndxFindResult * pStruct1C)
{
    TStruct40 * pStruct40 = pStruct1C->pStruct40;

    pStruct40->HashValue = 0;
    pStruct40->CharIndex = 0;
    pStruct40->SearchPhase = CASC_SEARCH_INITIALIZING;

    if(pStruct1C->cchSearchMask > 0)
    {
        while(pStruct40->CharIndex < pStruct1C->cchSearchMask)
        {
            // HOTS: 01957F12
            if(!sub_1957970(pStruct1C))
                return false;
        }
    }

    // HOTS: 1957F26
    if(!Struct68_68.ItemBits.IsBitSet(pStruct40->HashValue))
        return false;

    pStruct1C->szFoundPath = pStruct1C->szSearchMask;
    pStruct1C->cchFoundPath = pStruct1C->cchSearchMask;

    pStruct1C->MndxIndex = Struct68_68.GetExtraBitsIndex(pStruct40->HashValue);
    return true;
}

// HOTS: 1959790
int TFileNameDatabase::LoadFromStream(TByteStream & InStream)
{
    DWORD dwBitMask;
    int nError;

    nError = Struct68_00.LoadFromStream_Exchange(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = Struct68_68.LoadFromStream_Exchange(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = Struct68_D0.LoadFromStream_Exchange(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    // HOTS: 019597CD
    nError = FrgmDist_LoBits.LoadBytes_Copy(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = FrgmDist_HiBits.LoadFromStream_Exchange(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;
    
    // HOTS: 019597F5
    nError = IndexStruct_174.LoadFromStream_Exchange(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    // HOTS: 0195980A
    if(Struct68_D0.PresentItems != 0 && IndexStruct_174.NameFragments.ItemCount == 0)
    {
        TFileNameDatabase * pNextDB = new TFileNameDatabase;

        nError = NextDB.SetDatabase(pNextDB);
        if(nError != ERROR_SUCCESS)
            return nError;

        if(NextDB.pDB == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
        
        nError = NextDB.pDB->LoadFromStream(InStream);
        if(nError != ERROR_SUCCESS)
            return nError;
    }

    // HOTS: 0195986B
    nError = NameTable.LoadFragmentInfos_Copy(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    dwKeyMask = NameTable.ItemCount - 1;

    nError = InStream.GetValue_DWORD(field_214);
    if(nError != ERROR_SUCCESS)
        return nError;

    nError = InStream.GetValue_DWORD(dwBitMask);
    if(nError != ERROR_SUCCESS)
        return nError;

    return Struct10.sub_1957800(dwBitMask);
}

// HOTS: 19598D0
int TFileNameDatabase::LoadFromStream_Exchange(TByteStream & InStream)
{
    TFileNameDatabase TempDatabase;
    VARIANT_POINTER Pointer;
    DWORD dwSignature;
    int nError;

    // Get pointer to MAR signature
    nError = InStream.GetBytes(sizeof(DWORD), &Pointer);
    if(nError != ERROR_SUCCESS)
        return nError;
    
    // Verify the signature
    dwSignature = Pointer.DwordPtr[0];
    if(dwSignature != CASC_MAR_SIGNATURE)
        return ERROR_BAD_FORMAT;

    nError = TempDatabase.LoadFromStream(InStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    MarStream.ExchangeWith(InStream);
    ExchangeWith(TempDatabase);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// TFileNameDatabasePtr functions

// HOTS: 01956D70
TFileNameDatabasePtr::TFileNameDatabasePtr()
{
    pDB = NULL;
}

TFileNameDatabasePtr::~TFileNameDatabasePtr()
{
    delete pDB;
}

// HOTS: 1956C60
int TFileNameDatabasePtr::FindFileInDatabase(TMndxFindResult * pStruct1C)
{
    int nError = ERROR_SUCCESS;

    if(pDB == NULL)
        return ERROR_INVALID_PARAMETER;

    nError = pStruct1C->CreateStruct40();
    if(nError != ERROR_SUCCESS)
        return nError;

    if(!pDB->FindFileInDatabase(pStruct1C))
        nError = ERROR_FILE_NOT_FOUND;

    pStruct1C->FreeStruct40();
    return nError;
}

// HOTS: 1956CE0
int TFileNameDatabasePtr::sub_1956CE0(TMndxFindResult * pStruct1C, bool * pbFindResult)
{
    int nError = ERROR_SUCCESS;

    if(pDB == NULL)
        return ERROR_INVALID_PARAMETER;

    // Create the pStruct40, if not initialized yet
    if(pStruct1C->pStruct40 == NULL)
    {
        nError = pStruct1C->CreateStruct40();
        if(nError != ERROR_SUCCESS)
            return nError;
    }

    *pbFindResult = pDB->sub_1959460(pStruct1C);
    return nError;
}

// HOTS: 1956D20
int TFileNameDatabasePtr::GetStruct68_68_Field1C(PDWORD ptr_var_C)
{
    if(pDB == NULL)
        return ERROR_INVALID_PARAMETER;

    *ptr_var_C = pDB->Struct68_68.PresentItems;
    return ERROR_SUCCESS;
}

// HOTS: 1956DA0
int TFileNameDatabasePtr::CreateDatabase(LPBYTE pbMarData, DWORD cbMarData)
{
    TFileNameDatabase * pDatabase;
    TByteStream ByteStream;
    int nError;

    if(pbMarData == NULL && cbMarData != 0)
        return ERROR_INVALID_PARAMETER;

    pDatabase = new TFileNameDatabase;
    if(pDatabase == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    nError = ByteStream.SetByteBuffer(pbMarData, cbMarData);
    if(nError != ERROR_SUCCESS)
        return nError;

    // HOTS: 1956E11
    nError = pDatabase->LoadFromStream_Exchange(ByteStream);
    if(nError != ERROR_SUCCESS)
        return nError;

    pDB = pDatabase;
    return ERROR_SUCCESS;
}

// HOTS: 19584B0
int TFileNameDatabasePtr::SetDatabase(TFileNameDatabase * pNewDB)
{
    if(pNewDB != NULL && pDB == pNewDB)
        return ERROR_INVALID_PARAMETER;

    if(pDB != NULL)
        delete pDB;
    pDB = pNewDB;
    return ERROR_SUCCESS;           
}

//-----------------------------------------------------------------------------
// Local functions - MAR file

// HOTS: 00E94180
static void MAR_FILE_CreateDatabase(PMAR_FILE pMarFile)
{
    pMarFile->pDatabasePtr = new TFileNameDatabasePtr;
    if(pMarFile->pDatabasePtr != NULL)
        pMarFile->pDatabasePtr->CreateDatabase(pMarFile->pbMarData, pMarFile->cbMarData);
}

static int MAR_FILE_SearchFile(PMAR_FILE pMarFile, TMndxFindResult * pStruct1C)
{
    return pMarFile->pDatabasePtr->FindFileInDatabase(pStruct1C);
}

static void MAR_FILE_Destructor(PMAR_FILE pMarFile)
{
    if(pMarFile != NULL)
    {
        if(pMarFile->pDatabasePtr != NULL)
            delete pMarFile->pDatabasePtr;
        if(pMarFile->pbMarData != NULL)
            CASC_FREE(pMarFile->pbMarData);

        CASC_FREE(pMarFile);
    }
}

//-----------------------------------------------------------------------------
// Package functions

// TODO: When working, increment these values to lower number of (re)allocations
#define CASC_PACKAGES_INIT  0x10
#define CASC_PACKAGES_DELTA 0x10

static PCASC_PACKAGES AllocatePackages(size_t nNameEntries, size_t nNameBufferMax)
{
    PCASC_PACKAGES pPackages;
    size_t cbToAllocate;

    // Allocate space
    cbToAllocate = sizeof(CASC_PACKAGES) + (nNameEntries * sizeof(CASC_PACKAGE)) + nNameBufferMax;
    pPackages = (PCASC_PACKAGES)CASC_ALLOC(BYTE, cbToAllocate);
    if(pPackages != NULL)
    {
        // Fill the structure
        memset(pPackages, 0, cbToAllocate);

        // Init the list entries
        pPackages->szNameBuffer = (char *)(&pPackages->Packages[nNameEntries]);
        pPackages->NameEntries = nNameEntries;
        pPackages->NameBufferUsed = 0;
        pPackages->NameBufferMax = nNameBufferMax;
    }

    return pPackages;
}

static PCASC_PACKAGES InsertToPackageList(
    PCASC_PACKAGES pPackages,
    const char * szFileName,
    size_t cchFileName,
    size_t nPackageIndex)
{
    size_t nNewNameEntries = pPackages->NameEntries;
    size_t nNewNameBufferMax = pPackages->NameBufferMax;
    size_t cbToAllocate;
    char * szNameBuffer;

    // Need to reallocate?
    while(nPackageIndex >= nNewNameEntries)
        nNewNameEntries = nNewNameEntries + CASC_PACKAGES_DELTA;
    if((pPackages->NameBufferUsed + cchFileName + 1) > nNewNameBufferMax)
        nNewNameBufferMax = nNewNameBufferMax + 0x1000;
    
    // If any of the two variables overflowed, we need to reallocate the name list
    if(nNewNameEntries > pPackages->NameEntries || nNewNameBufferMax > pPackages->NameBufferMax)
    {
        PCASC_PACKAGES pOldPackages = pPackages;

        // Allocate new name list
        cbToAllocate = sizeof(CASC_PACKAGES) + (nNewNameEntries * sizeof(CASC_PACKAGE)) + nNewNameBufferMax;
        pPackages = (PCASC_PACKAGES)CASC_ALLOC(BYTE, cbToAllocate);
        if(pPackages == NULL)
            return NULL;

        // Copy the old entries
        memset(pPackages, 0, cbToAllocate);
        pPackages->szNameBuffer = szNameBuffer = (char *)(&pPackages->Packages[nNewNameEntries]);
        memcpy(pPackages->szNameBuffer, pOldPackages->szNameBuffer, pOldPackages->NameBufferUsed);

        // Copy the old entries
        for(size_t i = 0; i < pOldPackages->NameEntries; i++)
        {
            if(pOldPackages->Packages[i].szFileName != NULL)
            {
                pPackages->Packages[i].szFileName = pPackages->szNameBuffer + (pOldPackages->Packages[i].szFileName - pOldPackages->szNameBuffer);
                pPackages->Packages[i].nLength = pOldPackages->Packages[i].nLength;
            }
        }

        // Fill the limits
        pPackages->NameEntries = nNewNameEntries;
        pPackages->NameBufferUsed = pOldPackages->NameBufferUsed;
        pPackages->NameBufferMax = nNewNameBufferMax;

        // Switch the name lists
        CASC_FREE(pOldPackages);
    }

    // The slot is expected to be empty at the moment
    assert(pPackages->Packages[nPackageIndex].szFileName == NULL);
    assert(pPackages->Packages[nPackageIndex].nLength == 0);

    // Set the file name entry
    szNameBuffer = pPackages->szNameBuffer + pPackages->NameBufferUsed;
    pPackages->Packages[nPackageIndex].szFileName = szNameBuffer;
    pPackages->Packages[nPackageIndex].nLength = cchFileName;
    memcpy(szNameBuffer, szFileName, cchFileName);
    pPackages->NameBufferUsed += (cchFileName + 1);
    return pPackages;
}

static int LoadPackageNames(TCascStorage * hs)
{
    TMndxFindResult Struct1C;
    PCASC_PACKAGES pPackages = NULL;
    PMAR_FILE pMarFile;

    // Sanity checks
    assert(hs->pMndxInfo != NULL);

    // Prepare the file name search in the top level directory
    pMarFile = hs->pMndxInfo->pMarFile1;
    Struct1C.SetSearchPath("", 0);

    // Allocate initial name list structure
    pPackages = AllocatePackages(CASC_PACKAGES_INIT, 0x1000);
    if(pPackages == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Keep searching as long as we find something
    for(;;)
    {
        bool bFindResult = false;

        // Search the next file name
        pMarFile->pDatabasePtr->sub_1956CE0(&Struct1C, &bFindResult);
        if(bFindResult == false)
            break;

        // Insert the found name to the top level directory list
        pPackages = InsertToPackageList(pPackages, Struct1C.szFoundPath, Struct1C.cchFoundPath, Struct1C.MndxIndex);
        if(pPackages == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
    }

    // Set the name list to the CASC storage structure
    hs->pPackages = pPackages;
    return ERROR_SUCCESS;
}

PCASC_PACKAGE FindMndxPackage(TCascStorage * hs, const char * szFileName)
{
    PCASC_PACKAGE pMatching = NULL;
    PCASC_PACKAGE pPackage;
    size_t nMaxLength = 0;
    size_t nLength = strlen(szFileName);

    // Packages must be loaded
    assert(hs->pPackages != NULL);
    pPackage = hs->pPackages->Packages;

    // Find the longest matching name
    for(size_t i = 0; i < hs->pPackages->NameEntries; i++, pPackage++)
    {
        if(pPackage->szFileName != NULL && pPackage->nLength < nLength && pPackage->nLength > nMaxLength)
        {
            // Compare the package name
            if(!strncmp(szFileName, pPackage->szFileName, pPackage->nLength))
            {
                pMatching = pPackage;
                nMaxLength = pPackage->nLength;
            }
        }
    }

    // Give the package pointer or NULL if not found
    return pMatching;
}

static bool FillFindData(TCascSearch * pSearch, PCASC_FIND_DATA pFindData, TMndxFindResult * pStruct1C)
{
    CASC_ROOT_KEY_INFO RootKeyInfo;
    TCascStorage * hs = pSearch->hs;
    PCASC_PACKAGE pPackage;
    char * szStrippedName;
    int nError;

    // Sanity check
    assert(pStruct1C->cchFoundPath < MAX_PATH);

    // Fill the file name
    memcpy(pFindData->szFileName, pStruct1C->szFoundPath, pStruct1C->cchFoundPath);
    pFindData->szFileName[pStruct1C->cchFoundPath] = 0;
    pFindData->dwFileSize = CASC_INVALID_SIZE;
    
    // Fill the file size
    pPackage = FindMndxPackage(hs, pFindData->szFileName);
    if(pPackage != NULL)
    {
        // Cut the package name off the full path
        szStrippedName = pFindData->szFileName + pPackage->nLength;
        while(szStrippedName[0] == '/')
            szStrippedName++;

        nError = SearchMndxInfo(hs->pMndxInfo, szStrippedName, (DWORD)(pPackage - hs->pPackages->Packages), &RootKeyInfo);
        if(nError == ERROR_SUCCESS)
        {
            pFindData->dwFileSize = (DWORD)RootKeyInfo.FileSize;
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
// Public functions - MNDX info

int LoadMndxRootFile(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    PFILE_MNDX_HEADER pMndxHeader = (PFILE_MNDX_HEADER)pbRootFile;
    PCASC_MNDX_INFO pMndxInfo;
    FILE_MAR_INFO MarInfo;
    PMAR_FILE pMarFile;
    LPBYTE pbRootFileEnd = pbRootFile + cbRootFile;
    DWORD cbToAllocate;
    DWORD dwFilePointer = 0;
    DWORD i;
    int nError = ERROR_SUCCESS;

    // Check signature and the other variables
    if(pMndxHeader->Signature != CASC_MNDX_SIGNATURE || pMndxHeader->FormatVersion > 2 || pMndxHeader->FormatVersion < 1)
        return ERROR_BAD_FORMAT;

    // Allocate space for the CASC_MNDX_INFO structure
    pMndxInfo = CASC_ALLOC(CASC_MNDX_INFO, 1);
    if(pMndxInfo == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Copy the header into the MNDX info
    memset(pMndxInfo, 0, sizeof(CASC_MNDX_INFO));
    pMndxInfo->HeaderVersion = pMndxHeader->HeaderVersion;
    pMndxInfo->FormatVersion = pMndxHeader->FormatVersion;
    dwFilePointer += sizeof(FILE_MNDX_HEADER);

    // Header version 2 has 2 extra fields that we need to load
    if(pMndxInfo->HeaderVersion == 2)
    {
        if(!RootFileRead(pbRootFile + dwFilePointer, pbRootFileEnd, &pMndxInfo->field_1C, sizeof(DWORD) + sizeof(DWORD)))
            return ERROR_FILE_CORRUPT;
        dwFilePointer += sizeof(DWORD) + sizeof(DWORD);
    }

    // Load the rest of the file header
    if(!RootFileRead(pbRootFile + dwFilePointer, pbRootFileEnd, &pMndxInfo->MarInfoOffset, 0x1C))
        return ERROR_FILE_CORRUPT;

    // Verify the structure
    if(pMndxInfo->MarInfoCount > CASC_MAX_MAR_FILES || pMndxInfo->MarInfoSize != sizeof(FILE_MAR_INFO))
        return ERROR_FILE_CORRUPT;

    // Load all MAR infos
    for(i = 0; i < pMndxInfo->MarInfoCount; i++)
    {
        // Load the n-th MAR info
        dwFilePointer = pMndxInfo->MarInfoOffset + (pMndxInfo->MarInfoSize * i);
        if(!RootFileRead(pbRootFile + dwFilePointer, pbRootFileEnd, &MarInfo, sizeof(FILE_MAR_INFO)))
            return ERROR_FILE_CORRUPT;

        // Allocate MAR_FILE structure
        pMarFile = CASC_ALLOC(MAR_FILE, 1);
        if(pMarFile == NULL)
        {
            nError = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }

        // Allocate space for the MAR data
        pMarFile->pDatabasePtr = NULL;
        pMarFile->pbMarData = CASC_ALLOC(BYTE, MarInfo.MarDataSize);
        pMarFile->cbMarData = MarInfo.MarDataSize;
        if(pMarFile->pbMarData == NULL)
        {
            nError = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }

        // Read the MAR data
        if(!RootFileRead(pbRootFile + MarInfo.MarDataOffset, pbRootFileEnd, pMarFile->pbMarData, pMarFile->cbMarData))
        {
            nError = ERROR_FILE_CORRUPT;
            break;
        }

        // HOTS: 00E94FF1
        MAR_FILE_CreateDatabase(pMarFile);
        if(i == 0)
            pMndxInfo->pMarFile1 = pMarFile;
        if(i == 1)
            pMndxInfo->pMarFile2 = pMarFile;
        if(i == 2)
            pMndxInfo->pMarFile3 = pMarFile;
    }

    // All three MAR files must be loaded
    // HOTS: 00E9503B
    if(nError == ERROR_SUCCESS)
    {
        if(pMndxInfo->pMarFile1 == NULL || pMndxInfo->pMarFile2 == NULL || pMndxInfo->pMarFile3 == NULL)
            nError = ERROR_BAD_FORMAT;
        if(pMndxInfo->MndxEntrySize != sizeof(CASC_MNDX_ENTRY))
            nError = ERROR_BAD_FORMAT;
    }

    // Load the complete array of MNDX entries
    if(nError == ERROR_SUCCESS)
    {
        TFileNameDatabasePtr * pDbPtr = pMndxInfo->pMarFile2->pDatabasePtr;
        DWORD var_C;

        nError = pDbPtr->GetStruct68_68_Field1C(&var_C);
        if(nError == ERROR_SUCCESS && var_C == pMndxInfo->MndxEntriesValid)
        {
            cbToAllocate = pMndxInfo->MndxEntriesTotal * pMndxInfo->MndxEntrySize;
            pMndxInfo->pMndxEntries = (PCASC_MNDX_ENTRY)CASC_ALLOC(BYTE, cbToAllocate);
            if(pMndxInfo->pMndxEntries != NULL)
            {
                if(!RootFileRead(pbRootFile + pMndxInfo->MndxEntriesOffset, pbRootFileEnd, pMndxInfo->pMndxEntries, cbToAllocate))
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
        assert(pMndxInfo->MndxEntriesValid <= pMndxInfo->MndxEntriesTotal);
        pMndxInfo->ppValidEntries = CASC_ALLOC(PCASC_MNDX_ENTRY, pMndxInfo->MndxEntriesValid + 1);
        if(pMndxInfo->ppValidEntries != NULL)
        {
            PCASC_MNDX_ENTRY pMndxEntry = pMndxInfo->pMndxEntries;
            DWORD ValidEntryCount = 1; // edx
            DWORD nIndex1 = 0;

            // The first entry is always valid
            pMndxInfo->ppValidEntries[nIndex1++] = pMndxInfo->pMndxEntries;

            // Put the remaining entries
            for(i = 0; i < pMndxInfo->MndxEntriesTotal; i++, pMndxEntry++)
            {
                if(ValidEntryCount > pMndxInfo->MndxEntriesValid)
                    break;

                if(pMndxEntry->Flags & 0x80000000)
                {
                    pMndxInfo->ppValidEntries[nIndex1++] = pMndxEntry + 1;
                    ValidEntryCount++;
                }
            }

            // Verify the final number of valid entries
            if((ValidEntryCount - 1) != pMndxInfo->MndxEntriesValid)
                nError = ERROR_BAD_FORMAT;

            // Mark the MNDX info as fully loaded
            pMndxInfo->bRootFileLoaded = true;
        }
        else
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Save the MNDX info to the archive storage
    if(nError == ERROR_SUCCESS)
    {
        // Store the MNDX database into the archive
        hs->pMndxInfo = pMndxInfo;
        pMndxInfo = NULL;

#if defined(_DEBUG) && defined(_X86_) && defined(CASCLIB_TEST)
//      CascDumpMndxNameTable("E:\\mndx-packages.txt", hs->pMndxInfo->pMarFile1);
        TestMndxRootFile(hs->pMndxInfo);
#endif
        // Load the top level entries
        nError = LoadPackageNames(hs);
    }

    // If anything failed, free the memory remaining allocated
    if(nError != ERROR_SUCCESS)
    {
        if(pMndxInfo != NULL)
            FreeMndxInfo(pMndxInfo);
        pMndxInfo = NULL;
    }

    return nError;
}

int SearchMndxInfo(PCASC_MNDX_INFO pMndxInfo, const char * szFileName, DWORD dwPackage, PCASC_ROOT_KEY_INFO pFoundInfo)
{
    PCASC_MNDX_ENTRY pMndxEntry;
    TMndxFindResult Struct1C;

    // Search the database for the file name
    if(pMndxInfo->bRootFileLoaded)
    {
        Struct1C.SetSearchPath(szFileName, strlen(szFileName));

        // Search the file name in the second MAR info (the one with stripped package names)
        if(MAR_FILE_SearchFile(pMndxInfo->pMarFile2, &Struct1C) != ERROR_SUCCESS)
            return ERROR_FILE_NOT_FOUND;

        // The found MNDX index must fall into range of valid MNDX entries
        if(Struct1C.MndxIndex < pMndxInfo->MndxEntriesValid)
        {
            // HOTS: E945F4
            pMndxEntry = pMndxInfo->ppValidEntries[Struct1C.MndxIndex];
            while((pMndxEntry->Flags & 0x00FFFFFF) != dwPackage)
            {
                // The highest bit serves as a terminator if set
                if(pMndxEntry->Flags & 0x80000000)
                    return ERROR_FILE_NOT_FOUND;

                pMndxEntry++;
            }

            // Fill the root info
            memcpy(pFoundInfo->EncodingKey, pMndxEntry->EncodingKey, MD5_HASH_SIZE);
            pFoundInfo->FileSize = pMndxEntry->FileSize;
            pFoundInfo->Flags = (BYTE)((pMndxEntry->Flags >> 0x18) & 0x3F);
            return ERROR_SUCCESS;
        }
    }

    return ERROR_FILE_NOT_FOUND;
}

bool DoStorageSearch_MNDX(TCascSearch * pSearch, PCASC_FIND_DATA pFindData)
{
    TMndxFindResult * pStruct1C = NULL;
    PCASC_MNDX_INFO pMndxInfo = pSearch->hs->pMndxInfo;
    PMAR_FILE pMarFile = pMndxInfo->pMarFile3;
    bool bFindResult = false;
        
    // Sanity checks
    assert(pMndxInfo != NULL);

    // If the first time, allocate the structure for the search result
    if(pSearch->pStruct1C == NULL)
    {
        // Create the new search structure
        pSearch->pStruct1C = pStruct1C = new TMndxFindResult;
        if(pSearch->pStruct1C == NULL)
            return false;

        // Setup the search mask
        pStruct1C->SetSearchPath("", 0);
    }

    // Make shortcut for the search structure
    assert(pSearch->pStruct1C != NULL);
    pStruct1C = (TMndxFindResult *)pSearch->pStruct1C;

    // Search the next file name (our code)
    pMarFile->pDatabasePtr->sub_1956CE0(pStruct1C, &bFindResult);
    if(bFindResult)
        return FillFindData(pSearch, pFindData, pStruct1C);

    return false;
}

void FreeMndxInfo(PCASC_MNDX_INFO pMndxInfo)
{
    if(pMndxInfo != NULL)
    {
        if(pMndxInfo->pMarFile1 != NULL)
            MAR_FILE_Destructor(pMndxInfo->pMarFile1);
        if(pMndxInfo->pMarFile2 != NULL)
            MAR_FILE_Destructor(pMndxInfo->pMarFile2);
        if(pMndxInfo->pMarFile3 != NULL)
            MAR_FILE_Destructor(pMndxInfo->pMarFile3);
        if(pMndxInfo->ppValidEntries != NULL)
            CASC_FREE(pMndxInfo->ppValidEntries);
        if(pMndxInfo->pMndxEntries != NULL)
            CASC_FREE(pMndxInfo->pMndxEntries);
        CASC_FREE(pMndxInfo);
    }
}

//----------------------------------------------------------------------------
// Unit tests

#if defined(_DEBUG) && defined(_X86_) && defined(CASCLIB_TEST)

extern "C" {
    DWORD _cdecl sub_19573D0_x86(TFileNameDatabase * pDB, DWORD arg_0, DWORD arg_4);
    DWORD _cdecl sub_1957EF0_x86(TFileNameDatabase * pDB, TMndxFindResult * pStruct1C);
    bool  _cdecl sub_1959460_x86(TFileNameDatabase * pDB, TMndxFindResult * pStruct1C);
    DWORD _cdecl GetExtraBitsIndex_x86(TStruct68 * pStruct, DWORD dwKey);
    DWORD _cdecl sub_1959CB0_x86(TFileNameDatabase * pDB, DWORD dwKey);
    DWORD _cdecl sub_1959F50_x86(TFileNameDatabase * pDB, DWORD arg_0);
}

extern "C" void * allocate_zeroed_memory_x86(size_t bytes)
{
    return calloc(bytes, 1);
}

extern "C" void free_memory_x86(void * ptr)
{
    if(ptr != NULL)
    {
        free(ptr);
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
/*
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
            assert(Struct1C_1.MndxIndex == Struct1C_2.MndxIndex);
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
*/

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
        assert(Struct1C_1.MndxIndex == Struct1C_2.MndxIndex);
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
    DWORD dwMndxIndex1 = 0xFFFFFFFF;
    DWORD dwMndxIndex2 = 0xFFFFFFFF;

    // Perform the search using original HOTS code
    {
        TMndxFindResult Struct1C;

        Struct1C.CreateStruct40();
        Struct1C.SetSearchPath(szFileName, nLength);

        // Call the original HOTS function
        sub_1957EF0_x86(pDB, &Struct1C);
        dwMndxIndex1 = Struct1C.MndxIndex;
    }

    // Perform the search using our code
    {
        TMndxFindResult Struct1C;

        Struct1C.CreateStruct40();
        Struct1C.SetSearchPath(szFileName, nLength);

        // Call our function
        pDB->FindFileInDatabase(&Struct1C);
        dwMndxIndex2 = Struct1C.MndxIndex;
    }

    // Compare both
    assert(dwMndxIndex1 == dwMndxIndex2);
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
            DWORD dwResult2 = pDB->GetNameFragmentDistanceEx(arg_0, arg_4);
  
            assert(dwResult1 == dwResult2);
        }
    }

    // Exercise function GetExtraBitsIndex
    for(DWORD i = 0; i < 0x10000; i++)
    {
        DWORD dwResult1 = GetExtraBitsIndex_x86(&pDB->Struct68_D0, i);
        DWORD dwResult2 = pDB->Struct68_D0.GetExtraBitsIndex(i);
  
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
    TestMndxFunctions(pMndxInfo->pMarFile1);
    TestMndxFunctions(pMndxInfo->pMarFile2);
    TestMndxFunctions(pMndxInfo->pMarFile3);

    // Find a "mods" in the package array
    TestFindPackage(pMndxInfo->pMarFile1, "mods/heroes.stormmod/base.stormmaps/maps/heroes/builtin/startingexperience/practicemode01.stormmap/dede.stormdata");

    // Search the package MAR file aith a path shorter than a fragment
    TestFileSearch(pMndxInfo->pMarFile1, "mods/heroes.s");

    // Test the file search
    TestFileSearch(pMndxInfo->pMarFile1, "");
    TestFileSearch(pMndxInfo->pMarFile2, "");
    TestFileSearch(pMndxInfo->pMarFile3, "");

    // False file search
    TestFileSearch(pMndxInfo->pMarFile2, "assets/textures/storm_temp_hrhu");

    // Open the listfile stream and initialize the listfile cache
    pvListFile = ListFile_OpenExternal(_T("e:\\Ladik\\Appdir\\CascLib\\listfile\\listfile-hots-29049.txt"));
    if(pvListFile != NULL)
    {
        // Check every file in the database
        while((nLength = ListFile_GetNext(pvListFile, "*", szFileName, MAX_PATH)) != 0)
        {
            // Normalize the file name: ToLower + BackSlashToSlash
            NormalizeFileName_LowerSlash(szFileName);

            // Check the file with all three MAR files
            TestMarFile(pMndxInfo->pMarFile1, szFileName, nLength);
            TestMarFile(pMndxInfo->pMarFile2, szFileName, nLength);
            TestMarFile(pMndxInfo->pMarFile3, szFileName, nLength);
        }

        ListFile_Free(pvListFile);
    }
}
#endif  // defined(_DEBUG) && defined(_X86_) && defined(CASCLIB_TEST)
