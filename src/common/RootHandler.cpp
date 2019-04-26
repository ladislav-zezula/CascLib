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

LPBYTE TRootHandler::GetKey(const char * /* szFileName */, DWORD /* FileDataId */, PDWORD /* PtrFileSize */)
{
    return NULL;
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
    dwRootFlags = 0;
}

//-----------------------------------------------------------------------------
// Virtual functions - TFileTreeRoot

int TFileTreeRoot::Insert(
    const char * szFileName,
    PCASC_CKEY_ENTRY pCKeyEntry)
{
    PCONTENT_KEY pCKey = (PCONTENT_KEY)pCKeyEntry->CKey;
    void * pItem;
    DWORD FileSize = ConvertBytesToInteger_4(pCKeyEntry->ContentSize);

    // We can support both mappings (FileName->CKey or FileName->CKey)
    if(dwRootFlags & ROOT_FLAG_USES_EKEY)
    {
        if(pCKeyEntry->EKeyCount == 0)
            return ERROR_CAN_NOT_COMPLETE;
        pCKey = (PCONTENT_KEY)pCKeyEntry->EKey;
    }

    // Insert the entry
    pItem = FileTree.Insert(pCKey, szFileName, FileTree.GetMaxFileDataId() + 1, FileSize);
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
        
        // Ignore folders, include unnamed items in the search
        if(!(pFileNode->Flags & CFN_FLAG_FOLDER))
        {
            // Check the wildcard
            if (CheckWildCard(pSearch->szFileName, pSearch->szMask))
            {
                // Retrieve the extra values (FileDataId, file size and locale flags)
                FileTree.GetExtras(pFileNode, &pSearch->dwFileDataId, &pSearch->dwFileSize, &pSearch->dwLocaleFlags);

                // If this is an unnamed item, we need to put the flag to the search structure
                pSearch->dwOpenFlags |= (pFileNode->NameLength == 0) ? CASC_OPEN_BY_CKEY : 0;
                
                // Return the found CKey value
                return pFileNode->CKey.Value;
            }
        }
    }

    // No more entries
    return NULL;
}

LPBYTE TFileTreeRoot::GetKey(const char * szFileName, DWORD FileDataId, PDWORD PtrFileSize)
{
    PCASC_FILE_NODE pFileNode = FileTree.Find(szFileName, FileDataId, PtrFileSize);

    return (pFileNode != NULL) ? pFileNode->CKey.Value : NULL;
}

DWORD TFileTreeRoot::GetFileDataId(const char * szFileName)
{
    PCASC_FILE_NODE pFileNode;
    DWORD FileDataId = CASC_INVALID_ID;

    pFileNode = FileTree.Find(szFileName, CASC_INVALID_ID, NULL);
    if(pFileNode != NULL)
        FileTree.GetExtras(pFileNode, &FileDataId, NULL, NULL);

    return FileDataId;
}
