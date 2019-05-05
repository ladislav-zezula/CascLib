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

PCASC_CKEY_ENTRY FindCKeyEntry_CKey(TCascStorage * hs, LPBYTE pbCKey, PDWORD PtrIndex)
{
    return (PCASC_CKEY_ENTRY)hs->CKeyMap.FindObject(pbCKey, PtrIndex);
}

PCASC_CKEY_ENTRY FindCKeyEntry_EKey(TCascStorage * hs, LPBYTE pbEKey, PDWORD PtrIndex)
{
    return (PCASC_CKEY_ENTRY)hs->EKeyMap.FindObject(pbEKey, PtrIndex);
}

static TCascFile * CreateFileHandle(TCascStorage * hs, PCASC_CKEY_ENTRY pCKeyEntry)
{
    ULONGLONG StorageOffset = pCKeyEntry->StorageOffset;
    ULONGLONG FileOffsMask = ((ULONGLONG)1 << hs->InHeader.FileOffsetBits) - 1;
    TCascFile * hf;

    // Allocate the CASC file structure
    hf = (TCascFile *)CASC_ALLOC(TCascFile, 1);
    if(hf != NULL)
    {
        // Initialize the structure
        hf->ArchiveIndex = (DWORD)(StorageOffset >> hs->InHeader.FileOffsetBits);
        hf->ArchiveOffset = (DWORD)(StorageOffset & FileOffsMask);
        hf->szClassName = "TCascFile";
        hf->pCKeyEntry = pCKeyEntry;
        hf->pbFileCache = NULL;
        hf->cbFileCache = 0;
        hf->FilePointer = 0;
        hf->pStream = NULL;
        hf->pFrames = NULL;
        hf->FrameCount = 0;

        // Supply the sizes
        hf->EncodedSize = pCKeyEntry->EncodedSize;
        hf->ContentSize = pCKeyEntry->ContentSize;

        // Increment the number of references to the archive
        hs->dwRefCount++;
        hf->hs = hs;
    }

    return hf;
}

bool OpenFileByCKeyEntry(TCascStorage * hs, PCASC_CKEY_ENTRY pCKeyEntry, DWORD dwOpenFlags, HANDLE * PtrFileHandle)
{
    TCascFile * hf = NULL;
    int nError = ERROR_FILE_NOT_FOUND;

    // If the CKey entry is NULL, we consider the file non-existant
    if(pCKeyEntry != NULL)
    {
        // Create the file handle structure
        if((hf = CreateFileHandle(hs, pCKeyEntry)) != NULL)
        {
            hf->bVerifyIntegrity = (dwOpenFlags & CASC_STRICT_DATA_CHECK) ? true : false;
            nError = ERROR_SUCCESS;
        }
        else
        {
            nError = ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    // Give the output parameter, no matter what
    PtrFileHandle[0] = (HANDLE)hf;

    // Handle last error
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

bool OpenFileInternal(TCascStorage * hs, LPBYTE pbQueryKey, DWORD dwOpenFlags, HANDLE * PtrFileHandle)
{
    // Either open by CKey or EKey
    switch(dwOpenFlags & CASC_OPEN_TYPE_MASK)
    {
        case CASC_OPEN_BY_CKEY:
            return OpenFileByCKeyEntry(hs, FindCKeyEntry_CKey(hs, pbQueryKey), dwOpenFlags, PtrFileHandle);

        case CASC_OPEN_BY_EKEY:
            return OpenFileByCKeyEntry(hs, FindCKeyEntry_EKey(hs, pbQueryKey), dwOpenFlags, PtrFileHandle);

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
    }
}

//-----------------------------------------------------------------------------
// Public functions

bool WINAPI CascOpenFile(HANDLE hStorage, const void * pvFileName, DWORD dwLocaleFlags, DWORD dwOpenFlags, HANDLE * PtrFileHandle)
{
    PCASC_CKEY_ENTRY pCKeyEntry = NULL;
    TCascStorage * hs;
    const char * szFileName;
    DWORD FileDataId = CASC_INVALID_ID;
    BYTE CKeyEKeyBuffer[MD5_HASH_SIZE];
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
    if(PtrFileHandle == NULL)
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
            pCKeyEntry = hs->pRootHandler->GetFile(hs, szFileName);
            if(pCKeyEntry != NULL)
                break;

            // Second chance: If the file name is actually a file data id, we convert it to file data ID
            if(IsFileDataIdName(szFileName, FileDataId))
            {
                pCKeyEntry = hs->pRootHandler->GetFile(hs, FileDataId);
                if(pCKeyEntry != NULL)
                    break;
            }

            // Third chance: If the file name is a string representation of CKey/EKey, we try to query for CKey
            if(IsFileCKeyEKeyName(szFileName, CKeyEKeyBuffer))
            {
                pCKeyEntry = FindCKeyEntry_CKey(hs, CKeyEKeyBuffer);
                if(pCKeyEntry != NULL)
                    break;

                pCKeyEntry = FindCKeyEntry_EKey(hs, CKeyEKeyBuffer);
                if(pCKeyEntry != NULL)
                    break;
            }

            SetLastError(ERROR_FILE_NOT_FOUND);
            return false;

        case CASC_OPEN_BY_CKEY:

            // The 'pvFileName' must be a pointer to 16-byte CKey or EKey
            if(pvFileName == NULL)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }

            // Search the CKey map in order to find the CKey entry
            pCKeyEntry = FindCKeyEntry_CKey(hs, (LPBYTE)pvFileName);
            break;

        case CASC_OPEN_BY_EKEY:

            // The 'pvFileName' must be a pointer to 16-byte CKey or EKey
            if(pvFileName == NULL)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }

            // Search the CKey map in order to find the CKey entry
            pCKeyEntry = FindCKeyEntry_EKey(hs, (LPBYTE)pvFileName);
            break;

        case CASC_OPEN_BY_FILEID:

            // Retrieve the file CKey/EKey
            pCKeyEntry = hs->pRootHandler->GetFile(hs, CASC_FILE_DATA_ID_FROM_STRING(pvFileName));
            break;

        default:

            // Unknown open mode
            nError = ERROR_INVALID_PARAMETER;
            break;
    }

    // Perform the open operation
    return OpenFileByCKeyEntry(hs, pCKeyEntry, dwOpenFlags, PtrFileHandle);
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

