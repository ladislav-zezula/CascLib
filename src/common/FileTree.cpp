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

#define START_ITEM_COUNT          0x4000

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

//-----------------------------------------------------------------------------
// Protected functions

PCASC_FILE_NODE CASC_FILE_TREE::InsertNew(
    PCASC_CKEY_ENTRY pCKeyEntry,
    ULONGLONG FileNameHash,
    const char * szNodeName,
    const char * szNodeNameEnd,
    DWORD FileDataId,
    DWORD Parent)
{
    PCASC_FILE_NODE pFileNode;
    char * szInsertedName = NULL;
    void * SaveItemArray = FileTable.ItemArray();   // We need to save the array pointers. If it changes, we must rebuild both maps
    DWORD NameIndex = 0;
    USHORT NameLength = 0;

    // Sanity check
    assert(FileNameHash != 0 || FileDataId != 0);

    // If we have name for the node, we need to insert it to the name array
    if(szNodeName != NULL && szNodeNameEnd > szNodeName)
    {
        // Insert the name fragment to the name table
        szInsertedName = (char *)NameTable.Insert(szNodeName, (szNodeNameEnd - szNodeName));
        if(szInsertedName == NULL)
            return NULL;

        // Get the name offset
        NameLength = (USHORT)(szNodeNameEnd - szNodeName);
        NameIndex = (DWORD)NameTable.IndexOf(szInsertedName);
    }

    // Create a brand new node
    pFileNode = (PCASC_FILE_NODE)FileTable.Insert(NULL, 1);
    if(pFileNode != NULL)
    {
        // Setup the node
        memset(pFileNode, 0, FileTable.ItemSize());
        pFileNode->FileNameHash = FileNameHash;
        pFileNode->pCKeyEntry = pCKeyEntry;
        pFileNode->NameIndex = NameIndex;
        pFileNode->NameLength = NameLength;
        pFileNode->Parent = Parent;

        // Supply the FileDataId, FileSize, Locale and ContentFlags
        SetExtras(pFileNode, FileDataId, CASC_INVALID_ID, CASC_INVALID_ID);

        // Update the biggest file data id ever inserted
        if(FileDataIdOffset != 0 && FileDataId != CASC_INVALID_ID)
        {
            MinFileDataId = min(MinFileDataId, FileDataId);
            MaxFileDataId = max(MaxFileDataId, FileDataId);
        }

        // If the array pointer changed or we are close to the size of the array, we need to rebuild the maps
        if(FileTable.ItemArray() != SaveItemArray || (FileTable.ItemCount() * 3 / 2) > NameMap.HashTableSize())
        {
            // Rebuild both maps. Note that rebuilding also inserts all items to the maps, so no need to insert them here
            if(!RebuildNameMaps())
            {
                pFileNode = NULL;
                assert(false);
            }
        }
        else
        {
            // Insert the item to the map "FileNameHash -> CASC_FILE_NODE"
            if(NameMap.IsInitialized())
                NameMap.InsertObject(pFileNode, &pFileNode->FileNameHash);

            // Insert the item to the array "FileDataId -> CASC_FILE_NODE"
            if(FileDataIds.IsInitialized())
                InsertNew(pFileNode, FileDataId);
        }
    }

    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::InsertNew(PCASC_FILE_NODE pFileNode, DWORD FileDataId)
{
    PCASC_FILE_NODE * RefElement;

    if(FileDataId != CASC_INVALID_ID && FileDataIds.IsInitialized())
    {
        // Insert the element to the array
        RefElement = (PCASC_FILE_NODE *)FileDataIds.InsertAt(FileDataId);
        if(RefElement != NULL)
        {
            RefElement[0] = pFileNode;
            return pFileNode;
        }
    }

    return NULL;
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

bool CASC_FILE_TREE::SetKeyLength(DWORD aKeyLength)
{
    if(aKeyLength > MD5_HASH_SIZE)
        return false;
    KeyLength = aKeyLength;
    return true;
}

DWORD CASC_FILE_TREE::GetMaxFileDataId()
{
    return MaxFileDataId;
}

bool CASC_FILE_TREE::RebuildNameMaps()
{
    PCASC_FILE_NODE pFileNode;
    size_t nMaxItems = FileTable.ItemCountMax();

    // Free the map of "FullName -> CASC_FILE_NODE"
    NameMap.Free();

    // Create new map map "FullName -> CASC_FILE_NODE"
    if(NameMap.Create(nMaxItems, sizeof(ULONGLONG), FIELD_OFFSET(CASC_FILE_NODE, FileNameHash)) != ERROR_SUCCESS)
        return false;

    // Reset the entire array, but keep the buffer allocated
    FileDataIds.Reset();

    // Parse all items and insert them to the map
    for(size_t i = 0; i < FileTable.ItemCount(); i++)
    {
        // Retrieve the n-th object
        pFileNode = (PCASC_FILE_NODE)FileTable.ItemAt(i);

        // Insert it to the map "FileNameHash -> CASC_FILE_NODE"
        if(pFileNode != NULL && pFileNode->FileNameHash != 0)
            NameMap.InsertObject(pFileNode, &pFileNode->FileNameHash);

        // Insert it to the array "FileDataId -> CASC_FILE_NODE"
        if(FileDataIds.IsInitialized())
        {
            DWORD FileDataId = CASC_INVALID_ID;

            GetExtras(pFileNode, &FileDataId, NULL, NULL);
            InsertNew(pFileNode, FileDataId);
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
    MaxFileDataId = 0;
    KeyLength = MD5_HASH_SIZE;

    // Shall we use the data ID in the tree node?
    if(Flags & FTREE_FLAG_USE_DATA_ID)
    {
        // Set the offset of the file data id in the entry
        FileDataIdOffset = FileNodeSize;
        FileNodeSize += sizeof(DWORD);

        // Create the array for FileDataId -> CASC_FILE_NODE
        nError = FileDataIds.Create<PCASC_FILE_NODE>(START_ITEM_COUNT);
        if(nError != ERROR_SUCCESS)
            return nError;
    }

    // Shall we use the locale ID in the tree node?
    if(Flags & FTREE_FLAG_USE_LOCALE_FLAGS)
    {
        LocaleFlagsOffset = FileNodeSize;
        FileNodeSize += sizeof(DWORD);
    }

    if(Flags & FTREE_FLAG_USE_CONTENT_FLAGS)
    {
        ContentFlagsOffset = FileNodeSize;
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
                SetExtras(pRootNode, CASC_INVALID_ID, CASC_INVALID_ID, CASC_INVALID_ID);
            }
        }
    }

    // Create both maps
    if(!RebuildNameMaps())
        nError = ERROR_NOT_ENOUGH_MEMORY;
    return nError;
}

void CASC_FILE_TREE::Free()
{
    // Free both arrays
    FileTable.Free();
    NameTable.Free();
    FileDataIds.Free();

    // Free the name map
    NameMap.Free();

    // Zero the object
    memset(this, 0, sizeof(CASC_FILE_TREE));
}

PCASC_FILE_NODE CASC_FILE_TREE::Insert(PCASC_CKEY_ENTRY pCKeyEntry, const char * szFullPath, DWORD FileDataId, DWORD LocaleFlags, DWORD ContentFlags)
{
    PCASC_FILE_NODE pFileNode = NULL;
    ULONGLONG FileNameHash = 0;
    const char * szNodeBegin = szFullPath;
    char szPathBuffer[MAX_PATH+1];
    DWORD Parent = 0;
    size_t i;

    // Mark the CKey entry as referenced by root handler
    pCKeyEntry->RefCount++;

    // Do we have a file name?
    if(szFullPath != NULL)
    {
        // Traverse the entire path. For each subfolder, we insert an appropriate fake entry
        for(i = 0; szFullPath[i] != 0; i++)
        {
            char chOneChar = szFullPath[i];

            // Is there a path separator?
            // Note: Warcraft III paths may contain "mount points".
            // Example: "frFR-War3Local.mpq:Maps/FrozenThrone/Campaign/NightElfX06Interlude.w3x:war3map.j"
            if(chOneChar == '\\' || chOneChar == '/' || chOneChar == ':')
            {
                // Calculate hash of the file name
                FileNameHash = CalcNormNameHash(szPathBuffer, i);

                // If the entry is not there yet, create new one
                if((pFileNode = Find(FileNameHash)) == NULL)
                {
                    // Insert new entry to the tree
                    pFileNode = InsertNew(NULL, FileNameHash, szNodeBegin, szFullPath + i, CASC_INVALID_ID, Parent);
                    if(pFileNode == NULL)
                        return NULL;

                    // Populate the file entry
                    pFileNode->Flags |= (chOneChar == ':') ? CFN_FLAG_MOUNT_POINT : 0;
                    pFileNode->Flags |= CFN_FLAG_FOLDER;
                }

                // Get the new parent item
                Parent = (DWORD)FileTable.IndexOf(pFileNode);

                // Also reset the begin of the node
                szNodeBegin = szFullPath + i + 1;
            }

            // Copy the next character, even if it was slash/backslash before
            szPathBuffer[i] = AsciiToUpperTable_BkSlash[chOneChar];
        }

        // If there is anything left, we insert it as file name
        if((szFullPath + i) > szNodeBegin)
        {
            FileNameHash = CalcNormNameHash(szPathBuffer, i);
            pFileNode = InsertNew(pCKeyEntry, FileNameHash, szNodeBegin, szFullPath + i, FileDataId, Parent);
        }
    }
    else
    {
        // Try to find the file node by file data ID
        pFileNode = InsertNew(pCKeyEntry, 0, NULL, NULL, FileDataId, 0);
    }

    // Did we insert anything?
    if(pFileNode != NULL)
        SetExtras(pFileNode, FileDataId, LocaleFlags, ContentFlags);
    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::Insert(PCASC_CKEY_ENTRY pCKeyEntry, ULONGLONG FileNameHash, DWORD FileDataId, DWORD LocaleFlags, DWORD ContentFlags)
{
    PCASC_FILE_NODE pFileNode = InsertNew(pCKeyEntry, FileNameHash, NULL, NULL, FileDataId, 0);

    if(pFileNode != NULL)
        SetExtras(pFileNode, FileDataId, LocaleFlags, ContentFlags);
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

    // If we have FileDataId, then we need to enumerate the files by FileDataId
    if(FileDataIdOffset != 0)
        pFileNode = *(PCASC_FILE_NODE *)FileDataIds.ItemAt(nItemIndex);
    else
        pFileNode = (PCASC_FILE_NODE)FileTable.ItemAt(nItemIndex);

    // Query the item's file name. If the item doesn't have name, don't create anything
    if(pFileNode != NULL && pFileNode->NameLength != 0)
    {
        // Construct the full path
        nLength = MakePath(pFileNode, szBuffer, cchBuffer);
        szBuffer[nLength] = 0;
    }
    
    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::Find(const char * szFullPath, DWORD FileDataId, PCASC_FIND_DATA pFindData)
{
    PCASC_FILE_NODE pFileNode = NULL;
    ULONGLONG FileNameHash;

    // Can we search by FileDataId?
    if(FileDataIds.IsInitialized() && (FileDataId != CASC_INVALID_ID || IsFileDataIdName(szFullPath, FileDataId)))
    {
        pFileNode = FindById(FileDataId);
    }
    else
    {
        if(szFullPath != NULL && szFullPath[0] != 0)
        {
            FileNameHash = CalcFileNameHash(szFullPath);
            pFileNode = (PCASC_FILE_NODE)NameMap.FindObject(&FileNameHash);
        }
    }

    // Did we find anything?
    if(pFileNode != NULL && pFindData != NULL)
    {
        GetExtras(pFileNode, &pFindData->dwFileDataId, &pFindData->dwLocaleFlags, &pFindData->dwContentFlags);
        pFindData->bCanOpenByName   = (pFileNode->FileNameHash != 0);
        pFindData->bCanOpenByDataId = (FileDataIdOffset != 0);
    }

    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::Find(PCASC_CKEY_ENTRY pCKeyEntry)
{
    PCASC_FILE_NODE pFileNode;

    for(size_t i = 0; i < FileTable.ItemCount(); i++)
    {
        pFileNode = (PCASC_FILE_NODE)FileTable.ItemAt(i);
        if((pFileNode->Flags & (CFN_FLAG_FOLDER | CFN_FLAG_MOUNT_POINT)) == 0)
        {
            if(pFileNode->pCKeyEntry == pCKeyEntry)
                return pFileNode;
        }
    }

    return NULL;
}

PCASC_FILE_NODE CASC_FILE_TREE::Find(ULONGLONG FileNameHash)
{
    return (PCASC_FILE_NODE)NameMap.FindObject(&FileNameHash);
}

PCASC_FILE_NODE CASC_FILE_TREE::FindById(DWORD FileDataId)
{
    PCASC_FILE_NODE * RefElement;
    PCASC_FILE_NODE pFileNode = NULL;

    if(FileDataId != CASC_INVALID_ID && FileDataIds.IsInitialized())
    {
        // Insert the element to the array
        RefElement = (PCASC_FILE_NODE *)FileDataIds.ItemAt(FileDataId);
        if(RefElement != NULL)
        {
            pFileNode = RefElement[0];
        }
    }

    return pFileNode;
}

size_t CASC_FILE_TREE::GetCount()
{
    return FileTable.ItemCount();
}

size_t CASC_FILE_TREE::IndexOf(PCASC_FILE_NODE pFileNode)
{
    return FileTable.IndexOf(pFileNode);
}

void CASC_FILE_TREE::GetExtras(PCASC_FILE_NODE pFileNode, PDWORD PtrFileDataId, PDWORD PtrLocaleFlags, PDWORD PtrContentFlags)
{
    DWORD FileDataId = CASC_INVALID_ID;
    DWORD LocaleFlags = CASC_INVALID_ID;
    DWORD ContentFlags = CASC_INVALID_ID;

    // Retrieve the data ID, if supported
    if(PtrFileDataId != NULL)
    {
        if(FileDataIdOffset != 0)
            FileDataId = GET_NODE_INT32(pFileNode, FileDataIdOffset);
        PtrFileDataId[0] = FileDataId;
    }

    // Retrieve the locale ID, if supported
    if(PtrLocaleFlags != NULL)
    {
        if(LocaleFlagsOffset != 0)
            LocaleFlags = GET_NODE_INT32(pFileNode, LocaleFlagsOffset);
        PtrLocaleFlags[0] = LocaleFlags;
    }

    if(PtrContentFlags != NULL)
    {
        if(ContentFlagsOffset != 0)
            ContentFlags = GET_NODE_INT32(pFileNode, ContentFlagsOffset);
        PtrContentFlags[0] = ContentFlags;
    }
}

void CASC_FILE_TREE::SetExtras(PCASC_FILE_NODE pFileNode, DWORD FileDataId, DWORD LocaleFlags, DWORD ContentFlags)
{
    // Set the file data ID, if supported
    if(FileDataIdOffset != 0)
    {
        SET_NODE_INT32(pFileNode, FileDataIdOffset, FileDataId);
    }

    // Set the locale ID, if supported
    if(LocaleFlagsOffset != 0)
    {
        SET_NODE_INT32(pFileNode, LocaleFlagsOffset, LocaleFlags);
    }

    // Set the locale ID, if supported
    if(ContentFlagsOffset != 0)
    {
        SET_NODE_INT32(pFileNode, ContentFlagsOffset, ContentFlags);
    }
}
