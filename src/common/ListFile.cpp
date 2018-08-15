/*****************************************************************************/
/* ListFile.cpp                           Copyright (c) Ladislav Zezula 2004 */
/*---------------------------------------------------------------------------*/
/* Description:                                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 12.06.04  1.00  Lad  The first version of ListFile.cpp                    */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Listfile cache structure

typedef struct _LISTFILE_CACHE
{
    char * pBegin;                      // The begin of the listfile cache
    char * pPos;                        // Current position in the cache
    char * pEnd;                        // The last character in the file cache

    // Followed by the cache (variable length)

} LISTFILE_CACHE, *PLISTFILE_CACHE;

//-----------------------------------------------------------------------------
// Creating the listfile cache for the given amount of data

static PLISTFILE_CACHE CreateListFileCache(DWORD dwFileSize)
{
    PLISTFILE_CACHE pCache;

    // Allocate cache for one file block
    pCache = (PLISTFILE_CACHE)CASC_ALLOC(BYTE, sizeof(LISTFILE_CACHE) + dwFileSize);
    if(pCache != NULL)
    {
        // Set the initial pointers
        pCache->pBegin =
        pCache->pPos   = (char *)(pCache + 1);
        pCache->pEnd   = pCache->pBegin + dwFileSize;
    }

    // Return the cache
    return pCache;
}

//-----------------------------------------------------------------------------
// Functions for parsing an external listfile

void * ListFile_OpenExternal(const TCHAR * szListFile)
{
    PLISTFILE_CACHE pCache = NULL;
    TFileStream * pStream;
    ULONGLONG FileSize = 0;

    // Open the external listfile
    pStream = FileStream_OpenFile(szListFile, STREAM_FLAG_READ_ONLY);
    if(pStream != NULL)
    {
        // Retrieve the size of the external listfile
        FileStream_GetSize(pStream, &FileSize);
        if(0 < FileSize && FileSize <= 0x30000000)
        {
            // Create the in-memory cache for the entire listfile
            // The listfile does not have any data loaded yet
            pCache = CreateListFileCache((DWORD)FileSize);
            if(pCache != NULL)
            {
                if(!FileStream_Read(pStream, NULL, pCache->pBegin, (DWORD)FileSize))
                {
                    ListFile_Free(pCache);
                    pCache = NULL;
                }
            }
        }

        // Close the file stream
        FileStream_Close(pStream);
    }

    return pCache;
}

void * ListFile_FromBuffer(LPBYTE pbBuffer, DWORD cbBuffer)
{
    PLISTFILE_CACHE pCache = NULL;

    // Create the in-memory cache for the entire listfile
    // The listfile does not have any data loaded yet
    pCache = CreateListFileCache(cbBuffer);
    if(pCache != NULL)
        memcpy(pCache->pBegin, pbBuffer, cbBuffer);

    return pCache;
}

// Performs the MD5-based check on the listfile
bool ListFile_VerifyMD5(void * pvListFile, LPBYTE pbHashMD5)
{
    PLISTFILE_CACHE pCache = (PLISTFILE_CACHE)pvListFile;

    // Must be at the beginning
    assert(pCache->pPos == pCache->pBegin);

    // Verify the MD5 hash for the entire block
    return VerifyDataBlockHash(pCache->pBegin, (DWORD)(pCache->pEnd - pCache->pBegin), pbHashMD5);
}

size_t ListFile_GetNextLine(void * pvListFile, const char ** pszLineBegin, const char ** pszLineEnd)
{
    PLISTFILE_CACHE pCache = (PLISTFILE_CACHE)pvListFile;
    char * szExtraString = NULL;
    char * szLineBegin;
    char * szLineEnd;

    // Skip newlines, spaces, tabs and another non-printable stuff
    while(pCache->pPos < pCache->pEnd && pCache->pPos[0] <= 0x20)
        pCache->pPos++;

    // Remember the begin of the line
    szLineBegin = pCache->pPos;

    // Copy the remaining characters
    while(pCache->pPos < pCache->pEnd)
    {
        // If we have found a newline, stop loading
        // Note: the 0x85 char came from Overwatch build 24919
        if(pCache->pPos[0] == 0x0A || pCache->pPos[0] == 0x0D || pCache->pPos[0] == 0x85)
            break;

        // Blizzard listfiles can also contain information about patch:
        // Pass1\Files\MacOS\unconditional\user\Background Downloader.app\Contents\Info.plist~Patch(Data#frFR#base-frFR,1326)
        if(pCache->pPos[0] == '~')
            szExtraString = pCache->pPos;

        // Move the position by one character forward
        pCache->pPos++;
    }

    // Remember the end of the line
    szLineEnd = (szExtraString != NULL && szExtraString[0] == '~' && szExtraString[1] == 'P') ? szExtraString : pCache->pPos;

    // Give the caller the positions of the begin and end of the line
    pszLineBegin[0] = szLineBegin;
    pszLineEnd[0] = szLineEnd;
    return (size_t)(szLineEnd - szLineBegin);
}

size_t ListFile_GetNextLine(void * pvListFile, char * szBuffer, size_t nMaxChars)
{
    const char * szLineBegin = NULL;
    const char * szLineEnd = NULL;
    size_t nLength;

    // Retrieve the next line
    nLength = ListFile_GetNextLine(pvListFile, &szLineBegin, &szLineEnd);

    // Check the length
    if(nLength > nMaxChars)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }

    // Copy the line to the user buffer
    memcpy(szBuffer, szLineBegin, nLength);
    szBuffer[nLength] = 0;
    return nLength;
}

size_t ListFile_GetNext(void * pvListFile, const char * szMask, char * szBuffer, size_t nMaxChars)
{
    size_t nLength = 0;
    int nError = ERROR_SUCCESS;

    // Check for parameters
    for(;;)
    {
        // Read the (next) line
        nLength = ListFile_GetNextLine(pvListFile, szBuffer, nMaxChars);
        if(nLength == 0)
        {
            nError = ERROR_NO_MORE_FILES;
            break;
        }

        // If some mask entered, check it
        if(CheckWildCard(szBuffer, szMask))
        {
            nError = ERROR_SUCCESS;
            break;
        }
    }

    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return nLength;
}

// Resets the listfile to the begin
void ListFile_Reset(void * pvListFile)
{
    PLISTFILE_CACHE pCache = (PLISTFILE_CACHE)pvListFile;

    if(pCache != NULL)
    {
        pCache->pPos = pCache->pBegin;
    }
}

void ListFile_Free(void * pvListFile)
{
    if(pvListFile != NULL)
    {
        CASC_FREE(pvListFile);
    }
}
