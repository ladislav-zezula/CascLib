/*****************************************************************************/
/* CascCommon.cpp                         Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Common functions for CascLib                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascCommon.cpp                  */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local defines

typedef bool (WINAPI * OPEN_FILE)(HANDLE hStorage, PQUERY_KEY pCKey, DWORD dwFlags, HANDLE * phFile);

//-----------------------------------------------------------------------------
// Functions

LPBYTE LoadInternalFileToMemory(TCascStorage * hs, LPBYTE pbQueryKey, DWORD dwOpenFlags, DWORD * pcbFileData)
{
    QUERY_KEY QueryKey;
    LPBYTE pbFileData = NULL;
    HANDLE hFile = NULL;
    DWORD cbFileData = pcbFileData[0];
    DWORD dwBytesRead = 0;
    bool bOpenResult = false;
    int nError = ERROR_SUCCESS;

    // Prepare the query key
    QueryKey.pbData = pbQueryKey;
    QueryKey.cbData = MD5_HASH_SIZE;
    dwOpenFlags |= CASC_STRICT_DATA_CHECK;

    // Open the file
    if((dwOpenFlags & CASC_OPEN_TYPE_MASK) == CASC_OPEN_BY_CKEY)
        bOpenResult = CascOpenFileByCKey((HANDLE)hs, &QueryKey, dwOpenFlags, &hFile);
    else
        bOpenResult = CascOpenFileByEKey((HANDLE)hs, NULL, &QueryKey, dwOpenFlags, cbFileData, &hFile);

    // Load the internal file
    if(bOpenResult)
    {
        // Retrieve the size of the file. Note that the caller might specify
        // the real size of the file, in case the file size is not retrievable
        // or if the size is wrong. Example: ENCODING file has size specified in BUILD
        if(cbFileData == 0 || cbFileData == CASC_INVALID_SIZE)
        {
            cbFileData = CascGetFileSize(hFile, NULL);
            if(cbFileData == 0 || cbFileData == CASC_INVALID_SIZE)
                nError = ERROR_FILE_CORRUPT;
        }

        // Retrieve the size of the ENCODING file
        if(nError == ERROR_SUCCESS)
        {
            // Allocate space for the ENCODING file
            pbFileData = CASC_ALLOC(BYTE, cbFileData);
            if(pbFileData != NULL)
            {
                // Read the entire file to memory
                CascReadFile(hFile, pbFileData, cbFileData, &dwBytesRead);
                if(dwBytesRead != cbFileData)
                {
                    nError = ERROR_FILE_CORRUPT;
                }
            }
            else
            {
                nError = ERROR_NOT_ENOUGH_MEMORY;
            }
        }

        // Close the file
        CascCloseFile(hFile);
    }
    else
    {
        nError = GetLastError();
    }

    // Handle errors
    if(nError != ERROR_SUCCESS)
    {
        // Free the file data
        if(pbFileData != NULL)
            CASC_FREE(pbFileData);
        pbFileData = NULL;

        // Set the last error
        SetLastError(nError);
    }
    else
    {
        // Give the loaded file length
        if(pcbFileData != NULL)
            *pcbFileData = cbFileData;
    }

    return pbFileData;
}

void FreeCascBlob(PQUERY_KEY pBlob)
{
    if(pBlob != NULL)
    {
        if(pBlob->pbData != NULL)
            CASC_FREE(pBlob->pbData);

        pBlob->pbData = NULL;
        pBlob->cbData = 0;
    }
}
