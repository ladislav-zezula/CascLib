/*****************************************************************************/
/* CascFindFile.cpp                       Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* System-dependent directory functions for CascLib                          */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 10.05.14  1.00  Lad  The first version of CascFindFile.cpp                */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local functions

static TCascSearch * IsValidSearchHandle(HANDLE hFind)
{
    TCascSearch * pSearch = (TCascSearch *)hFind;

    return (pSearch != NULL && pSearch->szClassName != NULL && !strcmp(pSearch->szClassName, "TCascSearch") && pSearch->szMask != NULL) ? pSearch : NULL;
}

static void FreeSearchHandle(TCascSearch * pSearch)
{
    // Only if the storage handle is valid
    assert(pSearch != NULL);

    // Close (dereference) the archive handle
    if(pSearch->hs != NULL)
    {
        // Dereference the storage handle
        CascCloseStorage((HANDLE)pSearch->hs);
        pSearch->hs = NULL;
    }

    // Free the file cache and frame array
    if(pSearch->szMask != NULL)
        CASC_FREE(pSearch->szMask);
    if(pSearch->szListFile != NULL)
        CASC_FREE(pSearch->szListFile);
    if(pSearch->pCache != NULL)
        ListFile_Free(pSearch->pCache);

    // Free the structure itself
    pSearch->szClassName = NULL;
    CASC_FREE(pSearch);
}

static TCascSearch * AllocateSearchHandle(TCascStorage * hs, const TCHAR * szListFile, const char * szMask)
{
    TCascSearch * pSearch;
    size_t cbToAllocate;
    size_t CKeyCount = (hs->CKeyArray.ItemCount() + 31) / 32;

    // When using the MNDX info, do not allocate the extra bit array
    cbToAllocate = sizeof(TCascSearch) + (CKeyCount * sizeof(DWORD));
    pSearch = (TCascSearch *)CASC_ALLOC(BYTE, cbToAllocate);
    if(pSearch != NULL)
    {
        // Initialize the structure
        memset(pSearch, 0, cbToAllocate);
        pSearch->szClassName = "TCascSearch";

        // Save the search handle
        pSearch->hs = hs;
        hs->dwRefCount++;

        // If the mask was not given, use default
        if(szMask == NULL)
            szMask = "*";

        // Save the other variables
        if(szListFile != NULL)
        {
            pSearch->szListFile = CascNewStr(szListFile, 0);
            if(pSearch->szListFile == NULL)
            {
                FreeSearchHandle(pSearch);
                return NULL;
            }
        }

        // Allocate the search mask
        pSearch->szMask = CascNewStr(szMask, 0);
        if(pSearch->szMask == NULL)
        {
            FreeSearchHandle(pSearch);
            return NULL;
        }
    }

    return pSearch;
}

static bool FileFoundBefore(TCascSearch * pSearch, PCASC_CKEY_ENTRY pCKeyEntry)
{
    size_t CKeyIndex = pSearch->hs->CKeyArray.IndexOf(pCKeyEntry);
    DWORD IntIndex = (DWORD)(CKeyIndex / 0x20);
    DWORD BitMask = 1 << (CKeyIndex & 0x1F);

    // If the bit in the map is set, it means that the file was found before
    if(pSearch->BitArray[IntIndex] & BitMask)
        return true;

    // Not found before
    pSearch->BitArray[IntIndex] |= BitMask;
    return false;
}

// Reset the search structure. Called before each search
static void ResetFindData(PCASC_FIND_DATA pFindData)
{
    // Reset the variables
    ZeroMemory16(pFindData->CKey);
    ZeroMemory16(pFindData->EKey);
    pFindData->szFileName[0] = 0;
    pFindData->szPlainName = pFindData->szFileName;
    pFindData->TagBitMask = 0;
    pFindData->dwFileDataId = CASC_INVALID_ID;
    pFindData->dwFileSize = CASC_INVALID_SIZE;
    pFindData->dwLocaleFlags = CASC_INVALID_ID;
    pFindData->dwContentFlags = CASC_INVALID_ID;
    pFindData->NameType = CascNameFull;
    pFindData->bFileAvailable = false;
    pFindData->bCanOpenByName = false;
    pFindData->bCanOpenByDataId = false;
    pFindData->bCanOpenByCKey = false;
    pFindData->bCanOpenByEKey = false;
}

