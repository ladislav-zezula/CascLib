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

#define START_ITEM_COUNT          0x1000

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

PCASC_FILE_NODE CASC_FILE_TREE::GetOrInsert(
    ULONGLONG FileNameHash,
    const char * szNodeBegin,
    const char * szNodeEnd,
    PCONTENT_KEY pCKey,
    DWORD Parent,
    DWORD FileDataId)
{
    PCASC_FILE_NODE pFileNode;
    char * szInsertedName = NULL;
    void * SaveItemArray;
    DWORD NameIndex = 0;

    // Sanity check
    assert(FileNameHash != 0 || FileDataId != 0);

    // Check if the path fragment is already in the file table.
    pFileNode = FindFileNode(FileNameHash, FileDataId);

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

            // Supply the FileDataId, FileSize, Locale and ContentFlags
            SetExtras(pFileNode, FileDataId, CASC_INVALID_SIZE, CASC_INVALID_ID, CASC_INVALID_ID);

            // Update the biggest file data id ever inserted
            if(FileDataId != CASC_INVALID_ID)
            {
                MinFileDataId = min(MinFileDataId, FileDataId);
                MaxFileDataId = max(MaxFileDataId, FileDataId);
            }

            // Did the array pointer change? If yes, then all items in the map are invalid now
            if(FileTable.ItemArray() == SaveItemArray)
            {
                // Insert the item to the map "FileNameHash -> CASC_FILE_NODE"
                if(NameMap.IsInitialized())
                    NameMap.InsertObject(pFileNode, &pFileNode->NameHash);

                // Insert the item to the array "FileDataId -> CASC_FILE_NODE"
                if(FileDataIds.IsInitialized())
                    SetFileNodeById(pFileNode, FileDataId);
            }
            else
            {
                if(!RebuildNameMaps())
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

PCASC_FILE_NODE CASC_FILE_TREE::FindFileNode(const char * szFullPath, DWORD FileDataId)
{
    PCASC_FILE_NODE pFileNode = NULL;
    ULONGLONG FileNameHash;

    // Can we search by FileDataId?
    if(FileDataIds.IsInitialized() && (FileDataId != CASC_INVALID_ID || IsFileDataIdName(szFullPath, FileDataId)))
    {
        pFileNode = GetFileNodeById(FileDataId);
    }
    else
    {
        if(szFullPath != NULL && szFullPath[0] != 0)
        {
            FileNameHash = CalcFileNameHash(szFullPath);
            pFileNode = (PCASC_FILE_NODE)NameMap.FindObject(&FileNameHash);
        }
    }

    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::FindFileNode(ULONGLONG FileNameHash, DWORD FileDataId)
{
    PCASC_FILE_NODE pFileNode = NULL;

    // Can we search by FileDataId?
    if(FileDataIds.IsInitialized() && FileDataId != CASC_INVALID_ID)
    {
        pFileNode = GetFileNodeById(FileDataId);
    }
    else
    {
        if(FileNameHash != 0)
        {
            pFileNode = (PCASC_FILE_NODE)NameMap.FindObject(&FileNameHash);
        }
    }

    return pFileNode;
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
    if(NameMap.Create(nMaxItems, sizeof(ULONGLONG), FIELD_OFFSET(CASC_FILE_NODE, NameHash), true) != ERROR_SUCCESS)
        return false;

    // Reset the entire array, but keep the buffer allocated
    FileDataIds.Reset();

    // Parse all items and insert them to the map
    for(size_t i = 0; i < FileTable.ItemCount(); i++)
    {
        // Retrieve the n-th object
        pFileNode = (PCASC_FILE_NODE)FileTable.ItemAt(i);

        // Insert it to the map "FileNameHash -> CASC_FILE_NODE"
        if(pFileNode != NULL && pFileNode->NameHash != 0)
            NameMap.InsertObject(pFileNode, &pFileNode->NameHash);

        // Insert it to the array "FileDataId -> CASC_FILE_NODE"
        if(FileDataIds.IsInitialized())
        {
            DWORD FileDataId = CASC_INVALID_ID;

            GetExtras(pFileNode, &FileDataId, NULL, NULL, NULL);
            SetFileNodeById(pFileNode, FileDataId);
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

    // Shall we use the file size in the tree node?
    if(Flags & FTREE_FLAG_USE_FILE_SIZE)
    {
        FileSizeOffset = FileNodeSize;
        FileNodeSize += sizeof(DWORD);
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
                SetExtras(pRootNode, CASC_INVALID_ID, CASC_INVALID_SIZE, CASC_INVALID_ID, CASC_INVALID_ID);
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

PCASC_FILE_NODE CASC_FILE_TREE::Insert(PCONTENT_KEY pCKey, const char * szFullPath, DWORD FileDataId, DWORD FileSize, DWORD LocaleFlags, DWORD ContentFlags)
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
            DWORD ExistingLocaleFlags = CASC_INVALID_ID;
            DWORD ExistingContentFlags = CASC_INVALID_ID;

            // Get the existing extra fields
            GetExtras(pFileNode, &ExistingFileDataId, &ExistingFileSize, &ExistingLocaleFlags, &ExistingContentFlags);

            // Update whatever what is not set yet
            if(ExistingFileDataId == CASC_INVALID_ID)
                ExistingFileDataId = FileDataId;
            if(ExistingFileSize == CASC_INVALID_SIZE)
                ExistingFileSize = FileSize;
            if(ExistingLocaleFlags == CASC_INVALID_ID)
                ExistingLocaleFlags = LocaleFlags;
            if(ExistingContentFlags == CASC_INVALID_ID)
                ExistingContentFlags = ContentFlags;

            // Update the fields
            SetExtras(pFileNode, ExistingFileDataId, ExistingFileSize, ExistingLocaleFlags, ExistingContentFlags);
        }
    }

    // Return the file node
    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::Insert(PCONTENT_KEY pCKey, ULONGLONG NameHash, DWORD FileDataId, DWORD FileSize, DWORD LocaleFlags, DWORD ContentFlags)
{
    PCASC_FILE_NODE pFileNode;
    
    pFileNode = GetOrInsert(NameHash, NULL, NULL, pCKey, 0, FileDataId);
    if(pFileNode != NULL)
    {
        SetExtras(pFileNode, FileDataId, FileSize, LocaleFlags, ContentFlags);
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
        pFileNode = GetFileNodeById(FileDataId);
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
        GetExtras(pFileNode, &pFindData->dwFileDataId, &pFindData->dwFileSize, &pFindData->dwLocaleFlags, &pFindData->dwContentFlags);
        pFindData->bCanOpenByName   = (pFileNode->NameHash != 0);
        pFindData->bCanOpenByDataId = (FileDataIdOffset != 0);
    }

    return pFileNode;
}

PCASC_FILE_NODE CASC_FILE_TREE::Find(LPBYTE pbRootKey)
{
    PCASC_FILE_NODE pFileNode;

    for(size_t i = 0; i < FileTable.ItemCount(); i++)
    {
        pFileNode = (PCASC_FILE_NODE)FileTable.ItemAt(i);
        if((pFileNode->Flags & (CFN_FLAG_FOLDER | CFN_FLAG_MOUNT_POINT)) == 0)
        {
            if(!memcmp(pFileNode->CKey.Value, pbRootKey, KeyLength))
                return pFileNode;
        }
    }

    return NULL;
}

bool CASC_FILE_TREE::SetFileNodeById(PCASC_FILE_NODE pFileNode, DWORD FileDataId)
{
    PCASC_FILE_NODE * RefElement;

    if(FileDataId != CASC_INVALID_ID)
    {
        // Insert the element to the array
        RefElement = (PCASC_FILE_NODE *)FileDataIds.InsertAt(FileDataId);
        if(RefElement != NULL)
        {
            RefElement[0] = pFileNode;
            return true;
        }
    }

    return false;
}

PCASC_FILE_NODE CASC_FILE_TREE::GetFileNodeById(DWORD FileDataId)
{
    PCASC_FILE_NODE * RefElement;
    PCASC_FILE_NODE pFileNode = NULL;

    if(FileDataId != CASC_INVALID_ID)
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

void CASC_FILE_TREE::GetExtras(PCASC_FILE_NODE pFileNode, PDWORD PtrFileDataId, PDWORD PtrFileSize, PDWORD PtrLocaleFlags, PDWORD PtrContentFlags)
{
    DWORD FileDataId = CASC_INVALID_ID;
    DWORD FileSize = CASC_INVALID_SIZE;
    DWORD LocaleFlags = CASC_INVALID_ID;
    DWORD ContentFlags = CASC_INVALID_ID;

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

void CASC_FILE_TREE::SetExtras(PCASC_FILE_NODE pFileNode, DWORD FileDataId, DWORD FileSize, DWORD LocaleFlags, DWORD ContentFlags)
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
