/*****************************************************************************/
/* CascRootFile_TVFS.cpp                  Copyright (c) Ladislav Zezula 2018 */
/*---------------------------------------------------------------------------*/
/* ROOT handler for TACT VFS manifest format (root)                          */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 24.05.18  1.00  Lad  The first version of CascRootFile_TVFS.cpp           */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local defines

#define TVFS_FLAG_INCLUDE_CKEY       0x0001         // Include C-key in content file record
#define TVFS_FLAG_WRITE_SUPPORT      0x0002         // Write support. Include a table of encoding specifiers. This is required for writing files to the underlying storage. This bit is implied by the patch-support bit
#define TVFS_FLAG_PATCH_SUPPORT      0x0004         // Patch support. Include patch records in the content file records.
#define TVFS_FLAG_LOWERCASE_MANIFEST 0x0008         // Lowercase manifest. All paths in the path table have been converted to ASCII lowercase (i.e. [A-Z] converted to [a-z])

#define TVFS_FOLDER_NODE            0x80000000      // Highest bit is set if a file node is a folder
#define TVFS_FOLDER_SIZE_MASK       0x7FFFFFFF      // Mask to get length of the folder

//-----------------------------------------------------------------------------
// Local structures

// All fields in the in-file layout are big-endian
typedef struct _TVFS_FILE_HEADER
{
    DWORD dwSignature;                              // Must be CASC_TVFS_ROOT_SIGNATURE
    BYTE  FormatVersion;                            // Version of the format. Should be 1.
    BYTE  HeaderSize;                               // Size of the header, in bytes
    BYTE  EKeySize;                                 // Size of an E-Key. TACT uses 9-byte E-keys
    BYTE  PatchKeySize;                             // Size of a patch key. TACT uses 9-byte P-keys
    DWORD dwFlags;                                  // Flags. See TVFS_FLAG_XXX

    // Followed by the offset table (variable length)
    DWORD  dwPathTableOffset;                       // Offset of the path table
    DWORD  dwPathTableSize;                         // Size of the path table
    DWORD  dwVfsTableOffset;                        // Offset of the VFS table
    DWORD  dwVfsTableSize;                          // Size of the VFS table
    DWORD  dwCftTableOffset;                        // Offset of the container file table
    DWORD  dwCftTableSize;                          // Size of the container file table
    USHORT MaxDepth;                                // The maximum depth of the path prefix tree stored in the path table
    DWORD  dwEstTableOffset;                        // The offset of the encoding specifier table. Only if the write-support bit is set in the header flag
    DWORD  dwEstTableSize;                          // The size of the encoding specifier table. Only if the write-support bit is set in the header flag

} TVFS_FILE_HEADER, *PTVFS_FILE_HEADER;

// In-memory structure of a file entry in the linear file list
typedef struct _TVFS_FILE_ENTRY
{
    ULONGLONG FileNameHash;                         // Hash of the full file name
    DWORD dwParentFolder;
    DWORD dwNameOffset;                             // Offset of the name (in name's dynamic array). 0 = no name (i.e. root folder)
    DWORD dwSpanOffset;                             // Offset of the span array
    DWORD dwSpanCount;                              // Number of file spans

} TVFS_FILE_ENTRY, *PTVFS_FILE_ENTRY;

// A structure describing the file span
typedef struct _TVFS_SPAN_ENTRY
{
    ENCODING_KEY EKey;                              // Encoding/Index key for the span
    DWORD dwFileOffset;                             // Offset into the reference file
    DWORD dwSpanSize;                               // Size of the file span. Higher bit is set if the SpanKey is an index key (as opposite to encoding key)

} TVFS_SPAN_ENTRY, *PTVFS_SPAN_ENTRY;

struct TRootHandler_TVFS : public TRootHandler
{
    // File header, normalized to little endian
    TVFS_FILE_HEADER Header;

    // Global map of FileNameHash -> FileEntry. Used for fast search of a file entry
    PCASC_MAP pRootMap;

    // Linear global list of all files. Used for file enumeration
    DYNAMIC_ARRAY FileTable;

    // Linear global list of plain names. Used for constructing full path
    DYNAMIC_ARRAY NameList;

