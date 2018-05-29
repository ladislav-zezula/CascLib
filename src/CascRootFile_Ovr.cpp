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

typedef struct _CASC_FILE_ENTRY
{
    CONTENT_KEY CKey;                               // Content key
    ULONGLONG FileNameHash;                         // File name hash
    DWORD dwFileName;                               // Offset of the file name in the name cache
} CASC_FILE_ENTRY, *PCASC_FILE_ENTRY;

struct TRootHandler_Ovr : public TRootHandler
{
    // Linear global list of file entries
    CASC_ARRAY FileTable;

    // Linear global list of names
    CASC_ARRAY FileNames;

    // Global map of FileName -> FileEntry
    PCASC_MAP pRootMap;
};

//-----------------------------------------------------------------------------
// Local functions

static int InsertFileEntry(
    TRootHandler_Ovr * pRootHandler,
    const char * szFileName,
    LPBYTE pbCKey)
{
    PCASC_FILE_ENTRY pFileEntry;
    size_t nLength = strlen(szFileName);

    // Attempt to insert the file name to the global buffer
    szFileName = (char *)Array_Insert(&pRootHandler->FileNames, szFileName, nLength + 1);
    if(szFileName == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Attempt to insert the entry to the array of file entries
    pFileEntry = (PCASC_FILE_ENTRY)Array_Insert(&pRootHandler->FileTable, NULL, 1);
    if(pFileEntry == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill the file entry
    pFileEntry->CKey  = *(PCONTENT_KEY)pbCKey;
    pFileEntry->FileNameHash = CalcFileNameHash(szFileName);
    pFileEntry->dwFileName   = (DWORD)Array_IndexOf(&pRootHandler->FileNames, szFileName);

    // Insert the file entry to the map
    assert(Map_FindObject(pRootHandler->pRootMap, &pFileEntry->FileNameHash, NULL) == NULL);
    Map_InsertObject(pRootHandler->pRootMap, pFileEntry, &pFileEntry->FileNameHash);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// Implementation of Overwatch root file

static int OvrHandler_Insert(
    TRootHandler_Ovr * pRootHandler,
    const char * szFileName,
    LPBYTE pbCKey)
{
    return InsertFileEntry(pRootHandler, szFileName, pbCKey);
}

static LPBYTE OvrHandler_Search(TRootHandler_Ovr * pRootHandler, TCascSearch * pSearch)
{
    PCASC_FILE_ENTRY pFileEntry;

    // Are we still inside the root directory range?
    while(pSearch->IndexLevel1 < pRootHandler->FileTable.ItemCount)
    {
        // Retrieve the file item
        pFileEntry = (PCASC_FILE_ENTRY)Array_ItemAt(&pRootHandler->FileTable, pSearch->IndexLevel1);
        strcpy(pSearch->szFileName, (char *)Array_ItemAt(&pRootHandler->FileNames, pFileEntry->dwFileName));        
        
        // Prepare the pointer to the next search
        pSearch->IndexLevel1++;
        return pFileEntry->CKey.Value;
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

static DWORD OvrHandler_GetFileId(TRootHandler_Ovr * /* pRootHandler */, const char * /* szFileName */)
{
    // Not implemented for Overwatch
    return 0;
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
//
// Overwatch ROOT file (build 24919):
// -------------------------------------
// #MD5|CHUNK_ID|FILENAME|INSTALLPATH
// FE3AD8A77EEF77B383DF4929AED816FD|0|RetailClient/GameClientApp.exe|GameClientApp.exe
// 5EDDEFECA544B6472C5CD52BE63BC02F|0|RetailClient/Overwatch Launcher.exe|Overwatch Launcher.exe
// 6DE09F0A67F33F874F2DD8E2AA3B7AAC|0|RetailClient/ca-bundle.crt|ca-bundle.crt
// 99FE9EB6A4BB20209202F8C7884859D9|0|RetailClient/ortp_x64.dll|ortp_x64.dll
//
// Overwatch ROOT file (build 47161):
// -------------------------------------
// #FILEID|MD5|CHUNK_ID|PRIORITY|MPRIORITY|FILENAME|INSTALLPATH
// RetailClient/Overwatch.exe|807F96661280C07E762A8C129FEBDA6F|0|0|255|RetailClient/Overwatch.exe|Overwatch.exe
// RetailClient/Overwatch Launcher.exe|5EDDEFECA544B6472C5CD52BE63BC02F|0|0|255|RetailClient/Overwatch Launcher.exe|Overwatch Launcher.exe
// RetailClient/ortp_x64.dll|7D1B5DEC267480F3E8DAD6B95143A59C|0|0|255|RetailClient/ortp_x64.dll|ortp_x64.dll
//

int RootHandler_CreateOverwatch(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_Ovr * pRootHandler;
    CONTENT_KEY CKey;
    const char * szLineBegin;
    const char * szLineEnd;
    void * pTextFile;
    size_t nLength;
    char szFileName[MAX_PATH+1];
    DWORD dwFileCountMax = (DWORD)hs->pCKeyEntryMap->TableSize;
    int nFileNameIndex = 0;
    int nCKeyIndex = 0;
    int nError = ERROR_BAD_FORMAT;

    // Verify whether the ROOT file seems line overwarch root file
    pTextFile = ListFile_FromBuffer(pbRootFile, cbRootFile);
    if(pTextFile != NULL)
    {
        // Get the initial line, containing variable names
        nLength = ListFile_GetNextLine(pTextFile, &szLineBegin, &szLineEnd);
        if(nLength != 0)
        {        
            // Determine the index of the "FILENAME" variable
            nError = CSV_GetHeaderIndex(szLineBegin, szLineEnd, "FILENAME", &nFileNameIndex);
            if(nError == ERROR_SUCCESS)
            {
                nError = CSV_GetHeaderIndex(szLineBegin, szLineEnd, "MD5", &nCKeyIndex);
                if(nError == ERROR_SUCCESS)
                {
                    // Allocate the root handler object
                    hs->pRootHandler = pRootHandler = CASC_ALLOC(TRootHandler_Ovr, 1);
                    if(pRootHandler == NULL)
                        return ERROR_NOT_ENOUGH_MEMORY;

                    // Fill-in the handler functions
                    memset(pRootHandler, 0, sizeof(TRootHandler_Ovr));
                    pRootHandler->Insert      = (ROOT_INSERT)OvrHandler_Insert;
                    pRootHandler->Search      = (ROOT_SEARCH)OvrHandler_Search;
                    pRootHandler->EndSearch   = (ROOT_ENDSEARCH)OvrHandler_EndSearch;
                    pRootHandler->GetKey      = (ROOT_GETKEY)OvrHandler_GetKey;
                    pRootHandler->Close       = (ROOT_CLOSE)OvrHandler_Close;
                    pRootHandler->GetFileId   = (ROOT_GETFILEID)OvrHandler_GetFileId;

                    // Fill-in the flags
                    pRootHandler->dwRootFlags |= ROOT_FLAG_HAS_NAMES;

                    // Allocate the linear array of file entries
                    nError = Array_Create(&pRootHandler->FileTable, CASC_FILE_ENTRY, 0x10000);
                    if(nError != ERROR_SUCCESS)
                        return nError;

                    // Allocate the buffer for the file names
                    nError = Array_Create(&pRootHandler->FileNames, char, 0x10000);
                    if(nError != ERROR_SUCCESS)
                        return nError;

                    // Create map of FileName -> FileEntry
                    pRootHandler->pRootMap = Map_Create(dwFileCountMax, sizeof(ULONGLONG), FIELD_OFFSET(CASC_FILE_ENTRY, FileNameHash));
                    if(pRootHandler->pRootMap == NULL)
                        return ERROR_NOT_ENOUGH_MEMORY;

                    // Parse the next lines
                    while((nLength = ListFile_GetNextLine(pTextFile, &szLineBegin, &szLineEnd)) > 0)
                    {
                        // Parse the line
                        nError = CSV_GetNameAndCKey(szLineBegin, szLineEnd, nFileNameIndex, nCKeyIndex, szFileName, _maxchars(szFileName), &CKey);
                        if(nError == ERROR_SUCCESS)
                        {
                            InsertFileEntry(pRootHandler, szFileName, CKey.Value);
                        }
                    }
                }
            }
        }
        ListFile_Free(pTextFile);
    }

    // Succeeded
    return nError;
}
