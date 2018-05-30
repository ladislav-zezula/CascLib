/*****************************************************************************/
/* FileTree.cpp                           Copyright (c) Ladislav Zezula 2018 */
/*---------------------------------------------------------------------------*/
/* Common implementation of a file tree object for various ROOt file formats */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.05.18  1.00  Lad  The first version of FileTree.cpp                    */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Local defines

#define START_ITEM_COUNT    0x1000

//-----------------------------------------------------------------------------
// Local functions

static bool RebuildTreeMaps(PCASC_FILE_TREE pFileTree)
{
    PCASC_FILE_NODE pFileNode;
    size_t nMaxItems = pFileTree->FileTable.ItemCountMax;

    // Free the map of FullName -> CASC_FILE_NODE
    if(pFileTree->pNameMap != NULL)
        Map_Free(pFileTree->pNameMap);
    pFileTree->pNameMap = NULL;

    // Free the map of DataId -> CASC_FILE_NODE
    if(pFileTree->pIdMap != NULL)
        Map_Free(pFileTree->pIdMap);
    pFileTree->pIdMap = NULL;

    // Create both maps at once. Both must succeed.
    pFileTree->pNameMap = Map_Create(nMaxItems, sizeof(ULONGLONG), FIELD_OFFSET(CASC_FILE_NODE, NameHash));
    pFileTree->pIdMap   = Map_Create(nMaxItems, sizeof(DWORD), FIELD_OFFSET(CASC_FILE_NODE, DataId));
    if(pFileTree->pNameMap == NULL || pFileTree->pIdMap == NULL)
        return false;

    // Parse all items and insert them to the map
    for(size_t i = 0; i < pFileTree->FileTable.ItemCount; i++)
    {
        pFileNode = (PCASC_FILE_NODE)Array_ItemAt(&pFileTree->FileTable, i);
        if(pFileNode != NULL && pFileNode->NameHash != 0)
        {
            if(pFileNode->DataId != CASC_INVALID_ID)
                Map_InsertObject(pFileTree->pIdMap, pFileNode, &pFileNode->DataId);
            Map_InsertObject(pFileTree->pNameMap, pFileNode, &pFileNode->NameHash);
        }
    }

    return true;
}

static PCASC_FILE_NODE FileTree_GetOrInsert(
    PCASC_FILE_TREE pFileTree,
    const char * szNormPath,
    size_t nLength,
    const char * szNodeBegin,
    const char * szNodeEnd,
    PCONTENT_KEY pCKey,
    DWORD Parent,
    DWORD DataId)
{
    PCASC_FILE_NODE pFileNode;
    ULONGLONG FileNameHash = CalcNormNameHash(szNormPath, nLength);
    char * szInsertedName;
    void * SaveItemArray;

    // Check if the path fragment is already in the file table
    pFileNode = (PCASC_FILE_NODE)Map_FindObject(pFileTree->pNameMap, &FileNameHash);

    // If the node is not there yet, we need to insert it
    if(pFileNode == NULL)
    {
        // Insert the name fragment to the name table
        szInsertedName = (char *)Array_Insert(&pFileTree->NameTable, szNodeBegin, (szNodeEnd - szNodeBegin));
        if(szInsertedName != NULL)
        {
            // We need to save the array pointers. If it changes, we must rebuild both maps
            SaveItemArray = pFileTree->FileTable.ItemArray;

            // Create a brand new node
            pFileNode = (PCASC_FILE_NODE)Array_Insert(&pFileTree->FileTable, NULL, 1);
            if(pFileNode != NULL)
            {
                // Setup the node
                memset(pFileNode, 0, pFileTree->FileTable.ItemSize);
                pFileNode->NameHash = FileNameHash;
                pFileNode->NameIndex = (DWORD)Array_IndexOf(&pFileTree->NameTable, szInsertedName);
                pFileNode->NameLength = (USHORT)(szNodeEnd - szNodeBegin);
                pFileNode->Parent = Parent;

                // Supply the CKey/EKey
                if(pCKey != NULL)
                    pFileNode->CKey = *pCKey;

                // Supply the Data ID
                pFileNode->DataId = DataId;

                // Did the array pointer change? If yes, then all items in the map are invalid now
                if(pFileTree->FileTable.ItemArray == SaveItemArray)
                {
                    if(pFileNode->DataId != CASC_INVALID_ID)
                        Map_InsertObject(pFileTree->pIdMap, pFileNode, &pFileNode->DataId);
                    Map_InsertObject(pFileTree->pNameMap, pFileNode, &pFileNode->NameHash);
                }
                else
                {
                    if(!RebuildTreeMaps(pFileTree))
                    {
                        pFileNode = NULL;
                        assert(false);
                    }
                }
            }
        }
    }
    else
    {
        assert(pFileNode->NameLength == (USHORT)(szNodeEnd - szNodeBegin));
        assert(pFileNode->NameHash == FileNameHash);
    }

    return pFileNode;
}