    // Linear list of all file spans
    DYNAMIC_ARRAY SpanList;

    // Buffer for merging the full file name during loading the root directory
    DYNAMIC_ARRAY PathBuffer;

    // Used during parsing the root file
    LPBYTE pbRootFile;
    LPBYTE pbRootFileEnd;
    DWORD dwCurrentFolder;
    DWORD dwPathFragment;
    DWORD dwCftOffsSize;                            // Size of the "Container File Table Offset" entry in VFS table
    DWORD dwEstOffsSize;                            // Size of the "Encoding Specifier Table Entry" entry in Container File Table
};

typedef int (*PARSE_FILE_CB)(TRootHandler_TVFS * pRootHandler, const char * szNamePtr, const char * szNameEnd, DWORD dwNodeValue);

//-----------------------------------------------------------------------------
// Local variables

//-----------------------------------------------------------------------------
// Local functions

// Returns size of "container file table offset" fiels in the VFS.
// - If the container file table is larger than 0xffffff bytes, it's 4 bytes
// - If the container file table is larger than 0xffff bytes, it's 3 bytes
// - If the container file table is larger than 0xff bytes, it's 2 bytes
// - If the container file table is smaller than 0xff bytes, it's 1 byte
static DWORD GetOffsetFieldSize(ULONG dwTableSize)
{
    if(dwTableSize > 0xffffff)
        return 4;
    if(dwTableSize > 0xffff)
        return 3;
    if(dwTableSize > 0xff)
        return 2;
    return 1;
}

static int CaptureFileHeader(PTVFS_FILE_HEADER pHeader, LPBYTE pbBuffer, DWORD cbBuffer)
{
    PTVFS_FILE_HEADER pSrcHeader;

    // Verify data size
    if(cbBuffer < FIELD_OFFSET(TVFS_FILE_HEADER, dwPathTableOffset))
        return ERROR_BAD_FORMAT;
    pSrcHeader = (PTVFS_FILE_HEADER)pbBuffer;

    // Verify inner header size
    if(cbBuffer < pSrcHeader->HeaderSize)
        return ERROR_BAD_FORMAT;

    // Copy as much as possible
    memset(pHeader, 0, sizeof(TVFS_FILE_HEADER));
    memcpy(pHeader, pSrcHeader, pSrcHeader->HeaderSize);

    // Swap the header values
    pHeader->dwFlags = ConvertBytesToInteger_4_LE((LPBYTE)(&pHeader->dwFlags));

    // Swap the offset table values
    pHeader->dwPathTableOffset      = ConvertBytesToInteger_4((LPBYTE)(&pHeader->dwPathTableOffset));
    pHeader->dwPathTableSize        = ConvertBytesToInteger_4((LPBYTE)(&pHeader->dwPathTableSize));
    pHeader->dwVfsTableOffset       = ConvertBytesToInteger_4((LPBYTE)(&pHeader->dwVfsTableOffset));
    pHeader->dwVfsTableSize         = ConvertBytesToInteger_4((LPBYTE)(&pHeader->dwVfsTableSize));
    pHeader->dwCftTableOffset       = ConvertBytesToInteger_4((LPBYTE)(&pHeader->dwCftTableOffset));
    pHeader->dwCftTableSize         = ConvertBytesToInteger_4((LPBYTE)(&pHeader->dwCftTableSize));
    pHeader->MaxDepth               = (USHORT)ConvertBytesToInteger_2((LPBYTE)(&pHeader->MaxDepth));
    pHeader->dwEstTableOffset       = ConvertBytesToInteger_4((LPBYTE)(&pHeader->dwEstTableOffset));
    pHeader->dwEstTableSize         = ConvertBytesToInteger_4((LPBYTE)(&pHeader->dwEstTableSize));
    return ERROR_SUCCESS;
}

