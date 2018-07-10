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

static bool IsFileDataIdName(const char * szFileName, DWORD & DataId)
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
                DataId = ConvertBytesToInteger_4(BinaryValue);
                return (DataId != CASC_INVALID_ID);
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
    DWORD DataId)
{
    PCASC_FILE_NODE pFileNode;
    char * szInsertedName = NULL;
    void * SaveItemArray;
    DWORD NameIndex = 0;

    // Check if the path fragment is already in the file table
    pFileNode = (PCASC_FILE_NODE)Map_FindObject(pNameMap, &FileNameHash);

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

            // Supply the DataId
            if(DataIdOffset != 0)
                SET_NODE_INT32(pFileNode, DataIdOffset, DataId);

            // Did the array pointer change? If yes, then all items in the map are invalid now
            if(FileTable.ItemArray() == SaveItemArray)
            {
                if(pNameMap != NULL)
                    Map_InsertObject(pNameMap, pFileNode, &pFileNode->NameHash);
                if(pIdMap != NULL && DataId != CASC_INVALID_ID)
                    Map_InsertObject(pIdMap, pFileNode, &DataId);
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
    DWORD DataId)
{
    return GetOrInsert(CalcNormNameHash(szNormPath, nLength), szNodeBegin, szNodeEnd, pCKey, Parent, DataId);
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

void CASC_FILE_TREE::SetExtras(PCASC_FILE_NODE pFileNode, DWORD FileSize, DWORD LocaleId)
{
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

bool CASC_FILE_TREE::RebuildTreeMaps()
{
    PCASC_FILE_NODE pFileNode;
    size_t nMaxItems = FileTable.ItemCountMax();
    DWORD DataId = 0;

    // Free the map of FullName -> CASC_FILE_NODE
    if(pNameMap != NULL)
        Map_Free(pNameMap);
    pNameMap = NULL;

    // Free the map of DataId -> CASC_FILE_NODE
    if(pIdMap != NULL)
        Map_Free(pIdMap);
    pIdMap = NULL;

    // Create the name map
    pNameMap = Map_Create(nMaxItems, sizeof(ULONGLONG), FIELD_OFFSET(CASC_FILE_NODE, NameHash));
    if(pNameMap == NULL)
        return false;

    // Create the DataId map
    if(DataIdOffset != 0)
    {
        pIdMap = Map_Create(nMaxItems, sizeof(DWORD), DataIdOffset);
        if(pIdMap == NULL)
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
            if(DataIdOffset != 0)
            {
                GetExtras(pFileNode, &DataId, NULL, NULL);
                if(pIdMap != NULL && DataId != CASC_INVALID_ID)
                {
                    Map_InsertObject(pIdMap, pFileNode, &DataId);
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

    // Shall we use the data ID in the tree node?
    if(Flags & FTREE_FLAG_USE_DATA_ID)
    {
        DataIdOffset = FileNodeSize;
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
    Map_Free(pIdMap);
    
    // Free both arrays
    FileTable.Free();
    NameTable.Free();

    // Zero the object
    memset(this, 0, sizeof(CASC_FILE_TREE));
}

PCASC_FILE_NODE CASC_FILE_TREE::Insert(PCONTENT_KEY pCKey, const char * szFullPath, DWORD DataId, DWORD FileSize, DWORD LocaleId)
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
        pFileNode = GetOrInsert(szNormPath, i, szNodeBegin, szFullPath + i, pCKey, Parent, DataId);
        if(pFileNode != NULL)
        {
            SetExtras(pFileNode, FileSize, LocaleId);
        }
    }

    // Return the file node
    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::Insert(PCONTENT_KEY pCKey, ULONGLONG NameHash, DWORD DataId, DWORD FileSize, DWORD LocaleId)
{
    PCASC_FILE_NODE pFileNode;
    
    pFileNode = GetOrInsert(NameHash, NULL, NULL, pCKey, 0, DataId);
    if(pFileNode != NULL)
    {
        SetExtras(pFileNode, FileSize, LocaleId);
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
    DWORD DataId = CASC_INVALID_ID;

    // Lookup the path in the name map
    pFileNode = (PCASC_FILE_NODE)Map_FindObject(pNameMap, &FileNameHash);
    if(pFileNode == NULL && DataIdOffset != 0 && IsFileDataIdName(szFullPath, DataId))
    {
        pFileNode = (PCASC_FILE_NODE)Map_FindObject(pIdMap, &DataId);
    }

    // Did we find anything?
    if(pFileNode != NULL && FileSizeOffset != 0)
    {
        GetExtras(pFileNode, NULL, PtrFileSize, NULL);
    }

    return pFileNode;
}
/*
PCASC_FILE_NODE CASC_FILE_TREE::Find(DWORD DataId, PDWORD PtrFileSize)
{
    PCASC_FILE_NODE pFileNode = NULL;

    // Lookup the path in the name map
    pFileNode = (PCASC_FILE_NODE)Map_FindObject(pIdMap, &DataId);
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

void CASC_FILE_TREE::GetExtras(PCASC_FILE_NODE pFileNode, PDWORD PtrDataId, PDWORD PtrFileSize, PDWORD PtrLocaleId)
{
    DWORD FileSize = CASC_INVALID_SIZE;
    DWORD LocaleId = CASC_INVALID_ID;
    DWORD DataId = CASC_INVALID_ID;

    // Retrieve the data ID, if supported
    if(PtrDataId != NULL)
    {
        if(DataIdOffset != 0)
            DataId = GET_NODE_INT32(pFileNode, DataIdOffset);
        if(DataId != CASC_INVALID_ID)
            PtrDataId[0] = DataId;
    }

    // Retrieve the file size, if supported
    if(PtrFileSize != NULL)
    {
        if(FileSizeOffset != 0)
            FileSize = GET_NODE_INT32(pFileNode, FileSizeOffset);
        if(FileSize != CASC_INVALID_SIZE)
            PtrFileSize[0] = FileSize;
    }

    // Retrieve the locale ID, if supported
    if(PtrLocaleId != NULL)
    {
        if(LocaleIdOffset != 0)
            LocaleId = GET_NODE_INT32(pFileNode, LocaleIdOffset);
        if(LocaleId != CASC_INVALID_ID)
            PtrLocaleId[0] = LocaleId;
    }
}
