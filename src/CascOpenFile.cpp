/*****************************************************************************/
/* CascOpenFile.cpp                       Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* System-dependent directory functions for CascLib                          */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 01.05.14  1.00  Lad  The first version of CascOpenFile.cpp                */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local functions

TCascFile * IsValidCascFileHandle(HANDLE hFile)
{
    TCascFile * hf = (TCascFile *)hFile;

    return (hf != NULL && hf->hs != NULL && hf->szClassName != NULL && !strcmp(hf->szClassName, "TCascFile")) ? hf : NULL;
}

PCASC_CKEY_ENTRY FindCKeyEntry(TCascStorage * hs, PQUERY_KEY pCKey, PDWORD PtrIndex)
{
    return (PCASC_CKEY_ENTRY)hs->CKeyMap.FindObject(pCKey->pbData, PtrIndex);
}

PCASC_EKEY_ENTRY FindEKeyEntry(TCascStorage * hs, PQUERY_KEY pEKey, PDWORD PtrIndex)
{
    return (PCASC_EKEY_ENTRY)hs->EKeyMap.FindObject(pEKey->pbData, PtrIndex);
}

static TCascFile * CreateFileHandle(TCascStorage * hs, PCASC_CKEY_ENTRY pCKeyEntry, PCASC_EKEY_ENTRY pEKeyEntry)
{
    ULONGLONG StorageOffset = pEKeyEntry->StorageOffset;
    ULONGLONG FileOffsMask = ((ULONGLONG)1 << hs->InHeader.FileOffsetBits) - 1;
    TCascFile * hf;

    // Allocate the CASC file structure
    hf = (TCascFile *)CASC_ALLOC(TCascFile, 1);
    if(hf != NULL)
    {
        // Initialize the structure
        memset(hf, 0, sizeof(TCascFile));
        hf->ArchiveIndex = (DWORD)(StorageOffset >> hs->InHeader.FileOffsetBits);
        hf->ArchiveOffset = (DWORD)(StorageOffset & FileOffsMask);
        hf->szClassName = "TCascFile";
        hf->pCKeyEntry = pCKeyEntry;
        hf->pEKeyEntry = pEKeyEntry;
        hf->ContentSize = CASC_INVALID_SIZE;
        hf->EncodedSize = CASC_INVALID_SIZE;

        // Copy the encoded file size
        if(pCKeyEntry != NULL)
            hf->ContentSize = ConvertBytesToInteger_4(pCKeyEntry->ContentSize);
        if(pEKeyEntry != NULL)
            hf->EncodedSize = pEKeyEntry->EncodedSize;

        // Increment the number of references to the archive
        hs->dwRefCount++;
        hf->hs = hs;
    }

    return hf;
}

static bool OpenFileByEKey(TCascStorage * hs, PQUERY_KEY pEKey, PCASC_CKEY_ENTRY pCKeyEntry, DWORD dwOpenFlags, DWORD ContentSize, TCascFile ** PtrCascFile)
{
    PCASC_EKEY_ENTRY pEKeyEntry;
    TCascFile * hf = NULL;
    int nError = ERROR_SUCCESS;

    // Find the EKey entry in the array of encoded keys
    pEKeyEntry = FindEKeyEntry(hs, pEKey);
    if(pEKeyEntry != NULL)
    {
        // Create the file handle structure
        hf = CreateFileHandle(hs, pCKeyEntry, pEKeyEntry);
        if(hf != NULL)
        {
            if(hf->ContentSize == CASC_INVALID_SIZE && ContentSize != CASC_INVALID_SIZE)
                hf->ContentSize = ContentSize;
            hf->bVerifyIntegrity = (dwOpenFlags & CASC_STRICT_DATA_CHECK) ? true : false;
        }
        else
        {
            nError = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    else
    {
        nError = ERROR_FILE_NOT_FOUND;
    }

    // Give the output parameter, no matter what
    PtrCascFile[0] = hf;

    // Handle last error
    if(nError != ERROR_SUCCESS)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return (nError == ERROR_SUCCESS);
}

static bool OpenFileByCKey(TCascStorage * hs, PQUERY_KEY pCKey, DWORD dwOpenFlags, TCascFile ** PtrCascFile)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    QUERY_KEY EKey;

    // Find the encoding entry
    pCKeyEntry = FindCKeyEntry(hs, pCKey, NULL);
    if(pCKeyEntry == NULL)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return false;
    }

    // Prepare the file index and open the file by index
    // Note: We don't know what to do if there is more than just one EKey
    // We always take the first file present. Is that correct?
    EKey.pbData = pCKeyEntry->EKey;
    EKey.cbData = MD5_HASH_SIZE;
    return OpenFileByEKey(hs, &EKey, pCKeyEntry, dwOpenFlags, CASC_INVALID_SIZE, PtrCascFile);
}