static int CaptureCftSpanEntry(TRootHandler_TVFS * pRootHandler, PTVFS_SPAN_ENTRY pSpanEntry, DWORD dwCftOffset)
{
    PTVFS_FILE_HEADER pHeader = &pRootHandler->Header;
    LPBYTE pbCftFileTable;
    LPBYTE pbCftSpanEntry;
    LPBYTE pbCftTableEnd;

    // Get the start and end of the VFS table
    pbCftFileTable = pRootHandler->pbRootFile + pRootHandler->Header.dwCftTableOffset;
    pbCftTableEnd = pbCftFileTable + pRootHandler->Header.dwCftTableSize;

    // Get the pointer into the VFS table
    pbCftSpanEntry = pbCftFileTable + dwCftOffset;
    if(!(pbCftFileTable <= pbCftSpanEntry && pbCftSpanEntry < pbCftTableEnd))
        return ERROR_INVALID_PARAMETER;

    // Capture the E-Key
    if((pbCftSpanEntry + pHeader->EKeySize) > pbCftTableEnd)
        return ERROR_BAD_FORMAT;
    memcpy(pSpanEntry->EKey.Value, pbCftSpanEntry, pHeader->EKeySize);
//  pbCftSpanEntry += pHeader->EKeySize;

/*
    // Capture the encoding size (compressed size)
    if((pbCftSpanEntry + 4) > pbCftTableEnd)
        return ERROR_BAD_FORMAT;
    pSpanEntry->dwCompressedSize = ConvertBytesToInteger_4(pbCftSpanEntry);
    pbCftSpanEntry += 4;

    // Capture the offset to the E-spec table. This fiels is only there is write support is enabled
    if(pHeader->dwFlags & TVFS_FLAG_WRITE_SUPPORT)
    {
        if((pbCftSpanEntry + pRootHandler->dwCftOffsSize) > pbCftTableEnd)
            return ERROR_BAD_FORMAT;
        pSpanEntry->dwESpecEntryOffset = ConvertBytesToInteger_4(pbCftSpanEntry);
        pbCftSpanEntry += pRootHandler->dwCftOffsSize;
    }

    // Capture the content size (uncompressed size)
    if((pbCftSpanEntry + 4) > pbCftTableEnd)
        return ERROR_BAD_FORMAT;
    pSpanEntry->dwUncompressedSize = ConvertBytesToInteger_4(pbCftSpanEntry);
    pbCftSpanEntry += 4;

    // Capture the C-Key for the file. Always there.
    if(pHeader->dwFlags & TVFS_FLAG_INCLUDE_CKEY)
    {
        if((pbCftSpanEntry + 0x10) > pbCftTableEnd)
            return ERROR_BAD_FORMAT;
        memcpy(pSpanEntry->ContentKey, pbCftSpanEntry, 0x10);
        pbCftSpanEntry += 0x10;
    }

    // Followed by the patch support:
    // - (1 byte)  Number of patch records
    // - (E bytes) The E-key of the base file. Number of bytes matches the configured size of stored E-keys.
    // - (4 bytes) The C-size of the base file. Big endian.
    // - The patch key. Number of bytes as specified in the header.
    // - Size of the patch. 4 bytes, big endian.
    // - The age of the entry (1 byte).
*/

    return ERROR_SUCCESS;
}

static int CaptureVfsSpanEntry(TRootHandler_TVFS * pRootHandler, PTVFS_SPAN_ENTRY pSpanEntry, LPBYTE pbVfsFileEntry, LPBYTE pbVfsTableEnd)
{
    DWORD dwCftOffset;

    // Capture the file offset
    if((pbVfsFileEntry + 4) >= pbVfsTableEnd)
        return ERROR_BAD_FORMAT;
    pSpanEntry->dwFileOffset = ConvertBytesToInteger_4(pbVfsFileEntry);
    pbVfsFileEntry += 4;

    // Capture the span length
    if((pbVfsFileEntry + 4) >= pbVfsTableEnd)
        return ERROR_BAD_FORMAT;
    pSpanEntry->dwSpanSize = ConvertBytesToInteger_4(pbVfsFileEntry);
    pbVfsFileEntry += 4;

    // Capture the offset to the container file table
    if((pbVfsFileEntry + pRootHandler->dwCftOffsSize) > pbVfsTableEnd)
        return ERROR_BAD_FORMAT;
    dwCftOffset = ConvertBytesToInteger_X(pbVfsFileEntry, pRootHandler->dwCftOffsSize);

    // Capture the container file table entry
    return CaptureCftSpanEntry(pRootHandler, pSpanEntry, dwCftOffset);
}

