/****************************************************************************/
/* CascOpenFile.cpp                       Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* System-dependent directory functions for CascLib                          */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 01.05.14  1.00  Lad  The first version of CascOpenFile.cpp                */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local structures

#define BLTE_HEADER_SIGNATURE   0x45544C42      // 'BLTE' header in the data files
#define BLTE_HEADER_DELTA       0x1E            // Distance of BLTE header from begin of the header area
#define MAX_HEADER_AREA_SIZE    0x2A            // Length of the file header area

typedef struct _BLTE_ENCODED_HEADER
{
    ENCODED_KEY EKey;                           // Encoded key of the file. Byte-reversed
    DWORD EncodedSize;                          // Encoded size of the file (little endian)
    BYTE  SomeSize[4];                          // Some size (big endian)
    BYTE  Padding[6];                           // Padding (?)
    BYTE  Signature[4];                         // Must be "BLTE"
    BYTE  HeaderSize[4];                        // Header size in bytes (big endian)
    BYTE  MustBe0F;                             // Must be 0x0F. Optional, only if HeaderSize != 0
    BYTE  FrameCount[3];                        // Frame count (big endian). Optional, only if HeaderSize != 0
} BLTE_ENCODED_HEADER, *PBLTE_ENCODED_HEADER;

typedef struct _BLTE_FRAME
{
    BYTE EncodedSize[4];                        // Encoded frame size (big endian)
    BYTE ContentSize[4];                        // Content frame size (big endian)
    BYTE FrameHash[MD5_HASH_SIZE];              // Hash of the encoded frame

} BLTE_FRAME, *PBLTE_FRAME;

//-----------------------------------------------------------------------------
// Local functions

TCascFile * IsValidFileHandle(HANDLE hFile);        // In CascOpenFile.cpp

static int EnsureDataStreamIsOpen(TCascFile * hf)
{
    TCascStorage * hs = hf->hs;
    TFileStream * pStream = NULL;
    TCHAR * szDataFile;
    TCHAR szPlainName[0x40];

    // If the file is not open yet, do it
    if(hs->DataFiles[hf->ArchiveIndex] == NULL)
    {
        // Prepare the name of the data file
        _stprintf(szPlainName, _T("data.%03u"), hf->ArchiveIndex);
        szDataFile = CombinePath(hs->szIndexPath, szPlainName);

        // Open the data file
        if(szDataFile != NULL)
        {
            // Open the stream. Make sure that the encoded handlers will fill eventual
            // missing data with zeros. Observed in Overwatch build 24919
            pStream = FileStream_OpenFile(szDataFile, STREAM_FLAG_READ_ONLY | STREAM_PROVIDER_FLAT | STREAM_FLAG_FILL_MISSING | BASE_PROVIDER_FILE);
            hs->DataFiles[hf->ArchiveIndex] = pStream;
            CASC_FREE(szDataFile);
        }
    }

    // Return error or success
    hf->pStream = hs->DataFiles[hf->ArchiveIndex];
    return (hf->pStream != NULL) ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}

static DWORD GetEncodedHeaderDelta(TFileStream * pStream, DWORD HeaderOffset)
{
    ULONGLONG FileOffset = HeaderOffset;
    DWORD FourBytes = 0;

    // Read four bytes from the position. If it contains 'BLTE',
    // then we need to move back
    if(!FileStream_Read(pStream, &FileOffset, &FourBytes, sizeof(DWORD)))
        return CASC_INVALID_POS;

    return (FourBytes == BLTE_HEADER_SIGNATURE) ? BLTE_HEADER_DELTA : 0;
}

