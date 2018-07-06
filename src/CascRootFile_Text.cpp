/*****************************************************************************/
/* CascRootFile_Text.cpp                  Copyright (c) Ladislav Zezula 2017 */
/*---------------------------------------------------------------------------*/
/* Support for generic ROOT handler with mapping of FileName -> CKey         */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 28.10.15  1.00  Lad  The first version of CascRootFile_Text.cpp           */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Handler definitions for Starcraft I root file

struct TRootHandler_SC1 : public TFileTreeRoot
{
    public:

    TRootHandler_SC1() : TFileTreeRoot(0)
    {}

    static bool IsRootFile(void * pvTextFile)
    {
        CONTENT_KEY CKey;
        CASC_CSV Csv;
        size_t nColumns;
        char szFileName[MAX_PATH];
        bool bResult = false;

        // Get the first line from the listfile
        if(Csv.LoadNextLine(pvTextFile))
        {
            // There must be 2 or 3 elements
            nColumns = Csv.GetColumnCount();
            if (nColumns == 2 || nColumns == 3)
            {
                if (Csv.GetString(szFileName, MAX_PATH, 0) == ERROR_SUCCESS && Csv.GetBinary(CKey.Value, MD5_HASH_SIZE, 1) == ERROR_SUCCESS)
                {
                    bResult = true;
                }
            }
        }

        // We need to reset the listfile to the begin position
        ListFile_Reset(pvTextFile);
        return bResult;
    }

    int Load(void * pvTextFile)
    {
        CONTENT_KEY CKey;
        CASC_CSV Csv;
        char szFileName[MAX_PATH];

        // Parse all lines
        while(Csv.LoadNextLine(pvTextFile) != 0)
        {
            // Retrieve the file name and the content key
            if(Csv.GetString(szFileName, MAX_PATH, 0) == ERROR_SUCCESS && Csv.GetBinary(CKey.Value, MD5_HASH_SIZE, 1) == ERROR_SUCCESS)
            {
                // Insert the FileName+CKey to the file tree
//              void * pItem1 = FileTree_Insert(&pRootHandler->FileTree, &CKey, szFileName);
//              void * pItem2 = FileTree_Find(&pRootHandler->FileTree, szFileName);
//              assert(pItem1 == pItem2);

                // Insert the FileName+CKey to the file tree
                FileTree.Insert(&CKey, szFileName);
            }
        }

        return ERROR_SUCCESS;
    }
};

//-----------------------------------------------------------------------------
// Public functions

//
// Starcraft ROOT file is a text file with the following format:
// HD2/portraits/NBluCrit/NLCFID01.webm|c2795b120592355d45eba9cdc37f691e
// locales/enUS/Assets/campaign/EXPZerg/Zerg08/staredit/wav/zovtra01.ogg|316b0274bf2dabaa8db60c3ff1270c85
// locales/zhCN/Assets/sound/terran/ghost/tghdth01.wav|6637ed776bd22089e083b8b0b2c0374c
//

int RootHandler_CreateStarcraft1(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_SC1 * pRootHandler = NULL;
    void * pvTextFile;
    int nError = ERROR_BAD_FORMAT;

    // Parse the ROOT file first in order to see whether we have the correct format
    pvTextFile = ListFile_FromBuffer(pbRootFile, cbRootFile);
    if(pvTextFile != NULL)
    {
        // Verify whether this looks like a Starcraft I root file
        if(TRootHandler_SC1::IsRootFile(pvTextFile))
        {
            // Allocate the root handler object
            pRootHandler = new TRootHandler_SC1();
            if(pRootHandler != NULL)
            {
                // Load the root directory. If load failed, we free the object
                nError = pRootHandler->Load(pvTextFile);
                if(nError != ERROR_SUCCESS)
                {
                    delete pRootHandler;
                    pRootHandler = NULL;
                }
            }
        }

        // Free the listfile object
        ListFile_Free(pvTextFile);
    }

    // Assign the root directory (or NULL) and return error
    hs->pRootHandler = pRootHandler;
    return nError;
}
