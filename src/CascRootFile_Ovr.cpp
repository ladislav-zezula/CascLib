/*****************************************************************************/
/* CascRootFile_Ovr.cpp                   Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Support for loading Overwatch ROOT file                                   */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 28.10.15  1.00  Lad  The first version of CascRootFile_Ovr.cpp            */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Structure definitions for Overwatch root file

struct TRootHandler_Ovr : public TRootHandler
{
    DWORD dwDummy;
};

//-----------------------------------------------------------------------------
// Implementation of Overwatch root file

static LPBYTE OvrHandler_Search(TRootHandler_Ovr * pRootHandler, TCascSearch * pSearch, PDWORD /* PtrFileSize */, PDWORD /* PtrLocaleFlags */)
{
    // No more entries
    return NULL;
}

static void OvrHandler_EndSearch(TRootHandler_Ovr * /* pRootHandler */, TCascSearch * /* pSearch */)
{
    // Do nothing
}

static LPBYTE OvrHandler_GetKey(TRootHandler_Ovr * pRootHandler, const char * szFileName)
{
    // Return the entry's encoding key or NULL
    return NULL;
}

static void OvrHandler_Close(TRootHandler_Ovr * pRootHandler)
{
    if(pRootHandler != NULL)
    {
        // Free the root file itself
        CASC_FREE(pRootHandler);
    }
}

//-----------------------------------------------------------------------------
// Public functions

int RootHandler_CreateOverwatch(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_Ovr * pRootHandler;
    LPBYTE pbRootFileEnd = pbRootFile + cbRootFile;

    // Allocate the root handler object
    pRootHandler = CASC_ALLOC(TRootHandler_Ovr, 1);
    if(pRootHandler == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill-in the handler functions
    memset(pRootHandler, 0, sizeof(TRootHandler_Ovr));
    pRootHandler->Search      = (ROOT_SEARCH)OvrHandler_Search;
    pRootHandler->EndSearch   = (ROOT_ENDSEARCH)OvrHandler_EndSearch;
    pRootHandler->GetKey      = (ROOT_GETKEY)OvrHandler_GetKey;
    pRootHandler->Close       = (ROOT_CLOSE)OvrHandler_Close;

    // Fill-in the flags
    pRootHandler->dwRootFlags |= ROOT_FLAG_HAS_NAMES;
    hs->pRootHandler = pRootHandler;
    
    // Succeeded
    return ERROR_SUCCESS;
}
