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

// In-memory layout of the TVFS file header
typedef struct _TVFS_DIRECTORY_HEADER
{
    DWORD Signature;                                // Must be CASC_TVFS_ROOT_SIGNATURE
    BYTE  FormatVersion;                            // Version of the format. Should be 1.
    BYTE  HeaderSize;                               // Size of the header, in bytes
    BYTE  EKeySize;                                 // Size of an E-Key. TACT uses 9-byte E-keys
    BYTE  PatchKeySize;                             // Size of a patch key. TACT uses 9-byte P-keys
    DWORD Flags;                                    // Flags. See TVFS_FLAG_XXX

    // Followed by the offset table (variable length)
    DWORD  PathTableOffset;                         // Offset of the path table
    DWORD  PathTableSize;                           // Size of the path table
    DWORD  VfsTableOffset;                          // Offset of the VFS table
    DWORD  VfsTableSize;                            // Size of the VFS table
    DWORD  CftTableOffset;                          // Offset of the container file table
    DWORD  CftTableSize;                            // Size of the container file table
    USHORT MaxDepth;                                // The maximum depth of the path prefix tree stored in the path table
    DWORD  EstTableOffset;                          // The offset of the encoding specifier table. Only if the write-support bit is set in the header flag
    DWORD  EstTableSize;                            // The size of the encoding specifier table. Only if the write-support bit is set in the header flag

    DWORD  CftOffsSize;                             // Byte length of the offset in the Content File Table entry
    DWORD  EstOffsSize;                             // Byte length of the offset in the Encoding Specifier Table entry

    LPBYTE pbDirectoryData;                         // Pointer to the begin of directory data
    LPBYTE pbDirectoryEnd;                          // Pointer to the end of directory data

//  LPBYTE pbPathFileTable;                         // Begin and end of the path table
//  LPBYTE pbPathTableEnd;

//  LPBYTE pbVfsFileTable;                          // Begin and end of the VFS file table
//  LPBYTE pbVfsTableEnd;

//  LPBYTE pbCftFileTable;                          // Begin and end of the content file table
//  LPBYTE pbCftTableEnd;

} TVFS_DIRECTORY_HEADER, *PTVFS_DIRECTORY_HEADER;

// Minimum size of a valid path table entry. 1 byte + 1-byte name + 1 byte + DWORD
#define TVFS_HEADER_LENGTH FIELD_OFFSET(TVFS_DIRECTORY_HEADER, CftOffsSize)

// Minimum size of a valid path table entry. 1 byte + 1-byte name + 1 byte + DWORD
#define TVFS_MIN_PATH_ENTRY (1 + 1 + 1 + sizeof(DWORD)) 

// Minimum size of the VFS entry (SpanCount + FileOffset + SpanLength + CftOffset)
#define TVFS_MIN_VFS_ENTRY (1 + sizeof(DWORD) + sizeof(DWORD) + 1)

// Minimum size of the Content File Table entry (CASC_EKEY_SIZE + EncodedSize + ContentSize)
#define TVFS_MIN_CFT_ENTRY (CASC_EKEY_SIZE + sizeof(DWORD) + sizeof(DWORD))

// Minimum size of the TVFS folder data
#define TVFS_MIN_FILE_SIZE (TVFS_HEADER_LENGTH + TVFS_MIN_PATH_ENTRY + TVFS_MIN_VFS_ENTRY + TVFS_MIN_CFT_ENTRY)

// Maximum estimated file table. Empirically set to 8 MB, increase if needed.
#define TVFS_MAX_FILE_SIZE 0x00800000

// In-memory layout of the path table entry
typedef struct _TVFS_PATH_TABLE_ENTRY
{
    LPBYTE pbNamePtr;                               // Pointer to the begin of the node name
    LPBYTE pbNameEnd;                               // Pointer to the end of the file name
    DWORD NodeValue;                                // Node value
    DWORD EndOfNode;                                // If TRUE, then this node is an end node (aka end of path fragment)
} TVFS_PATH_TABLE_ENTRY, *PTVFS_PATH_TABLE_ENTRY;