static int LoadFileFrames(TCascFile * hf, ULONGLONG DataFileOffset)
{
    PBLTE_FRAME pFileFrames;
    PBLTE_FRAME pFileFrame;
    DWORD ContentSize = 0;
    DWORD FileOffset = 0;
    int nError = ERROR_SUCCESS;

    assert(hf != NULL);
    assert(hf->pStream != NULL);
    assert(hf->pFrames != NULL);

    // Allocate frame array
    pFileFrames = pFileFrame = CASC_ALLOC(BLTE_FRAME, hf->FrameCount);
    if(pFileFrames != NULL)
    {
        // Load the frame array
        if(FileStream_Read(hf->pStream, &DataFileOffset, pFileFrames, hf->FrameCount * sizeof(BLTE_FRAME)))
        {
            // Move the raw archive offset
            DataFileOffset += (hf->FrameCount * sizeof(BLTE_FRAME));

            // Copy the frames to the file structure
            for(DWORD i = 0; i < hf->FrameCount; i++, pFileFrame++)
            {
                hf->pFrames[i].DataFileOffset = (DWORD)DataFileOffset;
                hf->pFrames[i].FileOffset = FileOffset;
                hf->pFrames[i].EncodedSize = ConvertBytesToInteger_4(pFileFrame->EncodedSize);
                hf->pFrames[i].ContentSize = ConvertBytesToInteger_4(pFileFrame->ContentSize);
                memcpy(hf->pFrames[i].FrameHash, pFileFrame->FrameHash, MD5_HASH_SIZE);

                DataFileOffset += hf->pFrames[i].EncodedSize;
                ContentSize += hf->pFrames[i].ContentSize;
                FileOffset += hf->pFrames[i].ContentSize;
            }
        }
        else
            nError = GetLastError();

        // Note: on ENCODING file, this value is almost always bigger
        // then the real size of ENCODING. This is not a problem,
        // because reading functions will pad the file with zeros
        hf->ContentSize = ContentSize;

        // Free the array
        CASC_FREE(pFileFrames);
    }
    else
        nError = ERROR_NOT_ENOUGH_MEMORY;

    return nError;
}

static int LoadBlteHeader(
    BLTE_ENCODED_HEADER & BlteHeader,
    TFileStream * pStream,
    DWORD DataFileOffset,
    DWORD EncodedSize)
{
    ULONGLONG FileOffset = DataFileOffset;
    DWORD dwBytesToRead = CASCLIB_MIN(sizeof(BLTE_ENCODED_HEADER), EncodedSize);

    // Storage: "2017 - Starcraft1/2457"
    // File1: "locales\\itIT\\Assets\\SD\\campaign\\Starcraft\\SWAR\\staredit\\scenario.chk"
    // File2: "locales\\esES\\Assets\\campaign\\EXPProtoss\\Protoss03\\staredit\\wav\\nullsound.ogg"
    // EncodedSize = 0x27
    assert(EncodedSize > FIELD_OFFSET(BLTE_ENCODED_HEADER, MustBe0F));

    // Read as much as possible
    if(!FileStream_Read(pStream, &FileOffset, &BlteHeader, dwBytesToRead))
        return ERROR_FILE_CORRUPT;

    // Fill the rest with zeros
    if(dwBytesToRead < sizeof(BLTE_ENCODED_HEADER))
    {
        LPBYTE pbHeaderPad = (LPBYTE)(&BlteHeader) + dwBytesToRead;
        size_t cbHeaderPad = sizeof(BLTE_ENCODED_HEADER) - dwBytesToRead;

        memset(pbHeaderPad, 0, cbHeaderPad);
    }

    return ERROR_SUCCESS;
}

