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
    dwRootFlags = 0;
}

TRootHandler::~TRootHandler()
{}

//-----------------------------------------------------------------------------
// Virtual functions

int TRootHandler::Insert(const char * /* szFileName */, PCASC_CKEY_ENTRY /* pCKeyEntry */)
{
    return ERROR_NOT_SUPPORTED;
}

LPBYTE TRootHandler::Search(struct _TCascSearch * /* pSearch */)
{
    return NULL;
}

void TRootHandler::EndSearch(struct _TCascSearch * /* pSearch */)
{}

LPBYTE TRootHandler::GetKey(const char * /* szFileName */, PDWORD /* PtrFileSize */)
{
    return NULL;
}

DWORD TRootHandler::GetFileId(const char * /* szFileName */)
{
    return 0;
}

//-----------------------------------------------------------------------------
// Constructor and destructor - TFileTreeRoot

TFileTreeRoot::TFileTreeRoot(DWORD FileTreeFlags) : TRootHandler()
{
    // Initialize the file tree
    FileTree.Create(FileTreeFlags);

    // Remember that we have file names
    dwRootFlags |= ROOT_FLAG_HAS_NAMES;
}

TFileTreeRoot::~TFileTreeRoot()
{
    // Free the file tree
    FileTree.Free();
    dwRootFlags = 0;
}

//-----------------------------------------------------------------------------
// Virtual functions - TFileTreeRoot

int TFileTreeRoot::Insert(
    const char * szFileName,
    PCASC_CKEY_ENTRY pCKeyEntry)
{
    PCONTENT_KEY pCKey = (PCONTENT_KEY)pCKeyEntry->CKey;
    DWORD FileSize = ConvertBytesToInteger_4(pCKeyEntry->ContentSize);
    void * pItem;

    // We can support both mappings (FileName->CKey or FileName->CKey)
    if(dwRootFlags & ROOT_FLAG_USES_EKEY)
    {
        if(pCKeyEntry->EKeyCount == 0)
            return ERROR_CAN_NOT_COMPLETE;
        pCKey = (PCONTENT_KEY)pCKeyEntry->EKey;
    }

    // Insert the entry
    pItem = FileTree.Insert(pCKey, szFileName, CASC_INVALID_ID, FileSize);
    return (pItem != NULL) ? ERROR_SUCCESS : ERROR_CAN_NOT_COMPLETE;
}

LPBYTE TFileTreeRoot::Search(TCascSearch * pSearch)
{
    PCASC_FILE_NODE pFileNode;
    size_t ItemCount = FileTree.GetCount();

    // Are we still inside the root directory range?
    while(pSearch->IndexLevel1 < ItemCount)
    {
        // Retrieve the file item
        pFileNode = FileTree.PathAt(pSearch->szFileName, MAX_PATH, pSearch->IndexLevel1);
        pSearch->IndexLevel1++;
        
        // Ignore folders and items with no name
        if(pFileNode->NameLength != 0 && (pFileNode->Flags & CFN_FLAG_FOLDER) == 0)
        {
            // Check the wildcard
            if (CheckWildCard(pSearch->szFileName, pSearch->szMask))
            {
                FileTree.GetExtras(pFileNode, &pSearch->dwFileDataId, &pSearch->dwFileSize, &pSearch->dwLocaleFlags);
                return pFileNode->CKey.Value;
            }
        }
    }

    // No more entries
    return NULL;
}

LPBYTE TFileTreeRoot::GetKey(const char * szFileName, PDWORD PtrFileSize)
{
    PCASC_FILE_NODE pFileNode = FileTree.Find(szFileName, PtrFileSize);

    return (pFileNode != NULL) ? pFileNode->CKey.Value : NULL;
}

DWORD TFileTreeRoot::GetFileId(const char * szFileName)
{
    PCASC_FILE_NODE pFileNode = FileTree.Find(szFileName, NULL);
    DWORD FileId = CASC_INVALID_ID;

    if(pFileNode != NULL)
        FileTree.GetExtras(pFileNode, &FileId, NULL, NULL);
    return FileId;
}
