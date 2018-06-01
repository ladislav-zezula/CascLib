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
    OPEN_FILE OpenFile;
    LPBYTE pbFileData = NULL;
    HANDLE hFile = NULL;
    DWORD cbFileData = 0;
    DWORD dwBytesRead = 0;
    int nError = ERROR_SUCCESS;

    // Prepare the query key
    QueryKey.pbData = pbQueryKey;
    QueryKey.cbData = MD5_HASH_SIZE;

    // Open the internal file
    OpenFile = (dwOpenFlags == CASC_OPEN_BY_CKEY) ? CascOpenFileByCKey : CascOpenFileByEKey;
    if(OpenFile((HANDLE)hs, &QueryKey, CASC_INVALID_SIZE, &hFile))
    {
        // Retrieve the size of the ENCODING file
        cbFileData = CascGetFileSize(hFile, NULL);
        if(cbFileData != 0)
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
        else
        {
            nError = ERROR_BAD_FORMAT;
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