static int LoadEncodedHeaderAndFileFrames(TCascFile * hf)
{
    TCascStorage * hs = hf->hs;
    BLTE_ENCODED_HEADER BlteHeader;
    DWORD DataFileOffset = hf->ArchiveOffset - hs->dwHeaderDelta;
    DWORD BlteSignature = 0;
    DWORD EncodedSize = 0;
    DWORD HeaderSize = 0;
    int nError = ERROR_SUCCESS;

    // Should only be called when the file frames are NOT loaded
    assert(hf->pFrames == NULL);
    assert(hf->FrameCount == 0);
    
    // Load the encoded BLTE header. Note that it could be as small as 0x27 bytes
    nError = LoadBlteHeader(BlteHeader, hf->pStream, hf->ArchiveOffset - hs->dwHeaderDelta, hf->EncodedSize);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Capture the EKey
    hf->EKey = BlteHeader.EKey;

    // Capture the encoded size
    assert(BlteHeader.EncodedSize == hf->EncodedSize);
    EncodedSize = BlteHeader.EncodedSize;

    // Capture the BLTE signature
    BlteSignature = ConvertBytesToInteger_4_LE(BlteHeader.Signature);
    if(BlteSignature != BLTE_HEADER_SIGNATURE)
        return ERROR_FILE_CORRUPT;

    // Capture the header size
    HeaderSize = ConvertBytesToInteger_4(BlteHeader.HeaderSize);
    if(HeaderSize > EncodedSize)
        return ERROR_FILE_CORRUPT;

    // Decrement the encoded size by the (so-far) header data
    DataFileOffset += FIELD_OFFSET(BLTE_ENCODED_HEADER, MustBe0F);
    EncodedSize -= FIELD_OFFSET(BLTE_ENCODED_HEADER, MustBe0F);

    // If the header size is nonzero, there is list of frame headers following
    if(HeaderSize != 0)
    {
        // The next byte must be 0x0F
        if(BlteHeader.MustBe0F != 0x0F)
            return ERROR_BAD_FORMAT;

        // Save the number of file frames
        hf->FrameCount = ConvertBytesToInteger_3(BlteHeader.FrameCount);
        if (((hf->FrameCount * sizeof(BLTE_FRAME)) + 0x0C) != HeaderSize)
            return ERROR_BAD_FORMAT;

        // Allocate space for the file frames
        hf->pFrames = CASC_ALLOC(CASC_FILE_FRAME, hf->FrameCount);
        if(hf->pFrames == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(hf->pFrames, 0, hf->FrameCount * sizeof(CASC_FILE_FRAME));

        // Load the frames from the file
        nError = LoadFileFrames(hf, DataFileOffset + 4);
    }
    else
    {
        // The content size in the file header must be valid at this point,
        // otherwise we don't know the frame content size
        if(hf->ContentSize == CASC_INVALID_SIZE)
        {
            assert(false);
            return ERROR_CAN_NOT_COMPLETE;
        }

        // Save the number of file frames
        hf->FrameCount = 1;

        // Allocate space for the file frames
        hf->pFrames = CASC_ALLOC(CASC_FILE_FRAME, hf->FrameCount);
        if(hf->pFrames == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(hf->pFrames, 0, hf->FrameCount * sizeof(CASC_FILE_FRAME));

        // Create one artificial frame
        hf->pFrames->DataFileOffset = DataFileOffset;
        hf->pFrames->FileOffset = 0;
        hf->pFrames->EncodedSize = EncodedSize;
        hf->pFrames->ContentSize = hf->ContentSize;
    }

    return nError;
}

static int EnsureFileFramesLoaded(TCascFile * hf)
{
    // If the encoded frames are not loaded, do it now
    if(hf->pFrames == NULL)
    {
        TCascStorage * hs = hf->hs;
        int nError;

        // We need the data file to be open
        nError = EnsureDataStreamIsOpen(hf);
        if(nError != ERROR_SUCCESS)
            return nError;

        // Make sure that we already know the header delta value.
        // - Heroes of the Storm (older builds)'s CASC_EKEY_ENTRY::ArchiveAndOffset point to the BLTE signature in the encoded header.
        // - Heroes of the Storm (newer builds)'s CASC_EKEY_ENTRY::ArchiveAndOffset point to the encoded file header itself.
        // Solve this once for the entire storage
        if(hs->dwHeaderDelta == CASC_INVALID_POS)
        {
            hs->dwHeaderDelta = GetEncodedHeaderDelta(hf->pStream, hf->ArchiveOffset);
            if(hs->dwHeaderDelta == CASC_INVALID_POS)
                return ERROR_FILE_CORRUPT;
        }

        // Make sure we have header area loaded
        return LoadEncodedHeaderAndFileFrames(hf);
    }

    return ERROR_SUCCESS;
}

static PCASC_FILE_FRAME FindFileFrame(TCascFile * hf, DWORD FileOffset)
{
    PCASC_FILE_FRAME pFrame = hf->pFrames;

    // Sanity checks
    assert(hf->pFrames != NULL);
    assert(hf->FrameCount != 0);

    // Find the frame where to read from
    for(DWORD i = 0; i < hf->FrameCount; i++, pFrame++)
    {
        // Does the read request fit into the current frame?
        if(pFrame->FileOffset <= FileOffset && FileOffset < pFrame->FileOffset + pFrame->ContentSize)
            return pFrame;
    }

    // Does the read request ends at the end of the last frame?
    if(hf->FrameCount > 0 && FileOffset == hf->ContentSize)
        return hf->pFrames + hf->FrameCount - 1;

    // Not found, sorry
    return NULL;
}
/*
// Make sure we have enough file cache to keep all data from 0 to the end of the last loaded frame
static int AllocateFileCache(TCascFile * hf, PCASC_FILE_FRAME pEndFrame)
{
    DWORD cbFileCache = pEndFrame->FileOffset + pEndFrame->ContentSize;

    // If there's no cache, just allocate whole block
    if(hf->pbFileCache == NULL)
    {
        // Allocate cache block
        hf->pbFileCache = CASC_ALLOC(BYTE, cbFileCache);
        if(hf->pbFileCache == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Setup the cache block
        memset(hf->pbFileCache, 0, cbFileCache);
        hf->cbFileCache = cbFileCache;
    }
    
    // If there is cache, but smaller, we need to enlarge it
    else if(cbFileCache > hf->cbFileCache)
    {
        DWORD cbNewSize = hf->cbFileCache;

        // Double the size of the cache until we have enough. Don't go over the file size.
        while(cbNewSize < cbFileCache)
            cbNewSize = cbNewSize * 2;
        if(cbNewSize > hf->ContentSize)
            cbNewSize = hf->ContentSize;
        cbFileCache = cbNewSize;

        // Reallocate cache block
        hf->pbFileCache = CASC_REALLOC(BYTE, hf->pbFileCache, cbFileCache);
        if(hf->pbFileCache == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Setup the cache block
        memset(hf->pbFileCache + hf->cbFileCache, 0, cbFileCache - hf->cbFileCache);
        hf->cbFileCache = cbFileCache;
    }

    return ERROR_SUCCESS;
}
*/
static int LoadEncodedFrame(TFileStream * pStream, PCASC_FILE_FRAME pFrame, LPBYTE pbEncodedFrame)
{
    ULONGLONG FileOffset = pFrame->DataFileOffset;
    int nError = ERROR_SUCCESS;

    // Load the encoded frame to memory
    if(FileStream_Read(pStream, &FileOffset, pbEncodedFrame, pFrame->EncodedSize))
    {
        if(!VerifyDataBlockHash(pbEncodedFrame, pFrame->EncodedSize, pFrame->FrameHash))
            nError = ERROR_FILE_CORRUPT;
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
}

static int ProcessFileFrame(
    LPBYTE pbOutBuffer,
    DWORD  cbOutBuffer,
    LPBYTE pbInBuffer,
    DWORD cbInBuffer,
    DWORD dwFrameIndex)
{
    LPBYTE pbWorkBuffer = NULL;
    DWORD cbWorkBuffer = 0;
    DWORD dwStepCount = 0;
    bool bWorkComplete = false;
    int nError = ERROR_SUCCESS;

    // Perform the loop
    while(bWorkComplete == false)
    {
        // There should never be a 3rd step
        assert(dwStepCount < 2);

        // Perform the operation specific by the first byte
        switch(pbInBuffer[0])
        {
            case 'E':   // Encrypted files
                
                // The work buffer should not have been allocated by any step
                assert(pbWorkBuffer == NULL && cbWorkBuffer == 0);

                // Allocate temporary buffer to decrypt into
                // Example storage: "2016 - WoW/23420", File: "4ee6bc9c6564227f1748abd0b088e950"
                pbWorkBuffer = CASC_ALLOC(BYTE, cbInBuffer - 1);
                cbWorkBuffer = cbInBuffer - 1;
                if(pbWorkBuffer == NULL)
                    return ERROR_NOT_ENOUGH_MEMORY;

                // Decrypt the stream to the work buffer
                nError = CascDecrypt(pbWorkBuffer, &cbWorkBuffer, pbInBuffer + 1, cbInBuffer - 1, dwFrameIndex);
                if(nError != ERROR_SUCCESS)
                {
                    bWorkComplete = true;
                    break;
                }

                // When encrypted, there is always one more step after this.
                // Setup the work buffer as input buffer for the next operation
                pbInBuffer = pbWorkBuffer;
                cbInBuffer = cbWorkBuffer;
                break;

            case 'Z':   // ZLIB compressed files
                
                // If we decompressed less than expected, we simply fill the rest with zeros
                // Example: INSTALL file from the TACT CASC storage
                nError = CascDecompress(pbOutBuffer, &cbOutBuffer, pbInBuffer + 1, cbInBuffer - 1);
                if(nError == ERROR_SUCCESS && cbOutBuffer < cbOutBuffer)
                    memset(pbOutBuffer + cbOutBuffer, 0, (cbOutBuffer - cbOutBuffer));
                bWorkComplete = true;
                break;

            case 'N':   // Normal stored files
                nError = CascDirectCopy(pbOutBuffer, &cbOutBuffer, pbInBuffer + 1, cbInBuffer - 1);
                bWorkComplete = true;
                break;

            case 'F':   // Recursive frames - not supported
            default:    // Unrecognized - if we unpacked something, we consider it done
                nError = ERROR_NOT_SUPPORTED;
                bWorkComplete = true;
                assert(false);
                break;
        }

        // Increment the step count
        dwStepCount++;
    }

    // Free the temporary buffer
    if(pbWorkBuffer != NULL)
        CASC_FREE(pbWorkBuffer);
    return nError;
}

/*
static void BruteForceHash(TFileStream * pStream, ULONGLONG DataFileOffset, DWORD EncodedSize, LPBYTE FrameHash)
{
    hash_state md5_state2;
    hash_state md5_state;
    BYTE block_md5[MD5_HASH_SIZE];
    BYTE OneByte[1];
    DWORD dwLength = 0;

    md5_init(&md5_state);

    while(FileStream_Read(pStream, &DataFileOffset, OneByte, 1))
    {
        // Process the hash
        md5_process(&md5_state, OneByte, 1);
        
        // Match?
        md5_state2 = md5_state;
        md5_done(&md5_state2, block_md5);
        if(!memcmp(block_md5, FrameHash, MD5_HASH_SIZE))
            break;
        dwLength++;
    }
}
*/
//-----------------------------------------------------------------------------
// Public functions

bool WINAPI CascGetFileInfo(HANDLE hFile, CASC_FILE_INFO_CLASS InfoClass, void * pvOutFileInfo, size_t cbOutFileInfo, size_t * pcbLengthNeeded)
{
    TCascFile * hf;
    void * pvFileInfo = NULL;
    size_t cbFileInfo = 0;

    // Validate the file handle
    if((hf = IsValidFileHandle(hFile)) == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Differentiate between info classes
    switch(InfoClass)
    {
        case CascFileContentKey:

            // Do we have content key at all?
            if(!IsValidMD5(hf->CKey.Value))
            {
                SetLastError(ERROR_NOT_SUPPORTED);
                return false;
            }

            // Give the content key
            pvFileInfo = hf->CKey.Value;
            cbFileInfo = CASC_CKEY_SIZE;
            break;

        case CascFileEncodedKey:

            // Do we have content key at all?
            if(!IsValidMD5(hf->EKey.Value))
            {
                SetLastError(ERROR_NOT_SUPPORTED);
                return false;
            }

            // Give the content key
            pvFileInfo = hf->EKey.Value;
            cbFileInfo = CASC_CKEY_SIZE;
            break;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
    }

    // Sanity check
    assert(pvFileInfo != NULL);
    assert(cbFileInfo != 0);

    // Always give the length needed
    if(pcbLengthNeeded != NULL)
    {
        pcbLengthNeeded[0] = cbFileInfo;
    }

    // Do we have enough space?
    if(cbFileInfo > cbOutFileInfo)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    // Give the information
    memcpy(pvOutFileInfo, pvFileInfo, cbFileInfo);
    return true;
}

//
// THE FILE SIZE PROBLEM
//
// There are members called "FileSize" in many CASC-related structure
// For various files, these variables have different meaning.
//
// Storage      FileName  FileSize     FrameSum    HdrArea     CKeyEntry   EKeyEntry   RootEntry
// -----------  --------  ----------   --------    --------    ----------  ----------  ----------
// HotS(29049)  ENCODING  0x0024BA45 - 0x0024b98a  0x0024BA45  n/a         0x0024BA45  n/a
// HotS(29049)  ROOT      0x00193340 - 0x00193340  0x0010db65  0x00193340  0x0010db65  n/a
// HotS(29049)  (other)   0x00001080 - 0x00001080  0x000008eb  0x00001080  0x000008eb  0x00001080
//                                                             
// WoW(18888)   ENCODING  0x030d487b - 0x030dee79  0x030d487b  n/a         0x030d487b  n/a
// WoW(18888)   ROOT      0x016a9800 - n/a         0x0131313d  0x016a9800  0x0131313d  n/a
// WoW(18888)   (other)   0x000007d0 - 0x000007d0  0x00000397  0x000007d0  0x00000397  n/a
//

DWORD WINAPI CascGetFileSize(HANDLE hFile, PDWORD pdwFileSizeHigh)
{
    TCascFile * hf;
    int nError;

    CASCLIB_UNUSED(pdwFileSizeHigh);

    // Validate the file handle
    if((hf = IsValidFileHandle(hFile)) == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return CASC_INVALID_SIZE;
    }

    // Someone may have provided file content size.
    // If yes, do not load the frames, as it's not necessary.
    if(hf->ContentSize == CASC_INVALID_SIZE)
    {
        // Make sure that the file header area is loaded
        nError = EnsureFileFramesLoaded(hf);
        if(nError != ERROR_SUCCESS)
        {
            SetLastError(nError);
            return CASC_INVALID_SIZE;
        }

        // The content size should be loaded from the frames
        assert(hf->ContentSize != CASC_INVALID_SIZE);
    }

    // Give the file size to the caller
    if(pdwFileSizeHigh != NULL)
        *pdwFileSizeHigh = 0;
    return hf->ContentSize;
}

DWORD WINAPI CascSetFilePointer(HANDLE hFile, LONG lFilePos, LONG * plFilePosHigh, DWORD dwMoveMethod)
{
    TCascFile * hf;
    ULONGLONG FilePosition;
    ULONGLONG MoveOffset;
    DWORD dwFilePosHi;

    // If the hFile is not a valid file handle, return an error.
    hf = IsValidFileHandle(hFile);
    if(hf == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return CASC_INVALID_POS;
    }

    // Get the relative point where to move from
    switch(dwMoveMethod)
    {
        case FILE_BEGIN:
            FilePosition = 0;
            break;

        case FILE_CURRENT:
            FilePosition = hf->FilePointer;
            break;

        case FILE_END:
            FilePosition = hf->ContentSize;
            break;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return CASC_INVALID_POS;
    }

    // Now get the move offset. Note that both values form
    // a signed 64-bit value (a file pointer can be moved backwards)
    if(plFilePosHigh != NULL)
        dwFilePosHi = *plFilePosHigh;
    else
        dwFilePosHi = (lFilePos & 0x80000000) ? 0xFFFFFFFF : 0;
    MoveOffset = MAKE_OFFSET64(dwFilePosHi, lFilePos);

    // Now calculate the new file pointer
    // Do not allow the file pointer to overflow
    FilePosition = ((FilePosition + MoveOffset) >= FilePosition) ? (FilePosition + MoveOffset) : 0;

    // CASC files can't be bigger than 4 GB.
    // We don't allow to go past 4 GB
    if(FilePosition >> 32)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return CASC_INVALID_POS;
    }

    // Change the file position
    hf->FilePointer = (DWORD)FilePosition;

    // Return the new file position
    if(plFilePosHigh != NULL)
        *plFilePosHigh = 0;
    return hf->FilePointer;
}

bool WINAPI CascReadFile(HANDLE hFile, void * pvBuffer, DWORD dwBytesToRead, PDWORD pdwBytesRead)
{
    PCASC_FILE_FRAME pFrame1 = NULL;
    PCASC_FILE_FRAME pFrame2 = NULL;
    TCascFile * hf;
    int nError = ERROR_SUCCESS;

    // The buffer must be valid
    if(pvBuffer == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Validate the file handle
    if((hf = IsValidFileHandle(hFile)) == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // If the file frames are not loaded yet, do it now
    if(nError == ERROR_SUCCESS)
    {
        nError = EnsureFileFramesLoaded(hf);
    }

    // If the file position is at or beyond end of file, do nothing
    if(nError == ERROR_SUCCESS)
    {
        // Check the starting position
        if(hf->FilePointer >= hf->ContentSize)
        {
            *pdwBytesRead = 0;
            return true;
        }

        // Check the ending position
        if((hf->FilePointer + dwBytesToRead) > hf->ContentSize)
        {
            dwBytesToRead = hf->ContentSize - hf->FilePointer;
        }

        // Find the starting and ending frame where to read from
        pFrame1 = FindFileFrame(hf, hf->FilePointer);
        pFrame2 = FindFileFrame(hf, hf->FilePointer + dwBytesToRead);
        if(pFrame1 == NULL || pFrame2 == NULL)
            nError = ERROR_FILE_CORRUPT;

        // Sanity check
        assert((hf->FilePointer + dwBytesToRead) <= (pFrame2->FileOffset + pFrame2->ContentSize));
    }

    // Allocate cache buffer for the entire file. This is the fastest approach
    // (without reallocations). However, this may consume quite a lot of memory
    // (Storage: "2016 - Starcraft II/45364", file: "3d815f40c0413701aa2bd214070d0062"
    // needs 0x239a09b3 bytes of memory (~600 MB)
    if(nError == ERROR_SUCCESS)
    {
        if(hf->pbFileCache == NULL)
        {
            // Allocate buffer
            hf->pbFileCache = CASC_ALLOC(BYTE, hf->ContentSize);
            if(hf->pbFileCache == NULL)
                nError = ERROR_NOT_ENOUGH_MEMORY;

            // Prepare buffer
            memset(hf->pbFileCache, 0, hf->ContentSize);
            hf->cbFileCache = hf->ContentSize;
        }
//      nError = AllocateFileCache(hf, pFrame2);
    }

    // Load all frames that are not loaded yet
    if(nError == ERROR_SUCCESS)
    {
        for(PCASC_FILE_FRAME pFrame = pFrame1; pFrame <= pFrame2; pFrame++)
        {
            LPBYTE pbDecodedFrame = hf->pbFileCache + pFrame->FileOffset;
            LPBYTE pbEncodedFrame;

            // Is the frame already loaded?
            if(!pFrame->IsFrameLoaded)
            {
                // Allocate space for the encoded frame
                pbEncodedFrame = CASC_ALLOC(BYTE, pFrame->EncodedSize);
                if(pbEncodedFrame == NULL)
                {
                    nError = ERROR_NOT_ENOUGH_MEMORY;
                    break;
                }

                // Load the encoded frame data
                nError = LoadEncodedFrame(hf->pStream, pFrame, pbEncodedFrame);
                if(nError != ERROR_SUCCESS)
                    break;

                // Decode the frame
                nError = ProcessFileFrame(pbDecodedFrame,
                                          pFrame->ContentSize,
                                          pbEncodedFrame,
                                          pFrame->EncodedSize,
                                  (DWORD)(pFrame - hf->pFrames));
                if(nError != ERROR_SUCCESS)
                    break;

                // Move the frame pointer
                pFrame->IsFrameLoaded = 1;
                CASC_FREE(pbEncodedFrame);
            }
        }
    }

    // Now all frames have been loaded into the cache; copy the entire block to the output buffer
    if(nError == ERROR_SUCCESS)
    {
        // Copy the entire data
        memcpy(pvBuffer, hf->pbFileCache + hf->FilePointer, dwBytesToRead);
        hf->FilePointer += dwBytesToRead;

        // Give the number of bytes read
        if(pdwBytesRead != NULL)
            *pdwBytesRead = dwBytesToRead;
        return true;
    }
    else
    {
        SetLastError(nError);
        return false;
    }
}

/*
bool WINAPI CascReadFile(HANDLE hFile, void * pvBuffer, DWORD dwBytesToRead, PDWORD pdwBytesRead)
{
    PCASC_FILE_FRAME pFrame = NULL;
    ULONGLONG DataFileOffset;
    ULONGLONG StreamSize;
    TCascFile * hf;
    LPBYTE pbBuffer = (LPBYTE)pvBuffer;
    DWORD dwStartPointer = 0;
    DWORD dwFilePointer = 0;
    DWORD dwEndPointer = 0;
    DWORD dwFrameSize;
    bool bReadResult;
    int nError = ERROR_SUCCESS;

    // The buffer must be valid
    if(pvBuffer == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Validate the file handle
    if((hf = IsValidFileHandle(hFile)) == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // If the file frames are not loaded yet, do it now
    if(nError == ERROR_SUCCESS)
    {
        nError = EnsureFileFramesLoaded(hf);
    }

    // If the file position is at or beyond end of file, do nothing
    if(nError == ERROR_SUCCESS && hf->FilePointer >= hf->ContentSize)
    {
        *pdwBytesRead = 0;
        return true;
    }

    // Find the file frame where to read from
    if(nError == ERROR_SUCCESS)
    {
        // Get the frame
        pFrame = FindFileFrame(hf, hf->FilePointer);
        if(pFrame == NULL || pFrame->EncodedSize < 1)
            nError = ERROR_FILE_CORRUPT;
    }

    // Perform the read
    if(nError == ERROR_SUCCESS)
    {
        // If not enough bytes in the file remaining, cut them
        dwStartPointer = dwFilePointer = hf->FilePointer;
        dwEndPointer = dwStartPointer + dwBytesToRead;
        if(dwEndPointer > hf->ContentSize)
            dwEndPointer = hf->ContentSize;

        // Perform block read from each file frame
        while(dwFilePointer < dwEndPointer)
        {
            LPBYTE pbFrameData = NULL;
            DWORD dwFrameStart = pFrame->FileOffset;
            DWORD dwFrameEnd = pFrame->FileOffset + pFrame->ContentSize;

            // Shall we populate the cache with a new data?
            if(dwFrameStart != hf->CacheStart || hf->CacheEnd != dwFrameEnd)
            {
                // Shall we reallocate the cache buffer?
                if(pFrame->ContentSize > hf->cbFileCache)
                {
                    // Free the current file cache
                    if(hf->pbFileCache != NULL)
                        CASC_FREE(hf->pbFileCache);
                    hf->cbFileCache = 0;
                    
                    // Allocate a new file cache
                    hf->pbFileCache = CASC_ALLOC(BYTE, pFrame->ContentSize);
                    if(hf->pbFileCache == NULL)
                    {
                        nError = ERROR_NOT_ENOUGH_MEMORY;
                        break;
                    }

                    // Set the file cache length
                    hf->cbFileCache = pFrame->ContentSize;
                }

                // We also need to allocate buffer for the raw data
                pbFrameData = CASC_ALLOC(BYTE, pFrame->EncodedSize);
                if(pbFrameData == NULL)
                {
                    nError = ERROR_NOT_ENOUGH_MEMORY;
                    break;
                }

                // Load the raw file data to memory
                DataFileOffset = pFrame->DataFileOffset;
                bReadResult = FileStream_Read(hf->pStream, &DataFileOffset, pbFrameData, pFrame->EncodedSize);

                // Note: The raw file data size could be less than expected
                // Happened in WoW build 19342 with the ROOT file. MD5 in the frame header
                // is zeroed, which means it should not be checked
                // Frame File: data.029
                // Frame Offs: 0x013ED9F0 size 0x01325B32
                // Frame End:  0x02713522
                // File Size:  0x027134FC
                if(bReadResult == false && GetLastError() == ERROR_HANDLE_EOF && !IsValidMD5(pFrame->FrameHash))
                {
                    // Get the size of the remaining file
                    FileStream_GetSize(hf->pStream, &StreamSize);
                    dwFrameSize = (DWORD)(StreamSize - DataFileOffset);

                    // If the frame offset is before EOF and frame end is beyond EOF, correct it
                    if(DataFileOffset < StreamSize && dwFrameSize < pFrame->EncodedSize)
                    {
                        memset(pbFrameData + dwFrameSize, 0, (pFrame->EncodedSize - dwFrameSize));
                        bReadResult = true;
                    }
                }

                // If the read result failed, we cannot finish reading it
                if(bReadResult && VerifyDataBlockHash(pbFrameData, pFrame->EncodedSize, pFrame->FrameHash))
                {
                    // Convert the source frame to the file cache
                    nError = ProcessFileFrame(hf->pbFileCache,
                                              pFrame->ContentSize,
                                              pbFrameData,
                                              pFrame->EncodedSize,
                                      (DWORD)(pFrame - hf->pFrames));
                    if(nError == ERROR_SUCCESS)
                    {
                        // Set the start and end of the cache
                        hf->CacheStart = dwFrameStart;
                        hf->CacheEnd = dwFrameEnd;
                    }
                }
                else
                {
                    // Try to find the data which have the given hash
//                  BruteForceHash(hf->pStream, DataFileOffset, pFrame->EncodedSize, pFrame->FrameHash);
                    nError = ERROR_FILE_CORRUPT;
                }

                // Free the raw frame data
                CASC_FREE(pbFrameData);
            }

            // Copy the decompressed data
            if(dwFrameEnd > dwEndPointer)
                dwFrameEnd = dwEndPointer;
            memcpy(pbBuffer, hf->pbFileCache + (dwFilePointer - dwFrameStart), (dwFrameEnd - dwFilePointer));
            pbBuffer += (dwFrameEnd - dwFilePointer);

            // Move pointers
            dwFilePointer = dwFrameEnd;
            pFrame++;
        }
    }

    // Update the file position
    if(nError == ERROR_SUCCESS)
    {
        if(pdwBytesRead != NULL)
            *pdwBytesRead = (dwFilePointer - dwStartPointer);
        hf->FilePointer = dwFilePointer;
    }

    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}
*/
