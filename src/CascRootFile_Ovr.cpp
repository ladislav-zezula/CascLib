/*****************************************************************************/
/* CascRootFile_Ovr.cpp                   Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Support for loading Overwatch ROOT file                                   */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 28.10.15  1.00  Lad  The first version of CascRootFile_Ovr.cpp            */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Structure definitions for Overwatch root file

typedef struct _FILE_ENTRY_OVRW
{
    ENCODING_KEY EncodingKey;                       // Encoding key
    ULONGLONG FileNameHash;                         // File name hash
    DWORD dwFileName;                               // Offset of the file name in the name cache
} FILE_ENTRY_OVRW, *PFILE_ENTRY_OVRW;

struct TRootHandler_Ovr : public TRootHandler
{
    // Linear global list of file entries
    DYNAMIC_ARRAY FileTable;

    // Linear global list of names
    DYNAMIC_ARRAY FileNames;

    // Global map of FileName -> FileEntry
    PCASC_MAP pRootMap;
};

//-----------------------------------------------------------------------------
// Local functions

static ULONGLONG CalcFileNameHash(const char * szFileName)
{
    char szNormName[MAX_PATH+1];
    size_t nLength = 0;
    uint32_t dwHashHigh = 0;
    uint32_t dwHashLow = 0;

    // Normalize the file name
    while(szFileName[0] != 0 && nLength < MAX_PATH)
        szNormName[nLength++] = AsciiToUpperTable_BkSlash[*szFileName++];

    // Calculate the HASH value of the normalized file name
    hashlittle2(szNormName, nLength, &dwHashHigh, &dwHashLow);
    return ((ULONGLONG)dwHashHigh << 0x20) | dwHashLow;
}

static int InsertFileEntry(
    TRootHandler_Ovr * pRootHandler,
    const char * szFileName,
    ENCODING_KEY & EncodingKey)
{
    PFILE_ENTRY_OVRW pFileEntry;
    size_t nLength = strlen(szFileName);

    // Attempt to insert the file name to the global buffer
    szFileName = (char *)Array_Insert(&pRootHandler->FileNames, szFileName, nLength + 1);
    if(szFileName == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Attempt to insert the entry to the array of file entries
    pFileEntry = (PFILE_ENTRY_OVRW)Array_Insert(&pRootHandler->FileTable, NULL, 1);
    if(pFileEntry == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill the file entry
    pFileEntry->EncodingKey  = EncodingKey;
    pFileEntry->FileNameHash = CalcFileNameHash(szFileName);
    pFileEntry->dwFileName   = Array_IndexOf(&pRootHandler->FileNames, szFileName);

    // Insert the file entry to the map
    assert(Map_FindObject(pRootHandler->pRootMap, &pFileEntry->FileNameHash, NULL) == NULL);
    Map_InsertObject(pRootHandler->pRootMap, pFileEntry, &pFileEntry->FileNameHash);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// Implementation of Overwatch root file

static LPBYTE OvrHandler_Search(TRootHandler_Ovr * pRootHandler, TCascSearch * pSearch, PDWORD /* PtrFileSize */, PDWORD /* PtrLocaleFlags */)
{
    PFILE_ENTRY_OVRW pFileEntry;

    // Are we still inside the root directory range?
    while(pSearch->IndexLevel1 < pRootHandler->FileTable.ItemCount)
    {
        // Retrieve the file item
        pFileEntry = (PFILE_ENTRY_OVRW)Array_ItemAt(&pRootHandler->FileTable, pSearch->IndexLevel1);
        strcpy(pSearch->szFileName, (char *)Array_ItemAt(&pRootHandler->FileNames, pFileEntry->dwFileName));        
        
        // Prepare the pointer to the next search
        pSearch->IndexLevel1++;
        return pFileEntry->EncodingKey.Value;
    }

    // No more entries
    return NULL;
}

static void OvrHandler_EndSearch(TRootHandler_Ovr * /* pRootHandler */, TCascSearch * /* pSearch */)
{
    // Do nothing
}

static LPBYTE OvrHandler_GetKey(TRootHandler_Ovr * pRootHandler, const char * szFileName)
{
    ULONGLONG FileNameHash = CalcFileNameHash(szFileName);

    return (LPBYTE)Map_FindObject(pRootHandler->pRootMap, &FileNameHash, NULL);
}

static void OvrHandler_Close(TRootHandler_Ovr * pRootHandler)
{
    if(pRootHandler != NULL)
    {
        // Free the file map
        if(pRootHandler->pRootMap)
            Map_Free(pRootHandler->pRootMap);
        pRootHandler->pRootMap = NULL;

        // Free the array of the file names and file items
        Array_Free(&pRootHandler->FileTable);
        Array_Free(&pRootHandler->FileNames);

        // Free the root file itself
        CASC_FREE(pRootHandler);
    }
}

//-----------------------------------------------------------------------------
// Public functions

int RootHandler_CreateOverwatch(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_Ovr * pRootHandler;
    ENCODING_KEY KeyBuffer;
    QUERY_KEY EncodingKey = {KeyBuffer.Value, MD5_HASH_SIZE};
    void * pTextFile;
    size_t nLength;
    char szOneLine[0x200];
    char szFileName[MAX_PATH+1];
    DWORD dwFileCountMax = hs->pEncodingMap->TableSize;
    int nError = ERROR_SUCCESS;

    // Allocate the root handler object
    pRootHandler = CASC_ALLOC(TRootHandler_Ovr, 1);
    if(pRootHandler == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill-in the handler functions
    memset(pRootHandler, 0, sizeof(TRootHandler_Ovr));
    pRootHandler->Search      = (ROOT_SEARCH)OvrHandler_Search;
    pRootHandler->EndSearch   = (ROOT_ENDSEARCH)OvrHandler_EndSearch;
    pRootHandler->GetKey      = (ROOT_GETKEY)OvrHandler_GetKey;
    pRootHandler->Close       = (ROOT_CLOSE)OvrHandler_Close;

    // Allocate the linear array of file entries
    nError = Array_Create(&pRootHandler->FileTable, FILE_ENTRY_OVRW, 0x10000);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Allocate the buffer for the file names
    nError = Array_Create(&pRootHandler->FileNames, char, 0x10000);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Create map of ROOT_ENTRY -> FileEntry
    pRootHandler->pRootMap = Map_Create(dwFileCountMax, sizeof(ULONGLONG), FIELD_OFFSET(FILE_ENTRY_OVRW, FileNameHash));
    if(pRootHandler->pRootMap == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill-in the flags
    pRootHandler->dwRootFlags |= ROOT_FLAG_HAS_NAMES;
    hs->pRootHandler = pRootHandler;
    
    // Parse the ROOT file
    pTextFile = ListFile_FromBuffer(pbRootFile, cbRootFile);
    if(pTextFile != NULL)
    {
        // Skip the first line, containing "#MD5|CHUNK_ID|FILENAME|INSTALLPATH"
        ListFile_GetNextLine(pTextFile, szOneLine, _maxchars(szOneLine));

        // Parse the next lines
        while((nLength = ListFile_GetNextLine(pTextFile, szOneLine, _maxchars(szOneLine))) > 0)
        {
            // Parse the line
            nError = ParseRootFileLine(szOneLine, szOneLine + nLength, &EncodingKey, szFileName, _maxchars(szFileName));
            if(nError == ERROR_SUCCESS)
            {
                InsertFileEntry(pRootHandler, szFileName, KeyBuffer);
            }
        }

        ListFile_Free(pTextFile);
    }

    // Succeeded
    return nError;
}
