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

//-----------------------------------------------------------------------------
// Common support

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

void RootFile_Dump(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, const TCHAR * szNameFormat, const TCHAR * szListFile, int nDumpLevel)
{
    TDumpContext * dc;

    // Only if the ROOT provider suports the dump option
    if(hs->pRootFile != NULL && hs->pRootFile->Dump != NULL)
    {
        // Create the dump file
        dc = CreateDumpContext(hs, szNameFormat);
        if(dc != NULL)                      
        {
            // Dump the content and close the file
            hs->pRootFile->Dump(hs, dc, pbRootFile, cbRootFile, szListFile, nDumpLevel);
            dump_close(dc);
        }
    }
}

void RootFile_Close(TRootFile * pRootFile)
{
    // Check if the root structure is allocated at all
    if(pRootFile != NULL)
    {
        pRootFile->Close(pRootFile);
    }
}
