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

TCascFile * IsValidFileHandle(HANDLE hFile)
{
    TCascFile * hf = (TCascFile *)hFile;

    return (hf != NULL && hf->hs != NULL && hf->szClassName != NULL && !strcmp(hf->szClassName, "TCascFile")) ? hf : NULL;
}

PCASC_INDEX_ENTRY FindIndexEntry(TCascStorage * hs, PQUERY_KEY pEKey)
{
    PCASC_INDEX_ENTRY pIndexEntry = NULL;

    if(hs->pIndexEntryMap != NULL)
        pIndexEntry = (PCASC_INDEX_ENTRY)Map_FindObject(hs->pIndexEntryMap, pEKey->pbData, NULL);

    return pIndexEntry;
}

PCASC_ENCODING_ENTRY FindEncodingEntry(TCascStorage * hs, PQUERY_KEY pCKey, PDWORD PtrIndex)
{
    PCASC_ENCODING_ENTRY pEncodingEntry = NULL;

    if(hs->pEncodingMap != NULL)
        pEncodingEntry = (PCASC_ENCODING_ENTRY)Map_FindObject(hs->pEncodingMap, pCKey->pbData, PtrIndex);

    return pEncodingEntry;
}

static TCascFile * CreateFileHandle(TCascStorage * hs, PCASC_INDEX_ENTRY pIndexEntry)
{
    ULONGLONG FileOffsMask = ((ULONGLONG)1 << hs->IndexFile[0].SegmentBits) - 1;
    ULONGLONG FileOffset = ConvertBytesToInteger_5(pIndexEntry->FileOffsetBE);
    TCascFile * hf;

    // Allocate the CASC file structure
    hf = (TCascFile *)CASC_ALLOC(TCascFile, 1);
    if(hf != NULL)
    {
        // Initialize the structure
        memset(hf, 0, sizeof(TCascFile));
        hf->ArchiveIndex = (DWORD)(FileOffset >> hs->IndexFile[0].SegmentBits);
        hf->HeaderOffset = (DWORD)(FileOffset & FileOffsMask);
        hf->szClassName = "TCascFile";

        // Copy the file size. Note that for all files except ENCODING,
        // this is the compressed file size
        hf->CompressedSize = ConvertBytesToInteger_4_LE(pIndexEntry->FileSizeLE);

        // For now, we set the file size to be equal to compressed size
        // This is used when loading the ENCODING file, which does not
        // have entry in the encoding table
        hf->FileSize = hf->CompressedSize;

        // Increment the number of references to the archive
        hs->dwRefCount++;
        hf->hs = hs;
    }

    return hf;
}