bool OpenFileInternal(TCascStorage * hs, LPBYTE pbQueryKey, DWORD dwOpenFlags, DWORD dwContentSize, HANDLE * phFile)
{
    QUERY_KEY QueryKey;

    // Setup the CKey/EKey
    QueryKey.pbData = pbQueryKey;
    QueryKey.cbData = MD5_HASH_SIZE;

    // Either open by CKey or EKey
    switch(dwOpenFlags & CASC_OPEN_TYPE_MASK)
    {
        case CASC_OPEN_BY_CKEY:
            return OpenFileByCKey(hs, &QueryKey, dwOpenFlags, (TCascFile **)phFile);

        case CASC_OPEN_BY_EKEY:
            return OpenFileByEKey(hs, &QueryKey, NULL, dwOpenFlags, dwContentSize, (TCascFile **)phFile);

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
    }
}

//-----------------------------------------------------------------------------
// Public functions

bool WINAPI CascOpenFile(HANDLE hStorage, const void * pvFileName, DWORD dwLocaleFlags, DWORD dwOpenFlags, HANDLE * phFile)
{
    TCascStorage * hs;
    const char * szFileName;
    LPBYTE pbQueryKey = NULL;
    DWORD dwContentSize = CASC_INVALID_SIZE;
    DWORD dwFileDataId = CASC_INVALID_ID;
    BYTE CKeyEKeyBuffer[MD5_HASH_SIZE];
    bool bAutoDetectCKeyEKey = false;
    int nError = ERROR_SUCCESS;

    // This parameter is not used
    CASCLIB_UNUSED(dwLocaleFlags);

    // Validate the storage handle
    hs = IsValidCascStorageHandle(hStorage);
    if(hs == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Validate the other parameters
    if(phFile == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Retrieve the CKey/EKey from the file name in different modes
    switch(dwOpenFlags & CASC_OPEN_TYPE_MASK)
    {
        case CASC_OPEN_BY_NAME:

            // The 'pvFileName' must be zero terminated ANSI file name
            szFileName = (const char *)pvFileName;
            if(szFileName == NULL || szFileName[0] == 0)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }

            // The first chance: Try to find the file by name (using the root handler)
            pbQueryKey = hs->pRootHandler->GetKey(szFileName, CASC_INVALID_ID, &dwContentSize);
            if(pbQueryKey != NULL)
            {
                bAutoDetectCKeyEKey = true;
                break;
            }

            // Second chance: If the file name is actually a file data id, we convert it to file data ID
            if(IsFileDataIdName(szFileName, dwFileDataId))
            {
                pbQueryKey = hs->pRootHandler->GetKey(NULL, dwFileDataId, &dwContentSize);
                if(pbQueryKey != NULL)
                {
                    bAutoDetectCKeyEKey = true;
                    break;
                }
            }

            // Third chance: If the file name is actually a hash, then we try to open by hash
            if(IsFileCKeyEKeyName(szFileName, CKeyEKeyBuffer))
            {
                pbQueryKey = CKeyEKeyBuffer;
                bAutoDetectCKeyEKey = true;
                break;
            }
            break;

        case CASC_OPEN_BY_CKEY:
        case CASC_OPEN_BY_EKEY:

            // The 'pvFileName' must be a pointer to 16-byte CKey or EKey
            if(pvFileName == NULL)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }

            pbQueryKey = (LPBYTE)pvFileName;
            break;

        case CASC_OPEN_BY_FILEID:

            // Retrieve the file CKey/EKey
            pbQueryKey = hs->pRootHandler->GetKey(NULL, CASC_NAMETOID(pvFileName), &dwContentSize);
            break;

        default:

            // Unknown open mode
            nError = ERROR_INVALID_PARAMETER;
            break;
    }

    // Perform the open operation
    if(nError == ERROR_SUCCESS)
    {
        if(pbQueryKey != NULL)
        {
            // Auto-detect the open mode by whether the root handler supplies CKey or EKey
            if(bAutoDetectCKeyEKey)
            {
                dwOpenFlags &= ~CASC_OPEN_TYPE_MASK;

                if(hs->pRootHandler->GetFeatures() & CASC_FEATURE_ROOT_CKEY)
                    dwOpenFlags |= CASC_OPEN_BY_CKEY;
                else
                    dwOpenFlags |= CASC_OPEN_BY_EKEY;
            }

            // Perform the open
            if(!OpenFileInternal(hs, pbQueryKey, dwOpenFlags, dwContentSize, phFile))
            {
                assert(GetLastError() != ERROR_SUCCESS);
                nError = GetLastError();
            }
        }
        else
        {
            nError = ERROR_FILE_NOT_FOUND;
        }
    }

    // Set the last error and return
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

bool WINAPI CascCloseFile(HANDLE hFile)
{
    TCascFile * hf;

    hf = IsValidCascFileHandle(hFile);
    if(hf != NULL)
    {
        // Close (dereference) the archive handle
        if(hf->hs != NULL)
            CascCloseStorage((HANDLE)hf->hs);
        hf->hs = NULL;

        // Free the file cache and frame array
        if(hf->pbFileCache != NULL)
            CASC_FREE(hf->pbFileCache);
        if(hf->pFrames != NULL)
            CASC_FREE(hf->pFrames);

        // Free the structure itself
        hf->szClassName = NULL;
        CASC_FREE(hf);
        return true;
    }

    SetLastError(ERROR_INVALID_HANDLE);
    return false;
}

