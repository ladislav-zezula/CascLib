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

    static bool IsRootFile(void * pTextFile)
    {
        const char * szLineBegin;
        const char * szLineEnd;
        char szFileName[MAX_PATH];
        CONTENT_KEY CKey;
        bool bResult = false;

        // Get the first line from the listfile
        if(ListFile_GetNextLine(pTextFile, &szLineBegin, &szLineEnd))
        {
            // We check the line length; if the line is too long, we ignore it
            if((szLineEnd - szLineBegin) < (MAX_PATH + MD5_STRING_SIZE + 1))
            {
                if(CSV_GetNameAndCKey(szLineBegin, szLineEnd, 0, 1, szFileName, MAX_PATH, &CKey) == ERROR_SUCCESS)
                {
                    bResult = true;
                }
            }
        }

        // We need to reset the listfile to the begin position
        ListFile_Reset(pTextFile);
        return bResult;
    }

    int Load(void * pTextFile)
    {
        CONTENT_KEY CKey;
        const char * szLineBegin;
        const char * szLineEnd;
        char szFileName[MAX_PATH];

        // Parse all lines
        while(ListFile_GetNextLine(pTextFile, &szLineBegin, &szLineEnd) != 0)
        {
            // Retrieve the file name and the content key
            if(CSV_GetNameAndCKey(szLineBegin, szLineEnd, 0, 1, szFileName, MAX_PATH, &CKey) == ERROR_SUCCESS)
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
    void * pTextFile;
    int nError = ERROR_BAD_FORMAT;

    // Parse the ROOT file first in order to see whether we have the correct format
    pTextFile = ListFile_FromBuffer(pbRootFile, cbRootFile);
    if(pTextFile != NULL)
    {
        // Verify whether this looks like a Starcraft I root file
        if(TRootHandler_SC1::IsRootFile(pTextFile))
        {
            // Allocate the root handler object
            pRootHandler = new TRootHandler_SC1();
            if(pRootHandler != NULL)
            {
                // Load the root directory. If load failed, we free the object
                nError = pRootHandler->Load(pTextFile);
                if(nError != ERROR_SUCCESS)
                {
                    delete pRootHandler;
                    pRootHandler = NULL;
                }
            }
        }

        // Free the listfile object
        ListFile_Free(pTextFile);
    }

    // Assign the root directory (or NULL) and return error
    hs->pRootHandler = pRootHandler;
    return nError;
}
