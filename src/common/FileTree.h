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

#define FTREE_FLAG_USE_FILE_SIZE    0x0001          // The FILE_NODE also contains file size
#define FTREE_FLAG_USE_DATA_ID      0x0002          // The FILE_NODE also contains file data ID
#define FTREE_FLAG_USE_LOCALE       0x0004          // The FILE_NODE also contains file locale

#define CFN_FLAG_FOLDER             0x0001          // This item is a folder
#define CFN_FLAG_MOUNT_POINT        0x0002          // This item is a mount point.

// Common structure for holding a single folder/file node
typedef struct _CASC_FILE_NODE
{
    CONTENT_KEY CKey;                               // File CKey/EKey. If the key length is smaller, it is padded by zeros.
    ULONGLONG NameHash;                             // Jenkins hash of the normalized file name (uppercase, backslashes)
    DWORD Parent;                                   // The index of a parent directory. If CASC_INVALID_INDEX, then this is the root item
    DWORD NameIndex;                                // Index of the node name. If CASC_INVALID_INDEX, then this node has no name
    USHORT NameLength;                              // Length of the node name (without the zero terminator)
    USHORT Flags;                                   // See CFE_FLAG_XXX

    // Optional: DWORD FileSize;                    // Only if FTREE_FLAG_USE_FILE_SIZE specified at create
    // Optional: DWORD DataId;                      // Only if FTREE_FLAG_USE_DATA_ID specified at create
    // Optional: DWORD LocaleId;                    // Only if FTREE_FLAG_USE_LOCALE specified at create

} CASC_FILE_NODE, *PCASC_FILE_NODE;

class CASC_FILE_TREE
{
    public:

    // Initializes/destroys the entire tree
    int Create(DWORD Flags = 0);
    void Free();

    // Inserts a new node to the tree; either with name or nameless
    PCASC_FILE_NODE Insert(PCONTENT_KEY pCKey, const char * szFullPath = NULL, DWORD DataId = CASC_INVALID_ID, DWORD FileSize = CASC_INVALID_SIZE, DWORD LocaleId = CASC_INVALID_ID);
    PCASC_FILE_NODE Insert(PCONTENT_KEY pCKey, ULONGLONG NameHash, DWORD DataId = CASC_INVALID_ID, DWORD FileSize = CASC_INVALID_SIZE, DWORD LocaleId = CASC_INVALID_ID);

    // Returns an item at the given index. The PathAt also builds the full path of the node
    PCASC_FILE_NODE ItemAt(size_t nItemIndex);
    PCASC_FILE_NODE PathAt(char * szBuffer, size_t cchBuffer, size_t nItemIndex);

    // Finds a file using its full path or DataId
    PCASC_FILE_NODE Find(const char * szFullPath, PDWORD PtrFileSize);

    // Returns the number of items in the tree
    size_t GetCount();

    // Returns the index of an item in the tree
    size_t IndexOf(PCASC_FILE_NODE pFileNode);

    // Retrieves the extra values from the node (if supported)
    void GetExtras(PCASC_FILE_NODE pFileNode, PDWORD PtrDataId, PDWORD PtrFileSize, PDWORD PtrLocaleId);

    protected:

    PCASC_FILE_NODE GetOrInsert(ULONGLONG FileNameHash, const char * szNodeBegin, const char * szNodeEnd, PCONTENT_KEY pCKey, DWORD Parent, DWORD DataId);
    PCASC_FILE_NODE GetOrInsert(const char * szNormPath, size_t nLength, const char * szNodeBegin, const char * szNodeEnd, PCONTENT_KEY pCKey, DWORD Parent, DWORD DataId);
    size_t MakePath(PCASC_FILE_NODE pFileNode, char * szBuffer, size_t cchBuffer);
    void SetExtras(PCASC_FILE_NODE pFileNode, DWORD FileSize, DWORD LocaleId);
    bool RebuildTreeMaps();

    CASC_ARRAY FileTable;                           // Dynamic array that holds all CASC_FILE_NODEs
    CASC_ARRAY NameTable;                           // Dynamic array that holds all node names

    PCASC_MAP pNameMap;                             // Map of FullFileName -> CASC_FILE_NODE
    PCASC_MAP pIdMap;                               // Map of FileId -> CASC_FILE_NODE

    size_t FileSizeOffset;                          // If nonzero, this is the offset of the "FileSize" field in the CASC_FILE_NODE
    size_t DataIdOffset;                            // If nonzero, this is the offset of the "DataId" field in the CASC_FILE_NODE
    size_t LocaleIdOffset;                          // If nonzero, this is the offset of the "LocaleId" field in the CASC_FILE_NODE
};

#endif // __FILETREE_H__