// Structure for the root handler
struct TRootHandler_TVFS : public TRootHandler
{
    // File tree of the entire storage
    CASC_FILE_TREE FileTree;

    // Back pointer to the storage
    TCascStorage * hs;
};

// Recursive function to parse directory data
static int ParseDirectoryData(TRootHandler_TVFS * pRootHandler, PATH_BUFFER & PathBuffer, TVFS_DIRECTORY_HEADER & DirHeader);

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

static bool PathBuffer_AddBackslash(PATH_BUFFER & PathBuffer)
{
    if(PathBuffer.szPtr >= PathBuffer.szEnd)
        return false;

    *PathBuffer.szPtr++ = '\\';
    *PathBuffer.szPtr = 0;
    return true;
}

static bool PathBuffer_AppendNode(PATH_BUFFER & PathBuffer, TVFS_PATH_TABLE_ENTRY & PathEntry)
{
    // Do we have some name?
    if(PathEntry.pbNameEnd > PathEntry.pbNamePtr)
    {
        size_t nLength = (PathEntry.pbNameEnd - PathEntry.pbNamePtr);

        // Check whether we have enough space
        if((PathBuffer.szPtr + nLength) > PathBuffer.szEnd)
            return false;

        // Copy the node name
        memcpy(PathBuffer.szPtr, PathEntry.pbNamePtr, nLength);
        PathBuffer.szPtr += nLength;

        // Append backslash, if needed
//      if(PathEntry.NodeValue & TVFS_FOLDER_NODE)
        if(PathEntry.EndOfNode)
        {
            if(PathBuffer.szPtr >= PathBuffer.szEnd)
                return false;
            *PathBuffer.szPtr++ = '\\';
        }
    }

    // Always end the buffer with zero
    PathBuffer.szPtr[0] = 0;
    return true;
}

static int CaptureDirectoryHeader(TVFS_DIRECTORY_HEADER & DirHeader, LPBYTE pbDataPtr, LPBYTE pbDataEnd)
{
    // Fill the header structure with zeros
    memset(&DirHeader, 0, sizeof(TVFS_DIRECTORY_HEADER));
    DirHeader.pbDirectoryData = pbDataPtr;
    DirHeader.pbDirectoryEnd = pbDataEnd;

    // Capture the signature
    pbDataPtr = CaptureInteger32(pbDataPtr, pbDataEnd, &DirHeader.Signature);
    if(pbDataPtr == NULL || DirHeader.Signature != CASC_TVFS_ROOT_SIGNATURE)
        return ERROR_BAD_FORMAT;

    // Capture the other four integers 
    pbDataPtr = CaptureByteArray(pbDataPtr, pbDataEnd, 4, &DirHeader.FormatVersion);
    if(pbDataPtr == NULL || DirHeader.FormatVersion != 1 || DirHeader.EKeySize != 9 || DirHeader.PatchKeySize != 9 || DirHeader.HeaderSize < 8)
        return ERROR_BAD_FORMAT;

    // Capture the rest
    pbDataPtr = CaptureByteArray(pbDataPtr, pbDataEnd, DirHeader.HeaderSize - FIELD_OFFSET(TVFS_DIRECTORY_HEADER, Flags), (LPBYTE)(&DirHeader.Flags));
    if(pbDataPtr == NULL)
        return ERROR_BAD_FORMAT;

    // Swap the header values
    DirHeader.Flags = ConvertBytesToInteger_4_LE((LPBYTE)(&DirHeader.Flags));

    // Swap the offset table values
    DirHeader.PathTableOffset = ConvertBytesToInteger_4((LPBYTE)(&DirHeader.PathTableOffset));
    DirHeader.PathTableSize   = ConvertBytesToInteger_4((LPBYTE)(&DirHeader.PathTableSize));
    DirHeader.VfsTableOffset  = ConvertBytesToInteger_4((LPBYTE)(&DirHeader.VfsTableOffset));
    DirHeader.VfsTableSize    = ConvertBytesToInteger_4((LPBYTE)(&DirHeader.VfsTableSize));
    DirHeader.CftTableOffset  = ConvertBytesToInteger_4((LPBYTE)(&DirHeader.CftTableOffset));
    DirHeader.CftTableSize    = ConvertBytesToInteger_4((LPBYTE)(&DirHeader.CftTableSize));
    DirHeader.MaxDepth        = (USHORT)ConvertBytesToInteger_2((LPBYTE)(&DirHeader.MaxDepth));
    DirHeader.EstTableOffset  = ConvertBytesToInteger_4((LPBYTE)(&DirHeader.EstTableOffset));
    DirHeader.EstTableSize    = ConvertBytesToInteger_4((LPBYTE)(&DirHeader.EstTableSize));

    // Determine size of file table offsets
    DirHeader.CftOffsSize = GetOffsetFieldSize(DirHeader.CftTableSize);
    DirHeader.EstOffsSize = GetOffsetFieldSize(DirHeader.EstTableSize);

    // Capture the path table
//  DirHeader.pbPathFileTable = pbDirectory + DirHeader.PathTableOffset;
//  DirHeader.pbPathTableEnd = pbDirectory + DirHeader.PathTableOffset + DirHeader.PathTableSize;
//  if(DirHeader.pbPathTableEnd > pbDataEnd)
//      return ERROR_BAD_FORMAT;

    // Capture the VFS file table
//  DirHeader.pbVfsFileTable = pbDirectory + DirHeader.VfsTableOffset;
//  DirHeader.pbVfsTableEnd = pbDirectory + DirHeader.VfsTableOffset + DirHeader.VfsTableSize;
//  if(DirHeader.pbVfsTableEnd > pbDataEnd)
//      return ERROR_BAD_FORMAT;

    // Capture the container file table
//  DirHeader.pbCftFileTable = pbDirectory + DirHeader.CftTableOffset;
//  DirHeader.pbCftTableEnd = pbDirectory + DirHeader.CftTableOffset + DirHeader.CftTableSize;
//  if(DirHeader.pbCftTableEnd > pbDataEnd)
//      return ERROR_BAD_FORMAT;

    return ERROR_SUCCESS;
}

