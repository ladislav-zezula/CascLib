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

inline DWORD GET_NODE_INT32(void * node, size_t offset)
{
    PDWORD PtrValue = (PDWORD)((LPBYTE)node + offset);
    
    return PtrValue[0];
}

inline void SET_NODE_INT32(void * node, size_t offset, DWORD value)
{
    PDWORD PtrValue = (PDWORD)((LPBYTE)node + offset);
    
    PtrValue[0] = value;
}

static bool IsFileDataIdName(const char * szFileName, DWORD & FileDataId)
{
    BYTE BinaryValue[4];

    // File name must begin with "File", case insensitive
    if(AsciiToUpperTable_BkSlash[szFileName[0]] == 'F' &&
       AsciiToUpperTable_BkSlash[szFileName[1]] == 'I' &&
       AsciiToUpperTable_BkSlash[szFileName[2]] == 'L' &&
       AsciiToUpperTable_BkSlash[szFileName[3]] == 'E')
    {
        // Then, 8 hexadecimal digits must follow
        if(ConvertStringToBinary(szFileName + 4, 8, BinaryValue) == ERROR_SUCCESS)
        {
            // Must be followed by an extension or end-of-string
            if(szFileName[0x0C] == 0 || szFileName[0x0C] == '.')
            {
                FileDataId = ConvertBytesToInteger_4(BinaryValue);
                return (FileDataId != CASC_INVALID_ID);
            }
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// Protected functions

PCASC_FILE_NODE CASC_FILE_TREE::GetOrInsert(
    ULONGLONG FileNameHash,
    const char * szNodeBegin,
    const char * szNodeEnd,
    PCONTENT_KEY pCKey,
    DWORD Parent,
    DWORD FileDataId)
{
    PCASC_FILE_NODE pFileNode = NULL;
    char * szInsertedName = NULL;
    void * SaveItemArray;
    DWORD NameIndex = 0;

    // Sanity check
    assert(FileNameHash != 0 || FileDataId != 0);

    // Check if the path fragment is already in the file table.
    // Use the preferred search method for this purpose
    switch (PrefferedSearch)
    {
        case CascSearchByFileNameHash:
            pFileNode = (PCASC_FILE_NODE)Map_FindObject(pNameMap, &FileNameHash);
            assert(FileNameHash != 0);
            break;

        case CascSearchByFileDataId:
            pFileNode = (PCASC_FILE_NODE)Map_FindObject(pFileDataIdMap, &FileDataId);
            assert(FileDataId != CASC_INVALID_ID);
            break;
    }

    // If the node is not there yet, we need to insert it
    if(pFileNode == NULL)
    {
        // If we have name for the node, we need to insert it to the name array
        if(szNodeBegin && szNodeEnd)
        {
            // Insert the name fragment to the name table
            szInsertedName = (char *)NameTable.Insert(szNodeBegin, (szNodeEnd - szNodeBegin));
            if(szInsertedName == NULL)
                return NULL;

            // Get the name offset
            NameIndex = (DWORD)NameTable.IndexOf(szInsertedName);
        }

        // We need to save the array pointers. If it changes, we must rebuild both maps
        SaveItemArray = FileTable.ItemArray();

        // Create a brand new node
        pFileNode = (PCASC_FILE_NODE)FileTable.Insert(NULL, 1);
        if(pFileNode != NULL)
        {
            // Setup the node
            memset(pFileNode, 0, FileTable.ItemSize());
            pFileNode->NameHash = FileNameHash;
            pFileNode->NameIndex = NameIndex;
            pFileNode->NameLength = (USHORT)(szNodeEnd - szNodeBegin);
            pFileNode->Parent = Parent;

            // Supply the CKey/EKey
            if(pCKey != NULL)
                pFileNode->CKey = *pCKey;

            // Supply the FileDataId, Locale and FileSize
            SetExtras(pFileNode, FileDataId, CASC_INVALID_SIZE, CASC_INVALID_ID);

            // Update the maximum file data id ever inserted
            if(FileDataId != CASC_INVALID_ID)
                MaxFileDataId = max(FileDataId, MaxFileDataId);

            // Did the array pointer change? If yes, then all items in the map are invalid now
            if(FileTable.ItemArray() == SaveItemArray)
            {
                if(pNameMap != NULL && FileNameHash != 0)
                    Map_InsertObject(pNameMap, pFileNode, &pFileNode->NameHash);
                if(pFileDataIdMap != NULL && FileDataId != CASC_INVALID_ID)
                    Map_InsertObject(pFileDataIdMap, pFileNode, &FileDataId);
            }
            else
            {
                if(!RebuildTreeMaps())
                {
                    pFileNode = NULL;
                    assert(false);
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

PCASC_FILE_NODE CASC_FILE_TREE::GetOrInsert(
    const char * szNormPath,
    size_t nLength,
    const char * szNodeBegin,
    const char * szNodeEnd,
    PCONTENT_KEY pCKey,
    DWORD Parent,
    DWORD FileDataId)
{
    return GetOrInsert(CalcNormNameHash(szNormPath, nLength), szNodeBegin, szNodeEnd, pCKey, Parent, FileDataId);
}

size_t CASC_FILE_TREE::MakePath(PCASC_FILE_NODE pFileNode, char * szBuffer, size_t cchBuffer)
{
    PCASC_FILE_NODE pParentNode;
    const char * szNamePtr;
    char * szBufferEnd = szBuffer + cchBuffer - 1;
    size_t nLength = 0;

    if(pFileNode->Parent != CASC_INVALID_INDEX)
    {
        // Copy all parents
        pParentNode = (PCASC_FILE_NODE)FileTable.ItemAt(pFileNode->Parent);
        if(pParentNode != NULL)
        {
            // Query the parent and move the buffer
            nLength = MakePath(pParentNode, szBuffer, cchBuffer);
            szBuffer = szBuffer + nLength;
        }

        // Retrieve the node name
        szNamePtr = (const char *)NameTable.ItemAt(pFileNode->NameIndex);

        // Check whether we have enough space
        if((szBuffer + pFileNode->NameLength) < szBufferEnd)
        {
            // Copy the path part
            memcpy(szBuffer, szNamePtr, pFileNode->NameLength);
            nLength += pFileNode->NameLength;

            // Append backslash
            if((pFileNode->Flags & CFN_FLAG_FOLDER) && ((szBuffer + 1) < szBufferEnd))
            {
                szBuffer[pFileNode->NameLength] = (pFileNode->Flags & CFN_FLAG_MOUNT_POINT) ? ':' : '\\';
                nLength++;
            }
        }
    }

    return nLength;
}

void CASC_FILE_TREE::SetPreferredSearchMethod(CASC_NODE_SEARCH_TYPE SearchMethod)
{
    PrefferedSearch = SearchMethod;
}


DWORD CASC_FILE_TREE::GetMaxFileDataId()
{
    return MaxFileDataId;
}

bool CASC_FILE_TREE::RebuildTreeMaps()
{
    PCASC_FILE_NODE pFileNode;
    size_t nMaxItems = FileTable.ItemCountMax();
    DWORD FileDataId = 0;

    // Free the map of FullName -> CASC_FILE_NODE
    if(pNameMap != NULL)
        Map_Free(pNameMap);
    pNameMap = NULL;

    // Free the map of FileDataId -> CASC_FILE_NODE
    if(pFileDataIdMap != NULL)
        Map_Free(pFileDataIdMap);
    pFileDataIdMap = NULL;

    // Create the name map
    pNameMap = Map_Create(nMaxItems, sizeof(ULONGLONG), FIELD_OFFSET(CASC_FILE_NODE, NameHash));
    if(pNameMap == NULL)
        return false;

    // Create the FileDataId map
    if(FileDataIdOffset != 0)
    {
        pFileDataIdMap = Map_Create(nMaxItems, sizeof(DWORD), FileDataIdOffset);
        if(pFileDataIdMap == NULL)
            return false;
    }

    // Parse all items and insert them to the map
    for(size_t i = 0; i < FileTable.ItemCount(); i++)
    {
        pFileNode = (PCASC_FILE_NODE)FileTable.ItemAt(i);
        if(pFileNode != NULL)
        {
            // Insert the file by name
            if(pNameMap != NULL && pFileNode->NameHash != 0)
                Map_InsertObject(pNameMap, pFileNode, &pFileNode->NameHash);

            // Insert the file by data ID, if supported
            if(FileDataIdOffset != 0)
            {
                GetExtras(pFileNode, &FileDataId, NULL, NULL);
                if(pFileDataIdMap != NULL && FileDataId != CASC_INVALID_ID)
                {
                    Map_InsertObject(pFileDataIdMap, pFileNode, &FileDataId);
                }
            }
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
// Public functions

int CASC_FILE_TREE::Create(DWORD Flags)
{
    PCASC_FILE_NODE pRootNode;
    size_t FileNodeSize = sizeof(CASC_FILE_NODE);
    int nError;

    // Initialize the file tree
    memset(this, 0, sizeof(CASC_FILE_TREE));
    PrefferedSearch = CascSearchByFileNameHash;
    MaxFileDataId = 0;

    // Shall we use the data ID in the tree node?
    if(Flags & FTREE_FLAG_USE_DATA_ID)
    {
        FileDataIdOffset = FileNodeSize;
        FileNodeSize += sizeof(DWORD);
    }

    // Shall we use the file size in the tree node?
    if(Flags & FTREE_FLAG_USE_FILE_SIZE)
    {
        FileSizeOffset = FileNodeSize;
        FileNodeSize += sizeof(DWORD);
    }

    // Shall we use the locale ID in the tree node?
    if(Flags & FTREE_FLAG_USE_LOCALE)
    {
        LocaleIdOffset = FileNodeSize;
        FileNodeSize += sizeof(DWORD);
    }

    // Initialize the dynamic array
    nError = FileTable.Create(FileNodeSize, START_ITEM_COUNT);
    if(nError == ERROR_SUCCESS)
    {
        // Create the dynamic array that will hold the node names
        nError = NameTable.Create<char>(START_ITEM_COUNT);
        if(nError == ERROR_SUCCESS)
        {
            // Insert the first "root" node, without name
            pRootNode = (PCASC_FILE_NODE)FileTable.Insert(NULL, 1);
            if(pRootNode != NULL)
            {
                // Initialize the node
                memset(pRootNode, 0, FileTable.ItemSize());
                pRootNode->Parent = CASC_INVALID_INDEX;
                pRootNode->NameIndex = CASC_INVALID_INDEX;
                pRootNode->Flags = CFN_FLAG_FOLDER;
                SetExtras(pRootNode, CASC_INVALID_ID, CASC_INVALID_SIZE, CASC_INVALID_ID);
            }
        }
    }

    // Create both maps
    if(!RebuildTreeMaps())
        nError = ERROR_NOT_ENOUGH_MEMORY;
    return nError;
}

void CASC_FILE_TREE::Free()
{
    // Free both maps
    Map_Free(pNameMap);
    Map_Free(pFileDataIdMap);
    
    // Free both arrays
    FileTable.Free();
    NameTable.Free();

    // Zero the object
    memset(this, 0, sizeof(CASC_FILE_TREE));
}

PCASC_FILE_NODE CASC_FILE_TREE::Insert(PCONTENT_KEY pCKey, const char * szFullPath, DWORD FileDataId, DWORD FileSize, DWORD LocaleId)
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
        // Note: Warcraft III paths may contain "mount points".
        // Example: "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j"
        if(chOneChar == '\\' || chOneChar == '/' || chOneChar == ':')
        {
            pFileNode = GetOrInsert(szNormPath, i, szNodeBegin, szFullPath + i, NULL, Parent, CASC_INVALID_ID);
            if(pFileNode == NULL)
                return NULL;

            // Supply the missing values
            pFileNode->Flags |= (chOneChar == ':') ? CFN_FLAG_MOUNT_POINT : 0;
            pFileNode->Flags |= CFN_FLAG_FOLDER;

            // Get the new parent item
            Parent = (DWORD)FileTable.IndexOf(pFileNode);

            // Also reset the begin of the node
            szNodeBegin = szFullPath + i + 1;
        }

        // Copy the next character, even if it was slash/backslash before
        szNormPath[i] = AsciiToUpperTable_BkSlash[chOneChar];
    }

    // If there is anything left, we insert it as file name
    if((szFullPath + i) > szNodeBegin)
    {
        pFileNode = GetOrInsert(szNormPath, i, szNodeBegin, szFullPath + i, pCKey, Parent, FileDataId);
        if(pFileNode != NULL)
        {
            DWORD ExistingFileDataId = CASC_INVALID_ID;
            DWORD ExistingFileSize = CASC_INVALID_SIZE;
            DWORD ExistingLocaleId = CASC_INVALID_ID;

            // Get the existing extra fields
            GetExtras(pFileNode, &ExistingFileDataId, &ExistingFileSize, &ExistingLocaleId);

            // Update whatever what is not set yet
            if(ExistingFileDataId == CASC_INVALID_ID)
                ExistingFileDataId = FileDataId;
            if(ExistingFileSize == CASC_INVALID_SIZE)
                ExistingFileSize = FileSize;
            if(ExistingLocaleId == CASC_INVALID_ID)
                ExistingLocaleId = LocaleId;

            // Update the fields
            SetExtras(pFileNode, ExistingFileDataId, ExistingFileSize, ExistingLocaleId);
        }
    }

    // Return the file node
    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::Insert(PCONTENT_KEY pCKey, ULONGLONG NameHash, DWORD FileDataId, DWORD FileSize, DWORD LocaleId)
{
    PCASC_FILE_NODE pFileNode;
    
    pFileNode = GetOrInsert(NameHash, NULL, NULL, pCKey, 0, FileDataId);
    if(pFileNode != NULL)
    {
        SetExtras(pFileNode, FileDataId, FileSize, LocaleId);
    }

    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::ItemAt(size_t nItemIndex)
{
    return (PCASC_FILE_NODE)FileTable.ItemAt(nItemIndex);
}

PCASC_FILE_NODE CASC_FILE_TREE::PathAt(char * szBuffer, size_t cchBuffer, size_t nItemIndex)
{
    PCASC_FILE_NODE pFileNode = NULL;
    size_t nLength;

    // Pre-set the buffer with zero
    szBuffer[0] = 0;

    // Query the item
    pFileNode = (PCASC_FILE_NODE)FileTable.ItemAt(nItemIndex);
    if(pFileNode != NULL && pFileNode->NameLength != 0)
    {
        // Construct the full path
        nLength = MakePath(pFileNode, szBuffer, cchBuffer);
        szBuffer[nLength] = 0;
    }
    
    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::Find(const char * szFullPath, PDWORD PtrFileSize)
{
    PCASC_FILE_NODE pFileNode;
    ULONGLONG FileNameHash = CalcFileNameHash(szFullPath);
    DWORD FileDataId = CASC_INVALID_ID;

    // Lookup the path in the name map
    pFileNode = (PCASC_FILE_NODE)Map_FindObject(pNameMap, &FileNameHash);
    if(pFileNode == NULL && FileDataIdOffset != 0 && IsFileDataIdName(szFullPath, FileDataId))
    {
        pFileNode = (PCASC_FILE_NODE)Map_FindObject(pFileDataIdMap, &FileDataId);
    }

    // Did we find anything?
    if(pFileNode != NULL && FileSizeOffset != 0)
    {
        GetExtras(pFileNode, NULL, PtrFileSize, NULL);
    }

    return pFileNode;
}
/*
PCASC_FILE_NODE CASC_FILE_TREE::Find(DWORD FileDataId, PDWORD PtrFileSize)
{
    PCASC_FILE_NODE pFileNode = NULL;

    // Lookup the path in the name map
    pFileNode = (PCASC_FILE_NODE)Map_FindObject(pIdMap, &FileDataId);
    if(pFileNode != NULL)
    {
        GetExtras(pFileNode, NULL, PtrFileSize, NULL);
    }

    return pFileNode;
}
*/
size_t CASC_FILE_TREE::GetCount()
{
    return FileTable.ItemCount();
}

size_t CASC_FILE_TREE::IndexOf(PCASC_FILE_NODE pFileNode)
{
    return FileTable.IndexOf(pFileNode);
}

void CASC_FILE_TREE::GetExtras(PCASC_FILE_NODE pFileNode, PDWORD PtrFileDataId, PDWORD PtrFileSize, PDWORD PtrLocaleId)
{
    DWORD FileSize = CASC_INVALID_SIZE;
    DWORD LocaleId = CASC_INVALID_ID;
    DWORD FileDataId = CASC_INVALID_ID;

    // Retrieve the data ID, if supported
    if(PtrFileDataId != NULL)
    {
        if(FileDataIdOffset != 0)
            FileDataId = GET_NODE_INT32(pFileNode, FileDataIdOffset);
        PtrFileDataId[0] = FileDataId;
    }

    // Retrieve the file size, if supported
    if(PtrFileSize != NULL)
    {
        if(FileSizeOffset != 0)
            FileSize = GET_NODE_INT32(pFileNode, FileSizeOffset);
        PtrFileSize[0] = FileSize;
    }

    // Retrieve the locale ID, if supported
    if(PtrLocaleId != NULL)
    {
        if(LocaleIdOffset != 0)
            LocaleId = GET_NODE_INT32(pFileNode, LocaleIdOffset);
        PtrLocaleId[0] = LocaleId;
    }
}

void CASC_FILE_TREE::SetExtras(PCASC_FILE_NODE pFileNode, DWORD FileDataId, DWORD FileSize, DWORD LocaleId)
{
    // Set the file data ID, if supported
    if(FileDataIdOffset != 0)
    {
        SET_NODE_INT32(pFileNode, FileDataIdOffset, FileDataId);
    }

    // Set the file size, if supported
    if(FileSizeOffset != 0)
    {
        SET_NODE_INT32(pFileNode, FileSizeOffset, FileSize);
    }

    // Set the locale ID, if supported
    if(LocaleIdOffset != 0)
    {
        SET_NODE_INT32(pFileNode, LocaleIdOffset, LocaleId);
    }
}
