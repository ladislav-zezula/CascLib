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
    PCASC_FILE_NODE pFileNode;

    pFileNode = FileTree.InsertByName(pCKeyEntry, szFileName, FileTree.GetNextFileDataId());
    return (pFileNode != NULL) ? ERROR_SUCCESS : ERROR_CAN_NOT_COMPLETE;
}

PCASC_CKEY_ENTRY TFileTreeRoot::GetFile(TCascStorage * hs, const char * szFileName)
{
    PCASC_FILE_NODE pFileNode;
    ULONGLONG FileNameHash = CalcFileNameHash(szFileName);
    
    pFileNode = FileTree.Find(FileNameHash);
    if (pFileNode == NULL)
        return NULL;

    if (pFileNode->KeySize == 1 || hs->dwFeatures & CASC_FEATURE_ONLINE || pFileNode->pCKeyEntry[0]->Flags & CASC_CE_FILE_IS_LOCAL)
        return pFileNode->pCKeyEntry[0];

    for (DWORD i = 0; i < pFileNode->KeySize; i++)
    {
        if (pFileNode->pCKeyEntry[i]->Flags & CASC_CE_FILE_IS_LOCAL)
            return pFileNode->pCKeyEntry[i];
    }

    return pFileNode->pCKeyEntry[0];
    //return (pFileNode != NULL) ? pFileNode->pCKeyEntry : NULL;
}

PCASC_CKEY_ENTRY TFileTreeRoot::GetFile(TCascStorage * hs, DWORD FileDataId)
{
    PCASC_FILE_NODE pFileNode;

    pFileNode = FileTree.FindById(FileDataId);
    if (pFileNode == NULL)
        return NULL;

    if (pFileNode->KeySize == 1 || hs->dwFeatures & CASC_FEATURE_ONLINE || pFileNode->pCKeyEntry[0]->Flags & CASC_CE_FILE_IS_LOCAL)
        return pFileNode->pCKeyEntry[0];

    for (DWORD i = 0; i < pFileNode->KeySize; i++)
    {
        if (pFileNode->pCKeyEntry[i]->Flags & CASC_CE_FILE_IS_LOCAL)
            return pFileNode->pCKeyEntry[i];
    }

    return pFileNode->pCKeyEntry[0];
    //return (pFileNode != NULL) ? pFileNode->pCKeyEntry : NULL;
}

PCASC_CKEY_ENTRY TFileTreeRoot::GetFile(size_t nFileIndex, char * szFileName, size_t ccFileName)
{
    PCASC_CKEY_ENTRY pCKeyEntry = NULL;
    PCASC_FILE_NODE pFileNode;

    // Perform the search in the underlying file tree
    pFileNode = FileTree.PathAt(szFileName, ccFileName, nFileIndex);
    //if((pFileNode = FileTree.PathAt(szFileName, ccFileName, nFileIndex)) != NULL)
    //    pCKeyEntry = pFileNode->pCKeyEntry;
    //return pCKeyEntry;

    if (pFileNode == NULL)
        return NULL;

    if (pFileNode->KeySize == 1 /*|| hs->dwFeatures & CASC_FEATURE_ONLINE*/ || pFileNode->pCKeyEntry[0]->Flags & CASC_CE_FILE_IS_LOCAL)
        return pFileNode->pCKeyEntry[0];

    for (DWORD i = 0; i < pFileNode->KeySize; i++)
    {
        if (pFileNode->pCKeyEntry[i]->Flags & CASC_CE_FILE_IS_LOCAL)
            return pFileNode->pCKeyEntry[i];
    }

    return pFileNode->pCKeyEntry[0];
}

PCASC_CKEY_ENTRY TFileTreeRoot::Search(TCascSearch * pSearch, PCASC_FIND_DATA pFindData)
{
    PCASC_FILE_NODE pFileNode;
    size_t nMaxFileIndex = GetMaxFileIndex();

    // Are we still inside the root directory range?
    while(pSearch->nFileIndex < nMaxFileIndex)
    {
        //BREAKIF(pSearch->nFileIndex >= 2823765);

        // Retrieve the file item
        pFileNode = FileTree.PathAt(pFindData->szFileName, _countof(pFindData->szFileName), pSearch->nFileIndex++);
        if(pFileNode != NULL)
        {
            // Ignore folders, but report mount points. These can and should be able to open and read
            if((pFileNode->Flags & (CFN_FLAG_FOLDER | CFN_FLAG_MOUNT_POINT)) != CFN_FLAG_FOLDER)
            {
                // Check the wildcard
                if(CascCheckWildCard(pFindData->szFileName, pSearch->szMask))
                {
                    // Retrieve the extra values (FileDataId, file size and locale flags)
                    FileTree.GetExtras(pFileNode, &pFindData->dwFileDataId, &pFindData->dwLocaleFlags, &pFindData->dwContentFlags);

                    // Return the found CKey entry
                    for (DWORD i = 0; i < pFileNode->KeySize; i++)
                    {
                        if (pFileNode->pCKeyEntry[i]->Flags & CASC_CE_FILE_IS_LOCAL)
                            return pFileNode->pCKeyEntry[i];
                    }

                    return pFileNode->pCKeyEntry[0];
                }
            }
        }
    }

    // No more entries
    return NULL;
}

bool TFileTreeRoot::GetInfo(PCASC_CKEY_ENTRY pCKeyEntry, PCASC_FILE_FULL_INFO pFileInfo)
{
    PCASC_FILE_NODE pFileNode;

    // Can't do much if the root key is NULL
    if(pCKeyEntry != NULL)
    {
        pFileNode = FileTree.Find(pCKeyEntry);
        if(pFileNode != NULL)
        {
            FileTree.GetExtras(pFileNode, &pFileInfo->FileDataId, &pFileInfo->LocaleFlags, &pFileInfo->ContentFlags);
            pFileInfo->FileNameHash = pFileNode->FileNameHash;
            return true;
        }
    }

    return false;
}

bool TFileTreeRoot::GetInfo(const char* szFileName, PCASC_FILE_FULL_INFO pFileInfo)
{
    PCASC_FILE_NODE pFileNode;
    ULONGLONG FileNameHash = CalcFileNameHash(szFileName);

    pFileNode = FileTree.Find(FileNameHash);
    if (pFileNode == NULL)
        return NULL;

    FileTree.GetExtras(pFileNode, &pFileInfo->FileDataId, &pFileInfo->LocaleFlags, &pFileInfo->ContentFlags);
    pFileInfo->FileNameHash = pFileNode->FileNameHash;
    return true;
}

size_t TFileTreeRoot::Copy(TRootHandler * pRoot)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    size_t nMaxFileIndex = GetMaxFileIndex();
    size_t nItemsCopied = 0;
    char szFileName[0x200];

    for(size_t nFileIndex = 0; nFileIndex < nMaxFileIndex; nFileIndex++)
    {
        if((pCKeyEntry = pRoot->GetFile(nFileIndex, szFileName, _countof(szFileName))) != NULL)
        {
            if(szFileName[0] != 0)
            {
                Insert(szFileName, pCKeyEntry);
                nItemsCopied++;
            }
        }
    }

    return nItemsCopied;
}

size_t TFileTreeRoot::GetMaxFileIndex()
{
    return FileTree.GetMaxFileIndex();
}