static int CaptureVfsSpanEntries(TVFS_DIRECTORY_HEADER & DirHeader, PENCODED_KEY pEKey, PDWORD PtrSpanSize, DWORD dwVfsOffset)
{
    LPBYTE pbVfsFileTable = DirHeader.pbDirectoryData + DirHeader.VfsTableOffset;
    LPBYTE pbVfsFileEntry = pbVfsFileTable + dwVfsOffset;
    LPBYTE pbVfsFileEnd = pbVfsFileTable + DirHeader.VfsTableSize;
    LPBYTE pbCftFileTable;
    LPBYTE pbCftFileEntry;
    LPBYTE pbCftFileEnd;
    LPBYTE pbTemp = NULL;
    size_t ItemSize = sizeof(DWORD) + sizeof(DWORD) + DirHeader.CftOffsSize;
    DWORD dwCftOffset;
    DWORD dwSpanCount;
    DWORD dwSpanSize;

    // Get the number of span entries
    if(!(pbVfsFileTable <= pbVfsFileEntry && pbVfsFileEntry <= pbVfsFileEnd))
        return ERROR_INVALID_PARAMETER;
    dwSpanCount = *pbVfsFileEntry++;

    // 1 - 224 = valid file, 225-254 = other file, 255 = deleted file
    // We will ignore all files with unsupported span count
    if(dwSpanCount == 0 || dwSpanCount > 224)
        return ERROR_BAD_FORMAT;

    // So far we've only saw entries with 1 span.
    // Need to test files with multiple spans. Ignore such files for now.
    assert(dwSpanCount == 1);

    // Capture the array of span items
    if(CaptureArray_(pbVfsFileEntry, pbVfsFileEnd, &pbTemp, ItemSize, dwSpanCount) == NULL)
        return ERROR_BAD_FORMAT;

    //
    // Structure of the span entry:
    // (4bytes): Offset into the referenced file (big endian)
    // (4bytes): Size of the span (big endian)
    // (?bytes): Offset into Container File Table. Length depends on container file table size
    //

    // Get the offset to the Container File Table
    dwCftOffset = ConvertBytesToInteger_X(pbVfsFileEntry + sizeof(DWORD) + sizeof(DWORD), DirHeader.CftOffsSize);
    dwSpanSize = ConvertBytesToInteger_4(pbVfsFileEntry + sizeof(DWORD));

    // Go to the Container File Table and fetch the EKey from there
    pbCftFileTable = DirHeader.pbDirectoryData + DirHeader.CftTableOffset;
    pbCftFileEntry = pbCftFileTable + dwCftOffset;
    pbCftFileEnd = pbCftFileTable + DirHeader.CftTableSize;
    if((pbCftFileEntry + DirHeader.EKeySize) > pbCftFileEnd)
        return ERROR_BAD_FORMAT;

    // Give the pointer to the EKey
    memcpy(pEKey->Value, pbCftFileEntry, DirHeader.EKeySize);
    PtrSpanSize[0] = dwSpanSize;
    return ERROR_SUCCESS;
}

