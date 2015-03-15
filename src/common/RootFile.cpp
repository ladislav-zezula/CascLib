/*****************************************************************************/
/* RootFile.cpp                           Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Implementation of RootFile class                                          */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 09.03.15  1.00  Lad  Created                                              */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

LPBYTE RootFile_Search(TRootFile * pRootFile, struct _TCascSearch * pSearch, PDWORD PtrFileSize, PDWORD PtrLocaleFlags)
{
    // Check if the root structure is valid at all
    if(pRootFile == NULL)
        return NULL;
    
    return pRootFile->Search(pRootFile, pSearch, PtrFileSize, PtrLocaleFlags);
}

void RootFile_EndSearch(TRootFile * pRootFile, struct _TCascSearch * pSearch)
{
    // Check if the root structure is valid at all
    if(pRootFile != NULL)
    {
        pRootFile->EndSearch(pRootFile, pSearch);
    }
}

LPBYTE RootFile_GetKey(TRootFile * pRootFile, const char * szFileName)
{
    // Check if the root structure is valid at all
    if(pRootFile == NULL)
        return NULL;
    
    return pRootFile->GetKey(pRootFile, szFileName);
}

void RootFile_Close(TRootFile * pRootFile)
{
    // Check if the root structure is allocated at all
    if(pRootFile != NULL)
    {
        pRootFile->Close(pRootFile);
    }
}