static size_t FileTree_MakePath(PCASC_FILE_TREE pFileTree, PCASC_FILE_NODE pFileNode, char * szBuffer, size_t cchBuffer)
{
    PCASC_FILE_NODE pParentNode;
    const char * szNamePtr;
    char * szBufferEnd = szBuffer + cchBuffer - 1;
    size_t nLength = 0;

    if(pFileNode->Parent != CASC_INVALID_INDEX)
    {
        // Copy all parents
        pParentNode = (PCASC_FILE_NODE)Array_ItemAt(&pFileTree->FileTable, pFileNode->Parent);
        if(pParentNode != NULL)
        {
            // Query the parent and move the buffer
            nLength = FileTree_MakePath(pFileTree, pParentNode, szBuffer, cchBuffer);
            szBuffer = szBuffer + nLength;
        }

        // Retrieve the node name
        szNamePtr = (const char *)Array_ItemAt(&pFileTree->NameTable, pFileNode->NameIndex);

        // Check whether we have enough space
        if((szBuffer + pFileNode->NameLength) < szBufferEnd)
        {
            // Copy the path part
            memcpy(szBuffer, szNamePtr, pFileNode->NameLength);
            nLength += pFileNode->NameLength;

            // Append backslash
            if((pFileNode->Flags & CFN_FLAG_FOLDER) && ((szBuffer + 1) < szBufferEnd))
            {
                szBuffer[pFileNode->NameLength] = '\\';
                nLength++;
            }
        }
    }

    return nLength;
}

//-----------------------------------------------------------------------------
// Public functions

int FileTree_Create(PCASC_FILE_TREE pFileTree, size_t FileNodeSize)
{
    PCASC_FILE_NODE pRootNode;
    int nError;

    // The size must be greater or equal to the size of the CASC_FILE_NODE structure
    assert(FileNodeSize >= sizeof(CASC_FILE_NODE));

    // Initialize the file tree
    memset(pFileTree, 0, sizeof(CASC_FILE_TREE));

    // Initialize the dynamic array
    nError = Array_Create_(&pFileTree->FileTable, FileNodeSize, START_ITEM_COUNT);
    if(nError == ERROR_SUCCESS)
    {
        // Create the dynamic array that will hold the node names
        nError = Array_Create(&pFileTree->NameTable, char, START_ITEM_COUNT);
        if(nError == ERROR_SUCCESS)
        {
            // Insert the first "root" node, without name
            pRootNode = (PCASC_FILE_NODE)Array_Insert(&pFileTree->FileTable, NULL, 1);
            if(pRootNode != NULL)
            {
                // Initialize the node
                memset(pRootNode, 0, pFileTree->FileTable.ItemSize);
                pRootNode->Parent = CASC_INVALID_INDEX;
                pRootNode->NameIndex = CASC_INVALID_INDEX;
                pRootNode->DataId = CASC_INVALID_ID;
                pRootNode->Flags = CFN_FLAG_FOLDER;

                // Set the next data ID
                pFileTree->NextDataId = 1;
            }
        }
    }

    // Create both maps
    if(!RebuildTreeMaps(pFileTree))
        nError = ERROR_NOT_ENOUGH_MEMORY;
    return nError;
}