static LPBYTE CapturePathEntry(TVFS_PATH_TABLE_ENTRY & PathEntry, LPBYTE pbPathTablePtr, LPBYTE pbPathTableEnd)
{
    // We expect there to be a nonzero, and non-FF length
    if(pbPathTablePtr[0] == 0 || pbPathTablePtr[0] == 0xFF)
        return NULL;
    if((pbPathTablePtr + 1 + pbPathTablePtr[0] + 1 + sizeof(DWORD)) > pbPathTableEnd)
        return NULL;

    // Fill the path entry
    PathEntry.pbNamePtr = pbPathTablePtr + 1;
    PathEntry.pbNameEnd = pbPathTablePtr + 1 + pbPathTablePtr[0];
    pbPathTablePtr = PathEntry.pbNameEnd;

    // The '\0' character means that this is the end of the node
    PathEntry.EndOfNode = (pbPathTablePtr[0] == 0x00) ? 1 : 0;
    pbPathTablePtr += PathEntry.EndOfNode;
        
    // Check the proper terminator
    if(pbPathTablePtr[0] != 0xFF)
        return NULL;

    // Fill the node value
    PathEntry.NodeValue = ConvertBytesToInteger_4(pbPathTablePtr + 1);
    return pbPathTablePtr + 1 + sizeof(DWORD);
}

// This function verifies whether a file is actually a sub-directory.
// If yes, it contains just another "TVFS" virtual file system, just like the ROOT file.
static int IsSubDirectory(TRootHandler_TVFS * pRootHandler, TVFS_DIRECTORY_HEADER & DirHeader, TVFS_DIRECTORY_HEADER & SubHeader, ENCODED_KEY & EKey, DWORD dwFileSize)
{
    QUERY_KEY QueryKey;
    LPBYTE pbFileData = NULL;
    HANDLE hFile = NULL;
    DWORD cbBytesRead = 0;
    int nError = ERROR_BAD_FORMAT;

    // Verify the file size. We can do it before opening the file, thus speeding thins up a bit
    if(TVFS_MIN_FILE_SIZE < dwFileSize && dwFileSize < TVFS_MAX_FILE_SIZE)
    {
        // Open the file by encoded key
        QueryKey.pbData = EKey.Value;
        QueryKey.cbData = DirHeader.EKeySize;
        if(CascOpenFileByEKey((HANDLE)pRootHandler->hs, &QueryKey, dwFileSize, &hFile))
        {
            // Allocate space for eventual entire file.
            pbFileData = CASC_ALLOC(BYTE, dwFileSize);
            if(pbFileData != NULL)
            {
                // Read enough bytes to parse the header
                CascReadFile(hFile, pbFileData, TVFS_MIN_FILE_SIZE, &cbBytesRead);
                if(cbBytesRead == TVFS_MIN_FILE_SIZE)
                {
                    // Capture the file folder. This also serves as test
                    nError = CaptureDirectoryHeader(SubHeader, pbFileData, pbFileData + dwFileSize);
                    if(nError == ERROR_SUCCESS && dwFileSize > TVFS_MIN_FILE_SIZE)
                    {
                        // Load the rest of the file
                        CascReadFile(hFile, pbFileData + TVFS_MIN_FILE_SIZE, dwFileSize - TVFS_MIN_FILE_SIZE, &cbBytesRead);
                        if(cbBytesRead != (dwFileSize - TVFS_MIN_FILE_SIZE))
                        {
                            nError = ERROR_BAD_FORMAT;
                        }
                    }
                }

                // If bad format, we need to free the data
                if(nError != ERROR_SUCCESS)
                {
                    memset(&SubHeader, 0, sizeof(TVFS_DIRECTORY_HEADER));
                    CASC_FREE(pbFileData);
                }
            }
        }

        // Close the file handle
        CascCloseFile(hFile);
    }

    return nError;
}