static int CaptureVfsSpanEntries(TRootHandler_TVFS * pRootHandler, PTVFS_FILE_ENTRY pFileEntry, DWORD dwVfsOffset)
{
    PTVFS_SPAN_ENTRY pSpanEntry;
    LPBYTE pbVfsFileTable;
    LPBYTE pbVfsSpanEntry;
    LPBYTE pbVfsTableEnd;
    DWORD dwSpanCount;

    // Get the start and end of the VFS table
    pbVfsFileTable = pRootHandler->pbRootFile + pRootHandler->Header.dwVfsTableOffset;
    pbVfsTableEnd = pbVfsFileTable + pRootHandler->Header.dwVfsTableSize;

    // Get the pointer into the VFS table
    pbVfsSpanEntry = pbVfsFileTable + dwVfsOffset;
    if(!(pbVfsFileTable <= pbVfsSpanEntry && pbVfsSpanEntry < pbVfsTableEnd))
        return ERROR_INVALID_PARAMETER;

    // 1 - 224 = valid file, 225-254 = other file, 255 = deleted file
    // We will ignore all files with unsupported span count
    dwSpanCount = *pbVfsSpanEntry++;
    if(dwSpanCount == 0 || dwSpanCount > 224)
        return ERROR_BAD_FORMAT;

    // So far we've only saw entries with 1 span.
    // Need to test files with multiple spans
    assert(dwSpanCount == 1);

    // Parse all VFS items.
    for(DWORD i = 0; i < dwSpanCount; i++)
    {
        // Insert new span entry
        pSpanEntry = (PTVFS_SPAN_ENTRY)Array_Insert(&pRootHandler->SpanList, NULL, 1);
        if(pSpanEntry != NULL)
        {
            // Insert the offset of the first list in the span table
            if(i == 0)
            {
                pFileEntry->dwSpanOffset = Array_IndexOf(&pRootHandler->SpanList, pSpanEntry);
            }

            // Capture the span entry
            CaptureVfsSpanEntry(pRootHandler, pSpanEntry, pbVfsSpanEntry, pbVfsTableEnd);
        }
    }

    // Save the span count and exit
    pFileEntry->dwSpanCount = dwSpanCount;
    return ERROR_SUCCESS;
}

static void CreateFileName(TRootHandler_TVFS * pRootHandler, PTVFS_FILE_ENTRY pFileEntry)
{
    PTVFS_FILE_ENTRY pParentEntry;
    const char * szPathFragment = NULL;
    char chBackslash = '\\';

    // Do we have parent directory?
    if(pFileEntry->dwParentFolder != 0)
    {
        // Append the parent entry first
        pParentEntry = (PTVFS_FILE_ENTRY)Array_ItemAt(&pRootHandler->FileTable, pFileEntry->dwParentFolder);
        CreateFileName(pRootHandler, pParentEntry);

        // Append the backslash
        Array_Insert(&pRootHandler->PathBuffer, &chBackslash, 1);
    }

    // Append the current length
    szPathFragment = (const char *)Array_ItemAt(&pRootHandler->NameList, pFileEntry->dwNameOffset);
    Array_Insert(&pRootHandler->PathBuffer, szPathFragment, strlen(szPathFragment));
}