static void SupplyFakeFileName(PCASC_FIND_DATA pFindData)
{
    // If the file can be open by file data id, create fake file name
    if(pFindData->bCanOpenByDataId)
    {
        sprintf(pFindData->szFileName, "FILE%08X.dat", pFindData->dwFileDataId);
        pFindData->NameType = CascNameDataId;
        return;
    }

    // If the file can be open by CKey, convert the CKey to file name
    if(pFindData->bCanOpenByCKey)
    {
        StringFromBinary(pFindData->CKey, MD5_HASH_SIZE, pFindData->szFileName);
        pFindData->NameType = CascNameCKey;
        return;
    }

    // CKey should be always present
    StringFromBinary(pFindData->EKey, MD5_HASH_SIZE, pFindData->szFileName);
    pFindData->NameType = CascNameEKey;
    assert(pFindData->bCanOpenByEKey != false);
}

static bool CopyCKeyEntryToFindData(PCASC_FIND_DATA pFindData, PCASC_CKEY_ENTRY pCKeyEntry)
{
    // Supply both keys
    CopyMemory16(pFindData->CKey, pCKeyEntry->CKey);
    CopyMemory16(pFindData->EKey, pCKeyEntry->EKey);
    pFindData->bCanOpenByCKey = (pCKeyEntry->Flags & CASC_CE_HAS_CKEY) ? true : false;
    pFindData->bCanOpenByEKey = (pCKeyEntry->Flags & CASC_CE_HAS_EKEY) ? true : false;

    // Supply the tag mask
    pFindData->TagBitMask = pCKeyEntry->TagBitMask;
    
    // Supply the plain name. Only do that if the found name is not a CKey/EKey
    if(pFindData->szFileName[0] != 0)
        pFindData->szPlainName = (char *)GetPlainFileName(pFindData->szFileName);

    // If we retrieved the file size directly from the root provider, use it
    // Otherwise, supply EncodedSize or ContentSize, whichever is available (but ContentSize > EncodedSize)
    if(pFindData->dwFileSize == CASC_INVALID_SIZE)
        pFindData->dwFileSize = pCKeyEntry->ContentSize;

    // Set flag indicating that the file is locally available
    pFindData->bFileAvailable = (pCKeyEntry->Flags & CASC_CE_FILE_IS_LOCAL);

    // Supply a fake file name, if there is none supplied by the root handler
    if(pFindData->szFileName[0] == 0)
        SupplyFakeFileName(pFindData);
    return true;
}

// Perform searching using root-specific provider.
// The provider may need the listfile
static bool DoStorageSearch_RootFile(TCascSearch * pSearch, PCASC_FIND_DATA pFindData)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    TCascStorage * hs = pSearch->hs;

    // Reset the search structure
    ResetFindData(pFindData);

    // Enumerate over all files
    for(;;)
    {
        // Attempt to find (the next) file from the root handler
        pCKeyEntry = hs->pRootHandler->Search(pSearch, pFindData);
        if(pCKeyEntry == NULL)
            return false;

        // Remember that this file was found before.
        // DO NOT exclude files from search while searching by root.
        // * Multiple file names may be mapped to the same CKey
        // * Multiple file data ids may be mapped to the same CKey
        FileFoundBefore(pSearch, pCKeyEntry);

        // Copy the CKey entry to the find data and return it
        return CopyCKeyEntryToFindData(pFindData, pCKeyEntry);
    }
}