static DWORD ParsePathFileTable(
    TRootHandler_TVFS * pRootHandler,
    TVFS_DIRECTORY_HEADER & DirHeader,
    PATH_BUFFER & PathBuffer,
    LPBYTE pbPathTablePtr,
    LPBYTE pbPathTableEnd)
{
    TVFS_DIRECTORY_HEADER SubHeader;
    TVFS_PATH_TABLE_ENTRY PathEntry;
    ENCODED_KEY EKey;
    char * szSavePathPtr = PathBuffer.szPtr;
    DWORD dwSpanSize = 0;
    int nError;

    // Prepare the EKey structure to be filled with zeros
    memset(&EKey, 0, sizeof(ENCODED_KEY));

    // Parse the file table
    while(pbPathTablePtr < pbPathTableEnd)
    {
        //
        // Structure of the path table entry:
        // (1byte) File name length
        // (?byte) File name
        // (1byte) Zero terminator, if this is the last fragment in the node name
        // (1byte) Name terminator (0xFF)
        // (4byte) Node Value
        //
        // Note: The path "data\archive\maps\file.bmp" could be cut into nodes like:
        //                 data\0 (or data with subdirectory)
        //                   arc
        //                     hive\0
        //                       maps\0 (or folder data)
        //                         file.bmp
        //

        pbPathTablePtr = CapturePathEntry(PathEntry, pbPathTablePtr, pbPathTableEnd);
        if(pbPathTablePtr == NULL)
            return ERROR_BAD_FORMAT;

        // Append the node name to the total path. Also add backslash, if it's a folder
        PathBuffer_AppendNode(PathBuffer, PathEntry);

        // If the TVFS_FOLDER_NODE is set, then the path node is a directory,
        // with its data immediately following the path node. Lower 31 bits of NodeValue
        // contain the length of the directory (including the NodeValue!)
        if(PathEntry.NodeValue & TVFS_FOLDER_NODE)
        {
            LPBYTE pbDirectoryEnd = pbPathTablePtr + (PathEntry.NodeValue & TVFS_FOLDER_SIZE_MASK) - sizeof(DWORD);

            // Check for the minimum folder size
            assert((PathEntry.NodeValue & TVFS_FOLDER_SIZE_MASK) >= sizeof(DWORD));

            // Recursively call the folder parser on the same file
            nError = ParsePathFileTable(pRootHandler, DirHeader, PathBuffer, pbPathTablePtr, pbDirectoryEnd);
            if(nError != ERROR_SUCCESS)
                return nError;

            // Skip the directory data
            pbPathTablePtr = pbDirectoryEnd;
        }
        else
        {
            // Capture the VFS and Container Table Entry in order to get the file EKey
            nError = CaptureVfsSpanEntries(DirHeader, &EKey, &dwSpanSize, PathEntry.NodeValue);
            if(nError != ERROR_SUCCESS)
                return nError;

            // We need to check whether this is another subdirectory.
            if(IsSubDirectory(pRootHandler, DirHeader, SubHeader, EKey, dwSpanSize) == ERROR_SUCCESS)
            {
                // Add one extra backslash to the node
                PathBuffer_AddBackslash(PathBuffer);

                // Insert the file to the file tree
                FileTree_Insert(&pRootHandler->FileTree, &EKey, PathBuffer.szBegin, dwSpanSize);

                ParseDirectoryData(pRootHandler, PathBuffer, SubHeader);
                CASC_FREE(SubHeader.pbDirectoryData);
            }
            else
            {
                // Insert the file to the file tree
                FileTree_Insert(&pRootHandler->FileTree, &EKey, PathBuffer.szBegin, dwSpanSize);
            }
        }

        // Reset the position of the path buffer
        PathBuffer.szPtr = szSavePathPtr;
    }

    // Return the total number of entries
    return ERROR_SUCCESS;
}

