/*****************************************************************************/
/* CascRootFile_WoW.h                     Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Interface file for WoW 6.x root file                                      */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 15.05.14  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __CASC_DIABLO3_ROOT__
#define __CASC_DIABLO3_ROOT__

//-----------------------------------------------------------------------------
// Defines

#define CASC_DIABLO3_ROOT_SIGNATURE 0x8007000E

//-----------------------------------------------------------------------------
// Public functions

int LoadDiablo3RootFile(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask);

#endif  // __CASC_DIABLO3_ROOT__
