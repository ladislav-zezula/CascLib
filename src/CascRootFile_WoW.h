/*****************************************************************************/
/* CascRootFile_WoW.h                     Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Interface file for WoW 6.x root file                                      */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 15.05.14  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __CASC_WOW_ROOT__
#define __CASC_WOW_ROOT__

//-----------------------------------------------------------------------------
// Public functions

int LoadWowRootFile(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, DWORD dwLocaleMask);

#endif  // __CASC_WOW_ROOT__