static bool OpenFileByEKey(TCascStorage * hs, PQUERY_KEY pEKey, DWORD dwFlags, TCascFile ** ppCascFile)
{
    PCASC_INDEX_ENTRY pIndexEntry;
    int nError = ERROR_SUCCESS;

    CASCLIB_UNUSED(dwFlags);

    // Find the key entry in the array of file keys
    pIndexEntry = FindIndexEntry(hs, pEKey);
    if(pIndexEntry == NULL)
        nError = ERROR_FILE_NOT_FOUND;

    // Create the file handle structure
    if(nError == ERROR_SUCCESS)
    {
        ppCascFile[0] = CreateFileHandle(hs, pIndexEntry);
        if(ppCascFile[0] == NULL)
            nError = ERROR_FILE_NOT_FOUND;
    }

#ifdef CASCLIB_TEST
    if(nError == ERROR_SUCCESS && ppCascFile[0] != NULL)
    {
        ppCascFile[0]->FileSize_IdxEntry = ConvertBytesToInteger_4_LE(pIndexEntry->FileSizeLE);
    }
#endif

    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

static bool OpenFileByCKey(TCascStorage * hs, PQUERY_KEY pCKey, DWORD dwFlags, TCascFile ** ppCascFile)
{
    PCASC_ENCODING_ENTRY pEncodingEntry;
    QUERY_KEY EKey;

    // Find the encoding entry
    pEncodingEntry = FindEncodingEntry(hs, pCKey, NULL);
    if(pEncodingEntry == NULL)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return false;
    }

    // Prepare the file index and open the file by index
    // Note: We don't know what to do if there is more than just one EKey
    // We always take the first file present. Is that correct?
    EKey.pbData = GET_EKEY(pEncodingEntry);
    EKey.cbData = MD5_HASH_SIZE;
    if(OpenFileByEKey(hs, &EKey, dwFlags, ppCascFile))
    {
        // Check if the file handle was created
        if(ppCascFile[0] != NULL)
        {
            // Fill-in the file size. For all files except ENCODING,
            // this overrides the value stored in the index entry.
            ppCascFile[0]->FileSize = ConvertBytesToInteger_4(pEncodingEntry->FileSizeBE);

#ifdef CASCLIB_TEST
            ppCascFile[0]->FileSize_EncEntry = ConvertBytesToInteger_4(pEncodingEntry->FileSizeBE);
#endif
            return true;
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// Public functions

bool WINAPI CascOpenFileByEKey(HANDLE hStorage, PQUERY_KEY pEKey, DWORD dwFlags, HANDLE * phFile)
{
    TCascStorage * hs;

    // Validate the storage handle
    hs = IsValidStorageHandle(hStorage);
    if(hs == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Validate the other parameters
    if(pEKey == NULL || pEKey->pbData == NULL || pEKey->cbData == 0 || phFile == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Use the internal function to open the file
    return OpenFileByEKey(hs, pEKey, dwFlags, (TCascFile **)phFile);
}

bool WINAPI CascOpenFileByCKey(HANDLE hStorage, PQUERY_KEY pCKey, DWORD dwFlags, HANDLE * phFile)
{
    TCascStorage * hs;

    // Validate the storage handle
    hs = IsValidStorageHandle(hStorage);
    if(hs == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Validate the other parameters
    if(pCKey == NULL || pCKey->pbData == NULL || pCKey->cbData == 0 || phFile == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Use the internal function fo open the file
    return OpenFileByCKey(hs, pCKey, dwFlags, (TCascFile **)phFile);
}

bool WINAPI CascOpenFile(HANDLE hStorage, const char * szFileName, DWORD dwLocale, DWORD dwFlags, HANDLE * phFile)
{
    TCascStorage * hs;
    QUERY_KEY QueryKey;
    LPBYTE pbQueryKey = NULL;
    BYTE KeyBuffer[MD5_HASH_SIZE];
    DWORD dwOpenMode = 0;
    bool bOpenResult = false;
    int nError = ERROR_SUCCESS;

    CASCLIB_UNUSED(dwLocale);

    // Validate the storage handle
    hs = IsValidStorageHandle(hStorage);
    if(hs == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Validate the other parameters
    if(szFileName == NULL || szFileName[0] == 0 || phFile == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Retrieve the CKey/EKey from the file name in different modes
    switch(dwFlags & CASC_OPEN_TYPE_MASK)
    {
        case CASC_OPEN_BY_NAME:
            
            // Retrieve the file CKey/EKey
            pbQueryKey = RootHandler_GetKey(hs->pRootHandler, szFileName);
            if(pbQueryKey == NULL)
            {
                nError = ERROR_FILE_NOT_FOUND;
                break;
            }

            // Set the proper open mode
            dwOpenMode = (hs->pRootHandler->dwRootFlags & ROOT_FLAG_USES_EKEY) ? CASC_OPEN_BY_EKEY : CASC_OPEN_BY_CKEY;
            break;

        case CASC_OPEN_BY_CKEY:

            // Convert the string to binary
            nError = ConvertStringToBinary(szFileName, MD5_STRING_SIZE, KeyBuffer);
            if(nError != ERROR_SUCCESS)
                break;
            
            // Proceed opening by CKey
            dwOpenMode = CASC_OPEN_BY_CKEY;
            pbQueryKey = KeyBuffer;
            break;

        case CASC_OPEN_BY_EKEY:

            // Convert the string to binary
            nError = ConvertStringToBinary(szFileName, MD5_STRING_SIZE, KeyBuffer);
            if(nError != ERROR_SUCCESS)
                break;
            
            // Proceed opening by EKey
            dwOpenMode = CASC_OPEN_BY_EKEY;
            pbQueryKey = KeyBuffer;
            break;

        default:

            // Unknown open mode
            nError = ERROR_INVALID_PARAMETER;
            break;
    }

    // Perform the open operation
    if(nError == ERROR_SUCCESS)
    {
        // Setup the CKey/EKey
        QueryKey.pbData = pbQueryKey;
        QueryKey.cbData = MD5_HASH_SIZE;

        // Either open by CKey or EKey
        switch(dwOpenMode)
        {
            case CASC_OPEN_BY_CKEY:
                bOpenResult = OpenFileByCKey(hs, &QueryKey, dwFlags, (TCascFile **)phFile);
                break;

            case CASC_OPEN_BY_EKEY:
                bOpenResult = OpenFileByEKey(hs, &QueryKey, dwFlags, (TCascFile **)phFile);
                break;

            default:
                SetLastError(ERROR_INVALID_PARAMETER);
                break;
        }

        // Handle the error code
        if(!bOpenResult)
        {
            assert(GetLastError() != ERROR_SUCCESS);
            nError = GetLastError();
        }
    }

    // Set the last error and return
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

DWORD WINAPI CascGetFileId(HANDLE hStorage, const char * szFileName)
{
    TCascStorage * hs;

    // Validate the storage handle
    hs = IsValidStorageHandle(hStorage);
    if (hs == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Validate the other parameters
    if (szFileName == NULL || szFileName[0] == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    return RootHandler_GetFileId(hs->pRootHandler, szFileName);
}

bool WINAPI CascCloseFile(HANDLE hFile)
{
    TCascFile * hf;

    hf = IsValidFileHandle(hFile);
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

