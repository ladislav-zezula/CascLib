/*****************************************************************************/
/* FileTree.h                             Copyright (c) Ladislav Zezula 2018 */
/*---------------------------------------------------------------------------*/
/* Common implementation of a file tree object for various ROOt file formats */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.05.18  1.00  Lad  The first version of FileTree.h                      */
/*****************************************************************************/

#ifndef __FILETREE_H__
#define __FILETREE_H__

//-----------------------------------------------------------------------------
// Structures

#define FTREE_FLAG_USE_DATA_ID        0x0001        // The FILE_NODE also contains file data ID
#define FTREE_FLAG_USE_LOCALE_FLAGS   0x0002        // The FILE_NODE also contains file locale flags
#define FTREE_FLAG_USE_CONTENT_FLAGS  0x0004        // The FILE_NODE also contains content flags

#define CFN_FLAG_FOLDER               0x0001        // This item is a folder
#define CFN_FLAG_MOUNT_POINT          0x0002        // This item is a mount point.

// Common structure for holding a single folder/file node
typedef struct _CASC_FILE_NODE
{
    ULONGLONG FileNameHash;                         // Jenkins hash of the normalized file name (uppercase, backslashes)
    PCASC_CKEY_ENTRY pCKeyEntry;                    // Pointer to the CKey entry.
    DWORD Parent;                                   // The index of a parent directory. If CASC_INVALID_INDEX, then this is the root item
    DWORD NameIndex;                                // Index of the node name. If CASC_INVALID_INDEX, then this node has no name
    USHORT NameLength;                              // Length of the node name (without the zero terminator)
    USHORT Flags;                                   // See CFE_FLAG_XXX

    // Optional: DWORD FileDataId;                  // Only if FTREE_FLAG_USE_DATA_ID specified at create
    // Optional: DWORD FileSize;                    // Only if FTREE_FLAG_USE_FILE_SIZE specified at create
    // Optional: DWORD LocaleFlags;                 // Only if FTREE_FLAG_USE_LOCALE_FLAGS specified at create
    // Optional: DWORD ContentFlags;                // Only if FTREE_FLAG_USE_CONTENT_FLAGS specified at create

} CASC_FILE_NODE, *PCASC_FILE_NODE;

class CASC_FILE_TREE
{
    public:

    // Initializes/destroys the entire tree
    int Create(DWORD Flags = 0);
    void Free();

    // Inserts a new node to the tree; either with name or nameless
    PCASC_FILE_NODE Insert(PCASC_CKEY_ENTRY pCKeyEntry, const char * szFullPath, DWORD FileDataId = CASC_INVALID_ID, DWORD LocaleFlags = CASC_INVALID_ID, DWORD ContentFlags = CASC_INVALID_ID);
    PCASC_FILE_NODE Insert(PCASC_CKEY_ENTRY pCKeyEntry, ULONGLONG FileNameHash, DWORD FileDataId = CASC_INVALID_ID, DWORD LocaleFlags = CASC_INVALID_ID, DWORD ContentFlags = CASC_INVALID_ID);

    // Returns an item at the given index. The PathAt also builds the full path of the node
    PCASC_FILE_NODE ItemAt(size_t nItemIndex);
    PCASC_FILE_NODE PathAt(char * szBuffer, size_t cchBuffer, size_t nItemIndex);

    // Finds a file using its full path, FileDataId or CKey/EKey
    PCASC_FILE_NODE Find(const char * szFullPath, DWORD FileDataId, struct _CASC_FIND_DATA * pFindData);
    PCASC_FILE_NODE Find(PCASC_CKEY_ENTRY pCKeyEntry);
    PCASC_FILE_NODE Find(ULONGLONG FileNameHash);
    PCASC_FILE_NODE FindById(DWORD FileDataId);

    // Returns the number of items in the tree
    size_t GetCount();

    // Returns the index of an item in the tree
    size_t IndexOf(PCASC_FILE_NODE pFileNode);

    // Retrieves the extra values from the node (if supported)
    void GetExtras(PCASC_FILE_NODE pFileNode, PDWORD PtrFileDataId, PDWORD PtrLocaleFlags, PDWORD PtrContentFlags);
    void SetExtras(PCASC_FILE_NODE pFileNode, DWORD FileDataId, DWORD LocaleFlags, DWORD ContentFlags);

    // Change the length of the key
    bool SetKeyLength(DWORD KeyLength);

    // Retrieve the maximum FileDataId ever inserted
    DWORD GetMaxFileDataId();

    protected:

    PCASC_FILE_NODE InsertNew(PCASC_CKEY_ENTRY pCKeyEntry, ULONGLONG FileNameHash, const char * szNodeName, const char * szNodeNameEnd, DWORD FileDataId, DWORD Parent);
    PCASC_FILE_NODE InsertNew(PCASC_FILE_NODE pFileNode, DWORD FileDataId);

    size_t MakePath(PCASC_FILE_NODE pFileNode, char * szBuffer, size_t cchBuffer);
    bool RebuildNameMaps();

    CASC_ARRAY FileTable;                           // Dynamic array that holds all CASC_FILE_NODEs
    CASC_ARRAY NameTable;                           // Dynamic array that holds all node names

    CASC_ARRAY FileDataIds;                         // Dynamic array that maps FileDataId -> CASC_FILE_NODE
    CASC_MAP NameMap;                               // Map of FileNameHash -> CASC_FILE_NODE

    size_t FileDataIdOffset;                        // If nonzero, this is the offset of the "FileDataId" field in the CASC_FILE_NODE
    size_t LocaleFlagsOffset;                       // If nonzero, this is the offset of the "LocaleFlags" field in the CASC_FILE_NODE
    size_t ContentFlagsOffset;                      // If nonzero, this is the offset of the "ContentFlags" field in the CASC_FILE_NODE
    DWORD MinFileDataId;                            // The smallest value of FileDataId ever inserted
    DWORD MaxFileDataId;                            // The largest value of FileDataId ever inserted
    DWORD KeyLength;                                // Actual length of the key supported by the root handler
};

#endif // __FILETREE_H__