static bool DoStorageSearch_CKey(TCascSearch * pSearch, PCASC_FIND_DATA pFindData)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    TCascStorage * hs = pSearch->hs;
    size_t nTotalItems = hs->CKeyArray.ItemCount();

    // Reset the find data structure
    ResetFindData(pFindData);

    // Check for CKeys that haven't been found yet
    while(pSearch->nFileIndex < nTotalItems)
    {
        // Locate the CKey entry
        pCKeyEntry = (PCASC_CKEY_ENTRY)hs->CKeyArray.ItemAt(pSearch->nFileIndex++);
        if((pCKeyEntry->Flags & CASC_CE_HAS_CKEY) == 0)
            continue;

        // Skip files that have been found before
        if(!FileFoundBefore(pSearch, pCKeyEntry))
            return CopyCKeyEntryToFindData(pFindData, pCKeyEntry);
    }

    // Nameless search ended
    return false;
}

static bool DoStorageSearch(TCascSearch * pSearch, PCASC_FIND_DATA pFindData)
{
    // State 0: No search done yet
    if(pSearch->dwState == 0)
    {
        // Does the search specify listfile?
        if(pSearch->szListFile != NULL)
            pSearch->pCache = ListFile_OpenExternal(pSearch->szListFile);

        // Move the search phase to the listfile searching
        pSearch->nFileIndex = 0;
        pSearch->dwState++;
    }

    // State 1: Searching the list file
    if(pSearch->dwState == 1)
    {
        if(DoStorageSearch_RootFile(pSearch, pFindData))
            return true;

        // Move to the nameless search state
        pSearch->nFileIndex = 0;
        pSearch->dwState++;
    }

    // State 2: Searching the remaining entries by CKey
    if(pSearch->dwState == 2 && (pSearch->szMask == NULL || !strcmp(pSearch->szMask, "*")))
    {
        if(DoStorageSearch_CKey(pSearch, pFindData))
            return true;

        // Move to the final search state
        pSearch->nFileIndex = 0;
        pSearch->dwState++;
    }

    return false;
}

//-----------------------------------------------------------------------------
// Public functions

HANDLE WINAPI CascFindFirstFile(
    HANDLE hStorage,
    const char * szMask,
    PCASC_FIND_DATA pFindData,
    const TCHAR * szListFile)
{
    TCascStorage * hs;
    TCascSearch * pSearch = NULL;
    int nError = ERROR_SUCCESS;

    // Check parameters
    if((hs = IsValidCascStorageHandle(hStorage)) == NULL)
        nError = ERROR_INVALID_HANDLE;
    if(szMask == NULL || pFindData == NULL)
        nError = ERROR_INVALID_PARAMETER;

    // Init the search structure and search handle
    if(nError == ERROR_SUCCESS)
    {
        // Allocate the search handle
        pSearch = AllocateSearchHandle(hs, szListFile, szMask);
        if(pSearch == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Perform search
    if(nError == ERROR_SUCCESS)
    {
        if(!DoStorageSearch(pSearch, pFindData))
            nError = ERROR_NO_MORE_FILES;
    }

    if(nError != ERROR_SUCCESS)
    {
        if(pSearch != NULL)
            FreeSearchHandle(pSearch);
        pSearch = (TCascSearch *)INVALID_HANDLE_VALUE;
    }

    return (HANDLE)pSearch;
}

bool WINAPI CascFindNextFile(
    HANDLE hFind,
    PCASC_FIND_DATA pFindData)
{
    TCascSearch * pSearch;

    pSearch = IsValidSearchHandle(hFind);
    if(pSearch == NULL || pFindData == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Perform search
    return DoStorageSearch(pSearch, pFindData);
}

bool WINAPI CascFindClose(HANDLE hFind)
{
    TCascSearch * pSearch;

    pSearch = IsValidSearchHandle(hFind);
    if(pSearch == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    FreeSearchHandle(pSearch);
    return true;
}