static DWORD ParsePathFileTable(
    TRootHandler_TVFS * pRootHandler,
    PARSE_FILE_CB PfnFolderCB,
    PARSE_FILE_CB PfnFileCB,
    LPBYTE pbPathFileTable,
    LPBYTE pbPathTableEnd)
{
    DWORD dwSaveCurrentFolder = pRootHandler->dwCurrentFolder;
    DWORD dwSavePathFragment = pRootHandler->dwPathFragment;
    DWORD dwNodeValue;
    int nError;

    // Parse the file table
    while(pbPathFileTable < pbPathTableEnd)
    {
        // Set the default name begin and end
        char * szNamePtr = (char *)pbPathFileTable;
        char * szNameEnd = (char *)pbPathFileTable;

        // The path fragment is encoded as length+name (length = 1 byte). 0xFF means end of the list
        if(pbPathFileTable < pbPathTableEnd && pbPathFileTable[0] != 0xFF)
        {
            szNamePtr = (char *)(pbPathFileTable + 1);
            pbPathFileTable = pbPathFileTable + pbPathFileTable[0] + 1;
            szNameEnd = (char *)(pbPathFileTable);
        }

        // There must be 0xFF at the end of the name. Or at the very beginning (means end of the list)
        if(pbPathFileTable >= pbPathTableEnd || pbPathFileTable[0] != 0xFF)
            return ERROR_FILE_CORRUPT;
        pbPathFileTable++;

        // Load the node value
        if((pbPathFileTable + 4) > pbPathTableEnd)
            return ERROR_FILE_CORRUPT;
        dwNodeValue = ConvertBytesToInteger_4(pbPathFileTable);

        // Is it an inner node (with sub-nodes)?
        if(dwNodeValue & TVFS_FOLDER_NODE)
        {
            DWORD dwFolderDataSize = (dwNodeValue & TVFS_FOLDER_SIZE_MASK);

            // Call the directory callback, if any
            if(PfnFolderCB != NULL)
            {
                nError = PfnFolderCB(pRootHandler, szNamePtr, szNameEnd, dwNodeValue);
                if(nError != ERROR_SUCCESS)
                    return nError;
            }

            // Recursively call the 
            nError = ParsePathFileTable(pRootHandler, PfnFolderCB, PfnFileCB, pbPathFileTable + 4, pbPathFileTable + dwFolderDataSize);
            if(nError != ERROR_SUCCESS)
                return nError;

            // Restore the current folder and skip the directory
            pRootHandler->dwCurrentFolder = dwSaveCurrentFolder;
            pRootHandler->dwPathFragment = dwSavePathFragment;
            pbPathFileTable += dwFolderDataSize;
        }
        else
        {
            // Do we have a file callback?
            if(PfnFileCB != NULL)
            {
                nError = PfnFileCB(pRootHandler, szNamePtr, szNameEnd, dwNodeValue);
                if(nError != ERROR_SUCCESS)
                    return nError;
            }

            // Move to the next path table entry
            pbPathFileTable += 4;
        }
    }

    // Return the total number of entries
    return ERROR_SUCCESS;
}

