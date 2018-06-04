/*****************************************************************************/
/* RootHandler.cpp                        Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Implementation of root handler                                            */
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

int RootHandler_Insert(TRootHandler * pRootHandler, const char * szFileName, PCASC_CKEY_ENTRY pCKeyEntry)
{
    // Check if we have everything needed
    if(pRootHandler == NULL || pRootHandler->Insert == NULL)
        return ERROR_NOT_SUPPORTED;

    // The pbQueryKey parameter must be valid
    assert(pCKeyEntry != NULL);
    assert(pCKeyEntry->EKeyCount >= 1);

    // Ask the root folder to insert the key
    return pRootHandler->Insert(pRootHandler, szFileName, pCKeyEntry);
}

LPBYTE RootHandler_Search(TRootHandler * pRootHandler, struct _TCascSearch * pSearch)
{
    // Check if the root structure is valid at all
    if(pRootHandler == NULL)
        return NULL;

    return pRootHandler->Search(pRootHandler, pSearch);
}

void RootHandler_EndSearch(TRootHandler * pRootHandler, struct _TCascSearch * pSearch)
{
    // Check if the root structure is valid at all
    if(pRootHandler != NULL)
    {
        pRootHandler->EndSearch(pRootHandler, pSearch);
    }
}

LPBYTE RootHandler_GetKey(TRootHandler * pRootHandler, const char * szFileName, PDWORD PtrFileSize)
{
    // Check if the root structure is valid at all
    if(pRootHandler == NULL)
        return NULL;

    return pRootHandler->GetKey(pRootHandler, szFileName, PtrFileSize);
}

void RootHandler_Dump(TCascStorage * hs, LPBYTE pbRootHandler, DWORD cbRootHandler, const TCHAR * szNameFormat, const TCHAR * szListFile, int nDumpLevel)
{
    TDumpContext * dc;

    // Only if the ROOT provider suports the dump option
    if(hs->pRootHandler != NULL && hs->pRootHandler->Dump != NULL)
    {
        // Create the dump file
        dc = CreateDumpContext(hs, szNameFormat);
        if(dc != NULL)
        {
            // Dump the content and close the file
            hs->pRootHandler->Dump(hs, dc, pbRootHandler, cbRootHandler, szListFile, nDumpLevel);
            dump_close(dc);
        }
    }
}

void RootHandler_Close(TRootHandler * pRootHandler)
{
    // Check if the root structure is allocated at all
    if(pRootHandler != NULL)
    {
        pRootHandler->Close(pRootHandler);
    }
}

DWORD RootHandler_GetFileId(TRootHandler * pRootHandler, const char * szFileName)
{
    // Check if the root structure is valid at all
    if(pRootHandler == NULL)
        return 0;

    return pRootHandler->GetFileId(pRootHandler, szFileName);
}
