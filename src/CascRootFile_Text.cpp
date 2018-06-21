/*****************************************************************************/
/* CascRootFile_Text.cpp                  Copyright (c) Ladislav Zezula 2017 */
/*---------------------------------------------------------------------------*/
/* Support for loading ROOT files in plain text                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 28.10.15  1.00  Lad  The first version of CascRootFile_Text.cpp           */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Structure definitions for text root files

struct TRootHandler_Text : public TRootHandler
{
    CASC_FILE_TREE FileTree;
};

//-----------------------------------------------------------------------------
// Local functions

static bool IsRootFile_Starcraft1(void * pTextFile)
{
    const char * szLineBegin;
    const char * szLineEnd;
    char szFileName[MAX_PATH];
    CONTENT_KEY CKey;
    bool bResult = false;

    // Get the first line from the listfile
    if(ListFile_GetNextLine(pTextFile, &szLineBegin, &szLineEnd))
    {
        // We check the line length; if the line is too long, we ignore it
        if((szLineEnd - szLineBegin) < (MAX_PATH + MD5_STRING_SIZE + 1))
        {
            if(CSV_GetNameAndCKey(szLineBegin, szLineEnd, 0, 1, szFileName, MAX_PATH, &CKey) == ERROR_SUCCESS)
            {
                bResult = true;
            }
        }
    }

    // We need to reset the listfile to the begin position
    ListFile_Reset(pTextFile);
    return bResult;
}

//-----------------------------------------------------------------------------
// Implementation of text root file

static int FileTreeHandler_Insert(
    TRootHandler_Text * pRootHandler,
    const char * szFileName,
    PCASC_CKEY_ENTRY pCKeyEntry)
{
    PCONTENT_KEY pCKey = (PCONTENT_KEY)pCKeyEntry->CKey;
    DWORD FileSize = ConvertBytesToInteger_4(pCKeyEntry->ContentSize);
    void * pItem;

    // We can support both mappings (FileName->CKey or FileName->CKey)
    if(pRootHandler->dwRootFlags & ROOT_FLAG_USES_EKEY)
    {
        if(pCKeyEntry->EKeyCount == 0)
            return ERROR_CAN_NOT_COMPLETE;
        pCKey = (PCONTENT_KEY)pCKeyEntry->EKey;
    }

    // Insert the entry
    pItem = FileTree_Insert(&pRootHandler->FileTree, pCKey, szFileName, FileSize);
    return (pItem != NULL) ? ERROR_SUCCESS : ERROR_CAN_NOT_COMPLETE;
}

static LPBYTE FileTreeHandler_Search(TRootHandler_Text * pRootHandler, TCascSearch * pSearch)
{
    PCASC_FILE_NODE pFileNode;
    size_t ItemCount = FileTree_GetCount(&pRootHandler->FileTree);

    // Are we still inside the root directory range?
    while(pSearch->IndexLevel1 < ItemCount)
    {
        // Retrieve the file item
        pFileNode = (PCASC_FILE_NODE)FileTree_PathAt(&pRootHandler->FileTree, pSearch->szFileName, MAX_PATH, pSearch->IndexLevel1);
        pSearch->IndexLevel1++;
        
        // Ignore folders
        if((pFileNode->Flags & CFN_FLAG_FOLDER) == 0)
        {
            // Check the wildcard
            if (CheckWildCard(pSearch->szFileName, pSearch->szMask))
            {
                FileTree_GetFileSize(&pRootHandler->FileTree, pFileNode, &pSearch->dwFileSize);
                return pFileNode->CKey.Value;
            }
        }
    }

    // No more entries
    return NULL;
}

static void FileTreeHandler_EndSearch(TRootHandler_Text * /* pRootHandler */, TCascSearch * /* pSearch */)
{
    // Do nothing
}

static LPBYTE FileTreeHandler_GetKey(TRootHandler_Text * pRootHandler, const char * szFileName, PDWORD PtrFileSize)
{
    PCASC_FILE_NODE pFileNode;

    pFileNode = (PCASC_FILE_NODE)FileTree_Find(&pRootHandler->FileTree, szFileName);
    if(pFileNode == NULL)
        return NULL;

    // Give the information
    FileTree_GetFileSize(&pRootHandler->FileTree, pFileNode, PtrFileSize);
    return pFileNode->CKey.Value;
}

static DWORD FileTreeHandler_GetFileId(TRootHandler_Text * /* pRootHandler */, const char * /* szFileName */)
{
    // Not implemented for text roots
    return 0;
}