static int ParseCB_LoadPathItem(TRootHandler_TVFS * pRootHandler, const char * szNamePtr, const char * szNameEnd, DWORD dwNodeValue)
{
    PTVFS_FILE_ENTRY pFileEntry;
    const char * szFileName = NULL;
    DWORD dwCurrentFolder = pRootHandler->dwCurrentFolder;
    char chBackslash = '\\';
    char chZeroChar = 0;

    // Ignore anything that has no name
    if(szNameEnd > szNamePtr)
    {
        // Insert new item to the linear file table
        pFileEntry = (PTVFS_FILE_ENTRY)Array_Insert(&pRootHandler->FileTable, NULL, 1);
        if(pFileEntry == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(pFileEntry, 0, sizeof(TVFS_FILE_ENTRY));

        // Insert the path fragment to the end od the current path
        pRootHandler->PathBuffer.ItemCount = pRootHandler->dwPathFragment;
        szFileName = (const char *)Array_Insert(&pRootHandler->PathBuffer, szNamePtr, (szNameEnd - szNamePtr));

        // File: Insert the name to the path merger and insert the entry to the map
        if(!(dwNodeValue & TVFS_FOLDER_NODE))
        {
            // Terminate the name with zero and calculate hash
            Array_Insert(&pRootHandler->PathBuffer, &chZeroChar, 1);
            pFileEntry->FileNameHash = CalcFileNameHash(szFileName);
        }
        else
        {
            // Folder: Append '\\' after the name
            Array_Insert(&pRootHandler->PathBuffer, &chBackslash, 1);

            // Update the parent folder index
            pRootHandler->dwCurrentFolder = Array_IndexOf(&pRootHandler->FileTable, pFileEntry);
        }

        // Set the parent folder
        pFileEntry->dwParentFolder = dwCurrentFolder;

        // Insert the plain name to the name list and remember the name offset
        szFileName = (const char *)Array_Insert(&pRootHandler->NameList, szNamePtr, (szNameEnd - szNamePtr));
        Array_Insert(&pRootHandler->NameList, &chZeroChar, 1);
        pFileEntry->dwNameOffset = Array_IndexOf(&pRootHandler->NameList, szFileName);

        // TODO: If this item is a file, we need to get the list of spans
        if(!(dwNodeValue & TVFS_FOLDER_NODE))
        {
            // If this fails, don't stop the loading process, but ignore this entry
            CaptureVfsSpanEntries(pRootHandler, pFileEntry, dwNodeValue);
        }
    }
    return ERROR_SUCCESS;
}

static int CapturePathTables(TRootHandler_TVFS * pRootHandler, LPBYTE pbRootFile, LPBYTE pbRootEnd)
{
    PTVFS_FILE_HEADER pHeader = &pRootHandler->Header;    
    PTVFS_FILE_ENTRY pEntry;
    LPBYTE pbPathFileTable;
    LPBYTE pbPathTableEnd;
    LPBYTE pbVfsFileTable;
    LPBYTE pbVfsTableEnd;
    LPBYTE pbCftFileTable;
    LPBYTE pbCftTableEnd;
    char OneByteZero = 0;
    int nError;

    // Capture the path table
    pbPathFileTable = pbRootFile + pHeader->dwPathTableOffset;
    pbPathTableEnd = pbPathFileTable + pHeader->dwPathTableSize;
    if(pbPathTableEnd > pbRootEnd)
        return ERROR_BAD_FORMAT;

    // Capture the VFS file table
    pbVfsFileTable = pbRootFile + pHeader->dwVfsTableOffset;
    pbVfsTableEnd = pbVfsFileTable + pHeader->dwVfsTableSize;
    if(pbVfsTableEnd > pbRootEnd)
        return ERROR_BAD_FORMAT;

    // Capture the container file table
    pbCftFileTable = pbRootFile + pHeader->dwCftTableOffset;
    pbCftTableEnd = pbCftFileTable + pHeader->dwCftTableSize;
    if(pbCftTableEnd > pbRootEnd)
        return ERROR_BAD_FORMAT;

    // Allocate global buffer for file names
    nError = Array_Create(&pRootHandler->NameList, char, 0x01000000);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Allocate buffer for creating VFS entry
    nError = Array_Create(&pRootHandler->SpanList, TVFS_SPAN_ENTRY, 0x10000);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Allocate the global linear file table
    nError = Array_Create(&pRootHandler->FileTable, TVFS_FILE_ENTRY, 0x100);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Allocate buffer for merging file name
    nError = Array_Create(&pRootHandler->PathBuffer, char, 0x10000);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Insert dummy item at the begin of the file list
    Array_Insert(&pRootHandler->NameList, &OneByteZero, 1);
    pEntry = (PTVFS_FILE_ENTRY)Array_Insert(&pRootHandler->FileTable, NULL, 1);
    if(pEntry != NULL)
        memset(pEntry, 0, sizeof(TVFS_FILE_ENTRY));

    // Prepare the pointer to VFS file table for parsing
    pRootHandler->pbRootFile = pbRootFile;
    pRootHandler->pbRootFileEnd = pbRootEnd;
    nError = ParsePathFileTable(pRootHandler, ParseCB_LoadPathItem, ParseCB_LoadPathItem, pbPathFileTable, pbPathTableEnd);

    // Clear variables
    pRootHandler->pbRootFile = NULL;
    pRootHandler->pbRootFileEnd = NULL;
    pRootHandler->dwPathFragment = 0;
    return nError;
}

static int RebuildFileMap(TRootHandler_TVFS * pRootHandler)
{
    PTVFS_FILE_ENTRY pFileEntry;
    PCASC_MAP pRootMap = pRootHandler->pRootMap;
    DWORD dwTotalFileCount = pRootHandler->FileTable.ItemCount;
    int nError = ERROR_SUCCESS;

    // Free the existing file map
    if(pRootMap != NULL)
        Map_Free(pRootMap);
    pRootMap = NULL;

    // Retrieve the number of files in the map
    dwTotalFileCount = pRootHandler->FileTable.ItemCount;
    if(dwTotalFileCount != NULL)
    {
        // Create map of FileName -> TVFS_FILE_ENTRY
        pRootMap = Map_Create(dwTotalFileCount, sizeof(ULONGLONG), FIELD_OFFSET(TVFS_FILE_ENTRY, FileNameHash));
        if(pRootMap != NULL)
        {
            // Parse the entire file table and put items to the map
            for(DWORD i = 0; i < dwTotalFileCount; i++)
            {
                pFileEntry = (PTVFS_FILE_ENTRY)Array_ItemAt(&pRootHandler->FileTable, i);
                if(pFileEntry != NULL && pFileEntry->FileNameHash != 0 && pFileEntry->dwSpanCount != 0)
                {
                    Map_InsertObject(pRootMap, pFileEntry, &pFileEntry->FileNameHash);
                }
            }
        }
        else
        {
            nError = ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    // Store the new map in the root handler
    pRootHandler->pRootMap = pRootMap;
    return nError;
}

//-----------------------------------------------------------------------------
// Implementation of TVFS root file

static int TVFS_Insert(TRootHandler_TVFS * pRootHandler, const char * szFileName, LPBYTE pbEncodingKey)
{
    PTVFS_FILE_ENTRY pFileEntry;
    PTVFS_SPAN_ENTRY pSpanEntry;
    const char * szPlainName;
    char * ItemArray = pRootHandler->FileTable.ItemArray;
    size_t nLength;

    // Insert the entry to the file table
    pFileEntry = (PTVFS_FILE_ENTRY)Array_Insert(&pRootHandler->FileTable, NULL, 1);
    if(pFileEntry != NULL)
    {
        // Reset the entry
        memset(pFileEntry, 0, sizeof(TVFS_FILE_ENTRY));

        // Set the file name hash
        pFileEntry->FileNameHash = CalcFileNameHash(szFileName);

        // Insert the name to the name list
        nLength = strlen(szFileName);
        szPlainName = (const char *)Array_Insert(&pRootHandler->NameList, szFileName, nLength+1);
        if(szPlainName != NULL)
            pFileEntry->dwNameOffset = Array_IndexOf(&pRootHandler->NameList, szPlainName);

        // Insert the file span
        pSpanEntry = (PTVFS_SPAN_ENTRY)Array_Insert(&pRootHandler->SpanList, NULL, 1);
        if(pSpanEntry != NULL)
        {
            // Set the Index key of the span entry
            memcpy(pSpanEntry->EKey.Value, pbEncodingKey, MD5_HASH_SIZE);

            // Set the span offset and count
            pFileEntry->dwSpanOffset = Array_IndexOf(&pRootHandler->SpanList, pSpanEntry);
            pFileEntry->dwSpanCount = 1;

            // Insert the item to the map
            Map_InsertObject(pRootHandler->pRootMap, pFileEntry, &pFileEntry->FileNameHash);
        }

        // Do we need to rebuild the complete map?
        if(pRootHandler->FileTable.ItemArray != ItemArray)
            RebuildFileMap(pRootHandler);
        return ERROR_SUCCESS;
    }

    return ERROR_NOT_ENOUGH_MEMORY;
}

static LPBYTE TVFS_Search(TRootHandler_TVFS * pRootHandler, TCascSearch * pSearch)
{
    PTVFS_FILE_ENTRY pFileEntry;
    PTVFS_SPAN_ENTRY pSpanEntry;
    size_t nToCopy;

    // Keep searching until there is something
    while(pSearch->IndexLevel1 < pRootHandler->FileTable.ItemCount)
    {
        // Retrieve the file entry
        pFileEntry = (PTVFS_FILE_ENTRY)Array_ItemAt(&pRootHandler->FileTable, pSearch->IndexLevel1);
        if(pFileEntry == NULL)
            return NULL;

        // Move to the next one
        pSearch->IndexLevel1++;

        // Ignore empty entries
        if(pFileEntry->dwSpanCount != 0)
        {
            // Create the full path name
            pRootHandler->PathBuffer.ItemCount = 0;
            CreateFileName(pRootHandler, pFileEntry);

            // Copy the name to the search array
            nToCopy = min(MAX_PATH, pRootHandler->PathBuffer.ItemCount);
            strncpy(pSearch->szFileName, pRootHandler->PathBuffer.ItemArray, nToCopy);
            pSearch->szFileName[nToCopy] = 0;

            // Retrieve the first span entry
            pSpanEntry = (PTVFS_SPAN_ENTRY)Array_ItemAt(&pRootHandler->SpanList, pFileEntry->dwSpanOffset);
            if(pSpanEntry != NULL)
            {
                // Return the EKey key
                return pSpanEntry->EKey.Value;
            }
        }
    }

    return NULL;
}

static void TVFS_EndSearch(TRootHandler_TVFS * /* pRootHandler */, TCascSearch * /* pSearch */)
{
    // Do nothing here
}

static LPBYTE TVFS_GetKey(TRootHandler_TVFS * pRootHandler, const char * szFileName)
{
    PTVFS_FILE_ENTRY pFileEntry;
    PTVFS_SPAN_ENTRY pSpanEntry;
    ULONGLONG FileNameHash = CalcFileNameHash(szFileName);

    // Find the file in the name table
    pFileEntry = (PTVFS_FILE_ENTRY)Map_FindObject(pRootHandler->pRootMap, &FileNameHash, NULL);
    if(pFileEntry != NULL)
    {
        // Retrieve the span entry
        pSpanEntry = (PTVFS_SPAN_ENTRY)Array_ItemAt(&pRootHandler->SpanList, pFileEntry->dwSpanOffset);
        if(pSpanEntry != NULL)
        {
            // Give the encoding/index key
            return pSpanEntry->EKey.Value;            
        }
    }

    // File not there, sorry
    return NULL;
}

static DWORD TVFS_GetFileId(TRootHandler_TVFS * /* pRootHandler */, const char * /* szFileName */)
{
    // Not implemented for TVFS
    return 0;
}

static void TVFS_Close(TRootHandler_TVFS * pRootHandler)
{
    if(pRootHandler != NULL)
    {
        // Free the file map
        Map_Free(pRootHandler->pRootMap);

        // Free the array of the file entries and file names
        Array_Free(&pRootHandler->PathBuffer);
        Array_Free(&pRootHandler->FileTable);
        Array_Free(&pRootHandler->NameList);
        Array_Free(&pRootHandler->SpanList);

        // Free the root file itself
        CASC_FREE(pRootHandler);
    }
}

//-----------------------------------------------------------------------------
// Public functions - TVFS root

int RootHandler_CreateTVFS(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_TVFS * pRootHandler;
    LPBYTE pbRootEnd = pbRootFile + cbRootFile;
    int nError;

    // Allocate the root handler object
    hs->pRootHandler = pRootHandler = CASC_ALLOC(TRootHandler_TVFS, 1);
    if(pRootHandler == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Fill-in the handler functions
    memset(pRootHandler, 0, sizeof(TRootHandler_TVFS));
    pRootHandler->Insert      = (ROOT_INSERT)TVFS_Insert;
    pRootHandler->Search      = (ROOT_SEARCH)TVFS_Search;
    pRootHandler->EndSearch   = (ROOT_ENDSEARCH)TVFS_EndSearch;
    pRootHandler->GetKey      = (ROOT_GETKEY)TVFS_GetKey;
    pRootHandler->Close       = (ROOT_CLOSE)TVFS_Close;
    pRootHandler->GetFileId   = (ROOT_GETFILEID)TVFS_GetFileId;

    // We have file names. We return index keys, not encoding keys
    pRootHandler->dwRootFlags |= (ROOT_FLAG_HAS_NAMES | ROOT_FLAG_USES_INDEX_KEY | ROOT_FLAG_DONT_SEARCH_ENCKEY);

    // Check and capture the TVFS header
    nError = CaptureFileHeader(&pRootHandler->Header, pbRootFile, cbRootFile);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Set size of "container file table offset".
    pRootHandler->dwCftOffsSize = GetOffsetFieldSize(pRootHandler->Header.dwCftTableSize);
    pRootHandler->dwEstOffsSize = GetOffsetFieldSize(pRootHandler->Header.dwEstTableSize);

    // Load the path table into the root handler
    nError = CapturePathTables(pRootHandler, pbRootFile, pbRootEnd);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Build the file map of FileName -> TVFS_FILE_ENTRY
    return RebuildFileMap(pRootHandler);
}
