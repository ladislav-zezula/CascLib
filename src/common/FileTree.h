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

#define CFN_FLAG_FOLDER             0x0001          // This item is a folder

typedef struct _CONTENT_KEY
{
    BYTE Value[MD5_HASH_SIZE];                      // MD5 of the file

} CONTENT_KEY, *PCONTENT_KEY;

typedef struct _CASC_FILE_NODE
{
    // Item management
    CONTENT_KEY CKey;                               // File CKey/EKey. If the key length is smaller, it is padded by zeros.
    ULONGLONG NameHash;                             // Jenkins hash of the normalized file name (uppercase, backslashes)
    DWORD DataId;                                   // Item data ID
    DWORD Parent;                                   // The index of a parent directory. If CASC_INVALID_INDEX, then this is the root item
    DWORD NameIndex;                                // Index of the node name. If CASC_INVALID_INDEX, then this node has no name
    USHORT NameLength;                              // Length of the name, (without the zero terminator)
    USHORT Flags;                                   // See CFE_FLAG_XXX

} CASC_FILE_NODE, *PCASC_FILE_NODE;

typedef struct _CASC_FILE_TREE
{
    CASC_ARRAY FileTable;                           // Dynamic array that holds all CASC_FILE_NODEs
    CASC_ARRAY NameTable;                           // Dynamic array that holds all node names

    PCASC_MAP pNameMap;                             // Map of FullFileName -> CASC_FILE_NODE
    PCASC_MAP pIdMap;                               // Map of FileId -> CASC_FILE_NODE

    DWORD NextDataId;

} CASC_FILE_TREE, *PCASC_FILE_TREE;

//-----------------------------------------------------------------------------
// Functions for managing a file tree

int    FileTree_Create(PCASC_FILE_TREE pFileTree, size_t FileNodeSize = sizeof(CASC_FILE_NODE));
void * FileTree_Insert(PCASC_FILE_TREE pFileTree, PCONTENT_KEY pCKey, const char * szFullPath = NULL, DWORD DataId = CASC_INVALID_ID);
void * FileTree_ItemAt(PCASC_FILE_TREE pFileTree, size_t nItemIndex);
void * FileTree_PathAt(PCASC_FILE_TREE pFileTree, char * szBuffer, size_t cchBuffer, size_t nItemIndex);
void * FileTree_Find(PCASC_FILE_TREE pFileTree, const char * szFullPath);
size_t FileTree_GetCount(PCASC_FILE_TREE pFileTree);
void   FileTree_Free(PCASC_FILE_TREE pFileTree);

#endif // __FILETREE_H__