static void FileTreeHandler_Close(TRootHandler_Text * pRootHandler)
{
    if(pRootHandler != NULL)
    {
        FileTree_Free(&pRootHandler->FileTree);
        CASC_FREE(pRootHandler);
    }
}

//-----------------------------------------------------------------------------
// Testing functions
/*
static void TestFileMap(LPBYTE pbRootFile, DWORD cbRootFile)
{
    PCASC_FILE_NODE pFileNode;
    CASC_FILE_TREE FileTree;
    CONTENT_KEY CKey;
    FILE * fp;
    const char * szLineBegin;
    const char * szLineEnd;
    void * pvListFile;
    size_t nItemCount;
    char szFileName[MAX_PATH];

    pvListFile = ListFile_FromBuffer(pbRootFile, cbRootFile);
    if(pvListFile != NULL)
    {
        if(FileTree_Create(&FileTree) == ERROR_SUCCESS)
        {
            fp = fopen("E:\\root_dump1.txt", "wt");
            if(fp != NULL)
            {
                while(ListFile_GetNextLine(pvListFile, &szLineBegin, &szLineEnd) != 0)
                {
                    void * pItem1;
                    void * pItem2;

                    // Retrieve the file namew and the content key
                    CSV_GetNameAndCKey(szLineBegin, szLineEnd, 0, 1, szFileName, MAX_PATH, &CKey);

                    szLineEnd = strchr(szLineBegin, '|');
                    assert(szLineEnd != NULL);

                    fwrite(szLineBegin, 1, (szLineEnd - szLineBegin), fp);
                    fprintf(fp, "\n");

                    // Insert the pair to the map
                    pItem1 = FileTree_Insert(&FileTree, &CKey, szFileName);
                    pItem2 = FileTree_Find(&FileTree, szFileName);
                    assert(pItem1 == pItem1);
                }
                fclose(fp);
            }

            fp = fopen("E:\\root_dump2.txt", "wt");
            if(fp != NULL)
            {
                // Enumerate tree items
                nItemCount = FileTree_GetCount(&FileTree);
                for(size_t i = 0; i < nItemCount; i++)
                {
                    pFileNode = (PCASC_FILE_NODE)FileTree_ItemAt(&FileTree, i);
                    if((pFileNode->Flags & CFN_FLAG_FOLDER) == 0)
                    {
                        FileTree_PathAt(&FileTree, szFileName, MAX_PATH, i);
                        fprintf(fp, "%s\n", szFileName);
                    }
                }
                fclose(fp);
            }

            FileTree_Free(&FileTree);
        }

        ListFile_Free(pvListFile);
    }
}
*/

//-----------------------------------------------------------------------------
// Public functions

void InitRootHandler_FileTree(TRootHandler * pRootHandler, size_t nStructSize)
{
    // Fill the entire structure with zeros
    memset(pRootHandler, 0, nStructSize);

    // Supply the Text handler pointers
    pRootHandler->Insert      = (ROOT_INSERT)FileTreeHandler_Insert;
    pRootHandler->Search      = (ROOT_SEARCH)FileTreeHandler_Search;
    pRootHandler->EndSearch   = (ROOT_ENDSEARCH)FileTreeHandler_EndSearch;
    pRootHandler->GetKey      = (ROOT_GETKEY)FileTreeHandler_GetKey;
    pRootHandler->Close       = (ROOT_CLOSE)FileTreeHandler_Close;
    pRootHandler->GetFileId   = (ROOT_GETFILEID)FileTreeHandler_GetFileId;
}

//
// Starcraft ROOT file is a text file with the following format:
// HD2/portraits/NBluCrit/NLCFID01.webm|c2795b120592355d45eba9cdc37f691e
// locales/enUS/Assets/campaign/EXPZerg/Zerg08/staredit/wav/zovtra01.ogg|316b0274bf2dabaa8db60c3ff1270c85
// locales/zhCN/Assets/sound/terran/ghost/tghdth01.wav|6637ed776bd22089e083b8b0b2c0374c
//