void * FileTree_Insert(PCASC_FILE_TREE pFileTree, PCONTENT_KEY pCKey, const char * szFullPath, DWORD DataId)
{
    PCASC_FILE_NODE pFileNode = NULL;
    const char * szNodeBegin = szFullPath;
    char szNormPath[MAX_PATH+1];
    DWORD Parent = 0;
    size_t i;

    // Traverse the entire path and make sure that all subdirs are there
    for(i = 0; szFullPath[i] != 0; i++)
    {
        char chOneChar = szFullPath[i];

        // Is there a path separator?
        if(chOneChar == '\\' || chOneChar == '/')
        {
            pFileNode = FileTree_GetOrInsert(pFileTree, szNormPath, i, szNodeBegin, szFullPath + i, NULL, Parent, CASC_INVALID_ID);
            if(pFileNode == NULL)
                return NULL;

            // Supply the missing values
            pFileNode->Flags |= CFN_FLAG_FOLDER;

            // Get the new parent item
            Parent = (DWORD)Array_IndexOf(&pFileTree->FileTable, pFileNode);

            // Also reset the begin of the node
            szNodeBegin = szFullPath + i + 1;
        }

        // Copy the next character, even if it was slash/backslash before
        szNormPath[i] = AsciiToUpperTable_BkSlash[chOneChar];
    }

    // If there is anything left, we insert it as file name
    if((szFullPath + i) > szNodeBegin)
    {
        pFileNode = FileTree_GetOrInsert(pFileTree, szNormPath, i, szNodeBegin, szFullPath + i, pCKey, Parent, DataId);
    }

    return pFileNode;
}

void * FileTree_ItemAt(PCASC_FILE_TREE pFileTree, size_t nItemIndex)
{
    void * pFileNode = NULL;

    if(pFileTree != NULL)
        pFileNode = Array_ItemAt(&pFileTree->FileTable, nItemIndex);;
    return pFileNode;
}

void * FileTree_PathAt(PCASC_FILE_TREE pFileTree, char * szBuffer, size_t cchBuffer, size_t nItemIndex)
{
    PCASC_FILE_NODE pFileNode = NULL;
    size_t nLength;

    // Pre-set the buffer with zero
    szBuffer[0] = 0;

    // Query the item
    pFileNode = (PCASC_FILE_NODE)Array_ItemAt(&pFileTree->FileTable, nItemIndex);
    if(pFileNode != NULL && pFileNode->NameLength != 0)
    {
        // Construct the full path
        nLength = FileTree_MakePath(pFileTree, pFileNode, szBuffer, cchBuffer);
        szBuffer[nLength] = 0;
    }
    
    return pFileNode;
}

void * FileTree_Find(PCASC_FILE_TREE pFileTree, const char * szFullPath)
{
    ULONGLONG FileNameHash = CalcFileNameHash(szFullPath);

    // Lookup the path in the name map
    return Map_FindObject(pFileTree->pNameMap, &FileNameHash);
}

size_t FileTree_GetCount(PCASC_FILE_TREE pFileTree)
{
    size_t Count = 0;

    if(pFileTree != NULL)
        Count = pFileTree->FileTable.ItemCount;
    return Count;
}

size_t FileTree_IndexOf(PCASC_FILE_TREE pFileTree, const void * TreeNode)
{
    assert(pFileTree != NULL);
    return Array_IndexOf(&pFileTree->FileTable, TreeNode);
}

void FileTree_Free(PCASC_FILE_TREE pFileTree)
{
    if(pFileTree != NULL)
    {
        // Free both maps
        Map_Free(pFileTree->pNameMap);
        Map_Free(pFileTree->pIdMap);
        
        // Free both arrays
        Array_Free(&pFileTree->FileTable);
        Array_Free(&pFileTree->NameTable);

        // Zero the object
        memset(pFileTree, 0, sizeof(CASC_FILE_TREE));
    }
}
