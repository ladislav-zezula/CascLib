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
// Constructor and destructor

TRootHandler::TRootHandler()
{
    dwFeatures = 0;
}

TRootHandler::~TRootHandler()
{}

//-----------------------------------------------------------------------------
// Virtual functions

int TRootHandler::Insert(const char * /* szFileName */, PCASC_CKEY_ENTRY /* pCKeyEntry */)
{
    return ERROR_NOT_SUPPORTED;
}

LPBYTE TRootHandler::Search(struct _TCascSearch * /* pSearch */, PCASC_FIND_DATA /* pFindData */)
{
    return NULL;
}

void TRootHandler::EndSearch(struct _TCascSearch * /* pSearch */)
{}

LPBYTE TRootHandler::GetKey(const char * /* szFileName */, DWORD /* FileDataId */, PDWORD /* PtrFileSize */)
{
    return NULL;
}

bool TRootHandler::GetInfo(LPBYTE /* pbRootKey */, PCASC_FILE_FULL_INFO /* pFileInfo */)
{
    return false;
}

//-----------------------------------------------------------------------------
// Constructor and destructor - TFileTreeRoot

TFileTreeRoot::TFileTreeRoot(DWORD FileTreeFlags) : TRootHandler()
{
    // Initialize the file tree
    FileTree.Create(FileTreeFlags);
}

TFileTreeRoot::~TFileTreeRoot()
{
    // Free the file tree
    FileTree.Free();
    dwFeatures = 0;
}

//-----------------------------------------------------------------------------
// Virtual functions - TFileTreeRoot

int TFileTreeRoot::Insert(
    const char * szFileName,
    PCASC_CKEY_ENTRY pCKeyEntry)
{
    PCONTENT_KEY pCKey = (PCONTENT_KEY)pCKeyEntry->CKey;
    void * pItem;
    DWORD ContentSize = ConvertBytesToInteger_4(pCKeyEntry->ContentSize);

    // We can support both mappings (FileName->CKey or FileName->CKey)
    if((dwFeatures & CASC_FEATURE_ROOT_CKEY) == 0)
    {
        if(pCKeyEntry->EKeyCount == 0)
            return ERROR_CAN_NOT_COMPLETE;
        pCKey = (PCONTENT_KEY)pCKeyEntry->EKey;
    }

    // Insert the entry
    pItem = FileTree.Insert(pCKey, szFileName, FileTree.GetMaxFileDataId() + 1, ContentSize);
    return (pItem != NULL) ? ERROR_SUCCESS : ERROR_CAN_NOT_COMPLETE;
}

LPBYTE TFileTreeRoot::Search(TCascSearch * pSearch, PCASC_FIND_DATA pFindData)
{
    PCASC_FILE_NODE pFileNode;
    size_t ItemCount = FileTree.GetCount();

    // Are we still inside the root directory range?
    while(pSearch->IndexLevel1 < ItemCount)
    {
        // Retrieve the file item
        pFileNode = FileTree.PathAt(pFindData->szFileName, MAX_PATH, pSearch->IndexLevel1++);
        if(pFileNode != NULL)
        {
            // Ignore folders, include unnamed items in the search
            if(!(pFileNode->Flags & CFN_FLAG_FOLDER))
            {
                // Check the wildcard
                if (CheckWildCard(pFindData->szFileName, pSearch->szMask))
                {
                    // Retrieve the extra values (FileDataId, file size and locale flags)
                    FileTree.GetExtras(pFileNode, &pFindData->dwFileDataId, &pFindData->dwFileSize, &pFindData->dwLocaleFlags, &pFindData->dwContentFlags);

                    // Supply the bCanOpenByDataId variable
                    pFindData->bCanOpenByName = (pFileNode->NameHash != 0);
                    pFindData->bCanOpenByDataId = (pFindData->dwFileDataId != CASC_INVALID_ID);

                    // Return the found CKey value
                    return pFileNode->CKey.Value;
                }
            }
        }
    }

    // No more entries
    return NULL;
}

LPBYTE TFileTreeRoot::GetKey(const char * szFileName, DWORD FileDataId, PDWORD PtrFileSize)
{
    PCASC_FILE_NODE pFileNode;
    CASC_FIND_DATA FindData;
    LPBYTE pbCKey = NULL;

    pFileNode = FileTree.Find(szFileName, FileDataId, &FindData);
    if(pFileNode != NULL)
    {
        if(PtrFileSize != NULL)
            PtrFileSize[0] = FindData.dwFileSize;
        pbCKey = pFileNode->CKey.Value;
    }

    return pbCKey;
}

DWORD TFileTreeRoot::GetFileDataId(const char * szFileName)
{
    PCASC_FILE_NODE pFileNode;
    DWORD FileDataId = CASC_INVALID_ID;

    pFileNode = FileTree.Find(szFileName, CASC_INVALID_ID, NULL);
    if(pFileNode != NULL)
        FileTree.GetExtras(pFileNode, &FileDataId, NULL, NULL, NULL);

    return FileDataId;
}

bool TFileTreeRoot::GetInfo(LPBYTE pbRootKey, PCASC_FILE_FULL_INFO pFileInfo)
{
    PCASC_FILE_NODE pFileNode;

    // Can't do much if the root key is NULL
    if(pbRootKey != NULL)
    {
        pFileNode = FileTree.Find(pbRootKey);
        if(pFileNode != NULL)
        {
            FileTree.GetExtras(pFileNode, &pFileInfo->FileDataId, &pFileInfo->ContentSize, &pFileInfo->LocaleFlags, &pFileInfo->ContentFlags);
            pFileInfo->FileNameHash = pFileNode->NameHash;
            return true;
        }
    }

    return false;
}