int RootHandler_CreateStarcraft1(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_Text * pRootHandler;
    CONTENT_KEY CKey;
    const char * szLineBegin;
    const char * szLineEnd;
    void * pTextFile;
    char szFileName[MAX_PATH];
    int nError = ERROR_BAD_FORMAT;

    // Parse the ROOT file first in order to see whether we have the correct format
    pTextFile = ListFile_FromBuffer(pbRootFile, cbRootFile);
    if(pTextFile != NULL)
    {
        // Verify whether this looks like a Starcraft I root file
        if(IsRootFile_Starcraft1(pTextFile))
        {
            // Allocate the root handler object
            hs->pRootHandler = pRootHandler = CASC_ALLOC(TRootHandler_Text, 1);
            if(pRootHandler == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            // Fill-in the handler functions
            InitRootHandler_FileTree(pRootHandler, sizeof(TRootHandler_Text));

            // Allocate the generic file tree
            nError = FileTree_Create(&pRootHandler->FileTree);
            if(nError == ERROR_SUCCESS)
            {
                while(ListFile_GetNextLine(pTextFile, &szLineBegin, &szLineEnd) != 0)
                {
                    // Retrieve the file name and the content key
                    if(CSV_GetNameAndCKey(szLineBegin, szLineEnd, 0, 1, szFileName, MAX_PATH, &CKey) == ERROR_SUCCESS)
                    {
                        // Insert the FileName+CKey to the file tree
//                      void * pItem1 = FileTree_Insert(&pRootHandler->FileTree, &CKey, szFileName);
//                      void * pItem2 = FileTree_Find(&pRootHandler->FileTree, szFileName);
//                      assert(pItem1 == pItem2);

                        // Insert the FileName+CKey to the file tree
                        FileTree_Insert(&pRootHandler->FileTree, &CKey, szFileName);
                    }
                }

                // Fill-in the flags
                pRootHandler->dwRootFlags |= ROOT_FLAG_HAS_NAMES;
            }
        }
        
        // Free the listfile object
        ListFile_Free(pTextFile);
    }

    return nError;
}

//
// -------------------------------------
// Overwatch ROOT file (build 24919):
// -------------------------------------
// #MD5|CHUNK_ID|FILENAME|INSTALLPATH
// FE3AD8A77EEF77B383DF4929AED816FD|0|RetailClient/GameClientApp.exe|GameClientApp.exe
// 5EDDEFECA544B6472C5CD52BE63BC02F|0|RetailClient/Overwatch Launcher.exe|Overwatch Launcher.exe
// 6DE09F0A67F33F874F2DD8E2AA3B7AAC|0|RetailClient/ca-bundle.crt|ca-bundle.crt
// 99FE9EB6A4BB20209202F8C7884859D9|0|RetailClient/ortp_x64.dll|ortp_x64.dll
//
// -------------------------------------
// Overwatch ROOT file (build 47161):
// -------------------------------------
// #FILEID|MD5|CHUNK_ID|PRIORITY|MPRIORITY|FILENAME|INSTALLPATH
// RetailClient/Overwatch.exe|807F96661280C07E762A8C129FEBDA6F|0|0|255|RetailClient/Overwatch.exe|Overwatch.exe
// RetailClient/Overwatch Launcher.exe|5EDDEFECA544B6472C5CD52BE63BC02F|0|0|255|RetailClient/Overwatch Launcher.exe|Overwatch Launcher.exe
// RetailClient/ortp_x64.dll|7D1B5DEC267480F3E8DAD6B95143A59C|0|0|255|RetailClient/ortp_x64.dll|ortp_x64.dll
//

// TODO: There is way more files in the Overwatch CASC storage than present in the ROOT file.
// Maybe one day we will decode the structure of the storage and add it here.
int RootHandler_CreateOverwatch(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_Text * pRootHandler;
    CONTENT_KEY CKey;
    const char * szLineBegin;
    const char * szLineEnd;
    void * pTextFile;
    size_t nLength;
    char szFileName[MAX_PATH+1];
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
                    hs->pRootHandler = pRootHandler = CASC_ALLOC(TRootHandler_Text, 1);
                    if(pRootHandler == NULL)
                        return ERROR_NOT_ENOUGH_MEMORY;

                    // Fill-in the handler functions
                    InitRootHandler_FileTree(pRootHandler, sizeof(TRootHandler_Text));

                    // Allocate the generic file tree
                    nError = FileTree_Create(&pRootHandler->FileTree);
                    if(nError == ERROR_SUCCESS)
                    {
                        while(ListFile_GetNextLine(pTextFile, &szLineBegin, &szLineEnd) != 0)
                        {
                            // Retrieve the file name and the content key
                            if(CSV_GetNameAndCKey(szLineBegin, szLineEnd, nFileNameIndex, nCKeyIndex, szFileName, MAX_PATH, &CKey) == ERROR_SUCCESS)
                            {
                                FileTree_Insert(&pRootHandler->FileTree, &CKey, szFileName);
                            }
                        }

                        // Fill-in the flags
                        pRootHandler->dwRootFlags |= ROOT_FLAG_HAS_NAMES;
                    }
                }
            }
        }
        
        // Free the listfile object
        ListFile_Free(pTextFile);
    }

    return nError;
}