static int ParseDirectoryData(TRootHandler_TVFS * pRootHandler, PATH_BUFFER & PathBuffer, TVFS_DIRECTORY_HEADER & DirHeader)
{
    LPBYTE pbRootDirectory = DirHeader.pbDirectoryData + DirHeader.PathTableOffset;
    LPBYTE pbRootDirPtr = pbRootDirectory;
    LPBYTE pbRootDirEnd = pbRootDirPtr + DirHeader.PathTableSize;
    DWORD dwNodeValue = 0;

    // Most usually, there is a root directory in the folder
    if((pbRootDirPtr + 1 + sizeof(DWORD)) < pbRootDirEnd)
    {
        //
        // The structure of the root directory
        // -----------------------------------
        // 1byte   0xFF
        // 4bytes  NodeValue (BigEndian). The most significant bit is set
        //          - Lower 31 bits contain length of the directory data, including NodeValue
        //

        if(pbRootDirPtr[0] == 0xFF)
        {
            // Get the NodeValue and check its highest bit
            if(CaptureInteger32_BE(pbRootDirPtr + 1, pbRootDirEnd, &dwNodeValue) == NULL || (dwNodeValue & TVFS_FOLDER_NODE) == 0)
                return ERROR_BAD_FORMAT;
            
            // Get the range of the root directory
            pbRootDirEnd = pbRootDirPtr + 1 + (dwNodeValue & TVFS_FOLDER_SIZE_MASK);
            pbRootDirPtr = pbRootDirPtr + 1 + sizeof(DWORD);

            // Check the directory
            if(pbRootDirEnd > (pbRootDirectory + DirHeader.PathTableSize))
                return ERROR_BAD_FORMAT;
        }
    }

    // Now go parse the path file table
    return ParsePathFileTable(pRootHandler, DirHeader, PathBuffer, pbRootDirPtr, pbRootDirEnd);
}

//-----------------------------------------------------------------------------
// Public functions - TVFS root

int RootHandler_CreateTVFS(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile)
{
    TRootHandler_TVFS * pRootHandler;
    TVFS_DIRECTORY_HEADER RootHeader;
    PATH_BUFFER PathBuffer;
    char szPathBuffer[MAX_PATH];
    int nError;

    // Capture the entire root directory
    nError = CaptureDirectoryHeader(RootHeader, pbRootFile, pbRootFile + cbRootFile);
    if(nError == ERROR_SUCCESS)
    {
        // Allocate the root handler object
        hs->pRootHandler = pRootHandler = CASC_ALLOC(TRootHandler_TVFS, 1);
        if(pRootHandler == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Fill-in the handler functions
        InitRootHandler_FileTree(pRootHandler, sizeof(TRootHandler_TVFS));

        // Allocate the generic file tree
        nError = FileTree_Create(&pRootHandler->FileTree, FTREE_FLAG_USE_FILE_SIZE);
        if(nError == ERROR_SUCCESS)
        {
            // We have file names. We return EKeys, not CKeys
            pRootHandler->dwRootFlags |= (ROOT_FLAG_HAS_NAMES | ROOT_FLAG_USES_EKEY | ROOT_FLAG_DONT_SEARCH_CKEY);
            pRootHandler->hs = hs;

            // Initialize the path buffer
            memset(szPathBuffer, 0, sizeof(szPathBuffer));
            PathBuffer.szBegin =
            PathBuffer.szPtr = szPathBuffer;
            PathBuffer.szEnd = szPathBuffer + MAX_PATH;

            // Parse the entire directory data
            nError = ParseDirectoryData(pRootHandler, PathBuffer, RootHeader);
        }
    }

    return nError;
}
