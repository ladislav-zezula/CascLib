/*****************************************************************************/
/* CascOpenStorage.cpp                    Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Storage functions for CASC                                                */
/* Note: WoW6 offsets refer to WoW.exe 6.0.3.19116 (32-bit)                  */
/* SHA1: c10e9ffb7d040a37a356b96042657e1a0c95c0dd                            */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascOpenStorage.cpp             */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"
#include "CascRootFile_Diablo3.h"

//-----------------------------------------------------------------------------
// Local structures

//-----------------------------------------------------------------------------
// Local variables

//-----------------------------------------------------------------------------
// Local functions

//-----------------------------------------------------------------------------
// Public functions

int LoadDiablo3RootFile(
    TCascStorage * hs,
    LPBYTE pbRootFile,
    DWORD cbRootFile,
    DWORD dwLocaleMask)
{
    // TODO: Not supported (yet)
    return ERROR_BAD_FORMAT;
}

