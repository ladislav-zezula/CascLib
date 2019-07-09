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
// Local functions

static int OpenDataStream(TCascStorage * hs, PCASC_FILE_SPAN pFileSpan, PCASC_CKEY_ENTRY pCKeyEntry, bool bDownloadFileIf)
{
    TFileStream * pStream = NULL;
    TCHAR * szDataFile;
    TCHAR szCachePath[MAX_PATH];
    TCHAR szPlainName[0x80];
    int nError;

    // If the file is available locally, we rely on data files.
    // If not, we download the file and open the stream
    if(pCKeyEntry->Flags & CASC_CE_FILE_IS_LOCAL)
    {
        DWORD dwArchiveIndex = pFileSpan->ArchiveIndex;

        // If the file is not open yet, do it
        if(hs->DataFiles[dwArchiveIndex] == NULL)
        {
            // Prepare the name of the data file
            CascStrPrintf(szPlainName, _countof(szPlainName), _T("data.%03u"), dwArchiveIndex);
            szDataFile = CombinePath(hs->szIndexPath, szPlainName);

            // Open the data file
            if(szDataFile != NULL)
            {
                // Open the data stream with read+write sharing to prevent Battle.net agent
                // detecting a corruption and redownloading the entire package
                pStream = FileStream_OpenFile(szDataFile, STREAM_FLAG_READ_ONLY | STREAM_FLAG_WRITE_SHARE | STREAM_PROVIDER_FLAT | STREAM_FLAG_FILL_MISSING | BASE_PROVIDER_FILE);
                hs->DataFiles[dwArchiveIndex] = pStream;
                CASC_FREE(szDataFile);
            }
        }

        // Return error or success
        pFileSpan->pStream = hs->DataFiles[dwArchiveIndex];
        return (pFileSpan->pStream != NULL) ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
    }
    else
    {
        if(bDownloadFileIf)
        {
            // Create the local folder path and download the file from CDN
            nError = DownloadFileFromCDN(hs, _T("data"), pCKeyEntry->EKey, NULL, szCachePath, _countof(szCachePath));
            if(nError == ERROR_SUCCESS)
            {
/*
                hf->pStream = FileStream_OpenFile(szCachePath, BASE_PROVIDER_FILE | STREAM_PROVIDER_FLAT);
                if(hf->pStream != NULL)
                {
                    hf->bLocalFileStream = true;
                    return ERROR_SUCCESS;
                }
*/
            }
        }

        return ERROR_FILE_OFFLINE;
    }
}

#ifdef _DEBUG
static unsigned int table_16C57A8[0x10] =
{
    0x049396B8, 0x72A82A9B, 0xEE626CCA, 0x9917754F,
    0x15DE40B1, 0xF5A8A9B6, 0x421EAC7E, 0xA9D55C9A,
    0x317FD40C, 0x04FAF80D, 0x3D6BE971, 0x52933CFD,
    0x27F64B7D, 0xC6F5C11B, 0xD5757E3A, 0x6C388745
};

// Obtained from Agent.exe v 2.15.0.6296 (d14ec9d9a1b396a42964b05f40ea55f37eae5478d550c07ebb6cb09e50968d62)
// Note the "Checksum" value probably won't match with older game versions.
static void VerifyHeaderSpan(PBLTE_ENCODED_HEADER pBlteHeader, ULONGLONG HeaderOffset)
{
    LPBYTE pbBlteHeader = (LPBYTE)pBlteHeader;
    DWORD dwInt32;
    BYTE EncodedOffset[4] = { 0 };
    BYTE HashedHeader[4] = { 0 };
    BYTE JenkinsHash[4];
    BYTE Checksum[4];
    size_t i, j;

    // Seems to be hardcoded to zero
    assert(pBlteHeader->field_15 == 0);

    // Calculate the Jenkins hash and write it to the header
    dwInt32 = hashlittle(pbBlteHeader, FIELD_OFFSET(BLTE_ENCODED_HEADER, JenkinsHash), 0x3D6BE971);
    ConvertIntegerToBytes_4_LE(dwInt32, JenkinsHash);
//  assert(memcmp(pBlteHeader->JenkinsHash, JenkinsHash, sizeof(JenkinsHash)) == 0);

    // Encode the lower 32-bits of the offset
    dwInt32 = (DWORD)(HeaderOffset + FIELD_OFFSET(BLTE_ENCODED_HEADER, Signature));
    dwInt32 = table_16C57A8[dwInt32 & 0x0F] ^ dwInt32;
    ConvertIntegerToBytes_4_LE(dwInt32, EncodedOffset);

    // Calculate checksum of the so-far filled structure
    for (i = 0; i < FIELD_OFFSET(BLTE_ENCODED_HEADER, Checksum); i++)
        HashedHeader[i & 3] ^= pbBlteHeader[i];

    // XOR the two values together to get the final checksum.
    for (j = 0; j < 4; j++, i++)
        Checksum[j] = HashedHeader[i & 3] ^ EncodedOffset[i & 3];
//  assert(memcmp(pBlteHeader->Checksum, Checksum, sizeof(Checksum)) == 0);
}
#endif

static DWORD ParseBlteHeader(PCASC_FILE_SPAN pFileSpan, PCASC_CKEY_ENTRY pCKeyEntry, ULONGLONG HeaderOffset, LPBYTE pbEncodedBuffer, size_t cbEncodedBuffer, size_t * pcbHeaderSize)
{
    PBLTE_ENCODED_HEADER pEncodedHeader = (PBLTE_ENCODED_HEADER)pbEncodedBuffer;
    PBLTE_HEADER pBlteHeader = (PBLTE_HEADER)pbEncodedBuffer;
    DWORD ExpectedHeaderSize;
    DWORD ExHeaderSize = 0;
    DWORD HeaderSize;
    DWORD FrameCount = 0;

    CASCLIB_UNUSED(HeaderOffset);

    // On files within storage segments ("data.###"), there is BLTE_ENCODED_HEADER
    // On local files, there is just PBLTE_HEADER
    if(ConvertBytesToInteger_4_LE(pBlteHeader->Signature) != BLTE_HEADER_SIGNATURE)
    {
        // There must be at least some bytes
        if (cbEncodedBuffer < FIELD_OFFSET(BLTE_ENCODED_HEADER, MustBe0F))
            return ERROR_BAD_FORMAT;
        if (pEncodedHeader->EncodedSize != pCKeyEntry->EncodedSize)
            return ERROR_BAD_FORMAT;

#ifdef _DEBUG
        // Not really needed, it's here just for explanation of what the values mean
        //assert(memcmp(pCKeyEntry->EKey, pEncodedHeader->EKey.Value, MD5_HASH_SIZE) == 0);
        VerifyHeaderSpan(pEncodedHeader, HeaderOffset);
#endif
        // Capture the EKey
        ExHeaderSize = FIELD_OFFSET(BLTE_ENCODED_HEADER, Signature);
        pBlteHeader = (PBLTE_HEADER)(pbEncodedBuffer + ExHeaderSize);
    }

    // Verify the signature
    if(ConvertBytesToInteger_4_LE(pBlteHeader->Signature) != BLTE_HEADER_SIGNATURE)
        return ERROR_BAD_FORMAT;

    // Capture the header size. If this is non-zero, then array
    // of chunk headers follow. Otherwise, the file is just one chunk
    HeaderSize = ConvertBytesToInteger_4(pBlteHeader->HeaderSize);
    if (HeaderSize != 0)
    {
        if (pBlteHeader->MustBe0F != 0x0F)
            return ERROR_BAD_FORMAT;
        
        // Verify the header size
        FrameCount = ConvertBytesToInteger_3(pBlteHeader->FrameCount);
        ExpectedHeaderSize = 0x0C + FrameCount * sizeof(BLTE_FRAME);
        if (ExpectedHeaderSize != HeaderSize)
            return ERROR_BAD_FORMAT;

        // Give the values
        pcbHeaderSize[0] = ExHeaderSize + FIELD_OFFSET(BLTE_HEADER, MustBe0F) + sizeof(DWORD);
    }
    else
    {
        pcbHeaderSize[0] = ExHeaderSize + FIELD_OFFSET(BLTE_HEADER, MustBe0F);
    }

    // Give the frame count
    pFileSpan->FrameCount = FrameCount;
    return ERROR_SUCCESS;
}

static LPBYTE ReadMissingHeaderData(PCASC_FILE_SPAN pFileSpan, ULONGLONG DataFileOffset, LPBYTE pbEncodedBuffer, size_t cbEncodedBuffer, size_t cbTotalHeaderSize)
{
    LPBYTE pbNewBuffer;

    // Reallocate the buffer
    pbNewBuffer = CASC_REALLOC(BYTE, pbEncodedBuffer, cbTotalHeaderSize);
    if (pbNewBuffer != NULL)
    {
        // Load the missing data
        DataFileOffset += cbEncodedBuffer;
        if (FileStream_Read(pFileSpan->pStream, &DataFileOffset, pbNewBuffer + cbEncodedBuffer, (DWORD)(cbTotalHeaderSize - cbEncodedBuffer)))
        {
            return pbNewBuffer;
        }
    }

    // If anything failed, we free the original buffer and return NULL;
    CASC_FREE(pbEncodedBuffer);
    return NULL;
}

static LPBYTE CaptureBlteFileFrame(CASC_FILE_FRAME & Frame, LPBYTE pbFramePtr, LPBYTE pbFrameEnd)
{
    PBLTE_FRAME pFileFrame = (PBLTE_FRAME)pbFramePtr;

    // Check whether we have enough data ready
    if((pbFramePtr + sizeof(BLTE_FRAME)) > pbFrameEnd)
        return NULL;

    Frame.FrameHash   = pFileFrame->FrameHash;
    Frame.ContentSize = ConvertBytesToInteger_4(pFileFrame->ContentSize);
    Frame.EncodedSize = ConvertBytesToInteger_4(pFileFrame->EncodedSize);
    return pbFramePtr + sizeof(BLTE_FRAME);
}

static DWORD LoadSpanFrames(PCASC_FILE_SPAN pFileSpan, PCASC_CKEY_ENTRY pCKeyEntry, DWORD DataFileOffset, LPBYTE pbFramePtr, LPBYTE pbFrameEnd)
{
    PCASC_FILE_FRAME pFrames = NULL;
    DWORD ContentSize = 0;

    assert(pFileSpan != NULL);
    assert(pFileSpan->pStream != NULL);
    assert(pFileSpan->pFrames == NULL);

    if (pFileSpan->FrameCount != 0)
    {
        // Move the raw archive offset
        DataFileOffset += (pFileSpan->FrameCount * sizeof(BLTE_FRAME));

        // Allocate array of file frames
        pFrames = CASC_ALLOC<CASC_FILE_FRAME>(pFileSpan->FrameCount);
        if (pFrames != NULL)
        {
            // Copy the frames to the file structure
            for (DWORD i = 0; i < pFileSpan->FrameCount; i++)
            {
                CASC_FILE_FRAME & Frame = pFrames[i];

                // Capture the single BLTE frame
                pbFramePtr = CaptureBlteFileFrame(Frame, pbFramePtr, pbFrameEnd);
                if(pbFramePtr == NULL)
                    return ERROR_BAD_FORMAT;

                // Fill-in the file range of the frame
                Frame.StartOffset = pFileSpan->StartOffset + ContentSize;
                Frame.EndOffset = Frame.StartOffset + Frame.ContentSize;
                ContentSize += Frame.ContentSize;

                // Fill-in the archive range of the frame
                assert((DataFileOffset + Frame.EncodedSize) > DataFileOffset);
                Frame.DataFileOffset = DataFileOffset;
                DataFileOffset += Frame.EncodedSize;
            }

            // Save the content size of the file
            if(pCKeyEntry->ContentSize == CASC_INVALID_SIZE)
            {
                pCKeyEntry->ContentSize = ContentSize;
            }
        }
    }
    else
    {
        // Allocate single "dummy" frame
        pFrames = CASC_ALLOC<CASC_FILE_FRAME>(1);
        if (pFrames != NULL)
        {
            // Fill the single frame
            memset(&pFrames->FrameHash, 0, sizeof(CONTENT_KEY));
            pFrames->StartOffset = pFileSpan->StartOffset;
            pFrames->EndOffset = pFileSpan->EndOffset;
            pFrames->DataFileOffset = DataFileOffset;
            pFrames->EncodedSize = (DWORD)(pbFrameEnd - pbFramePtr);
            pFrames->ContentSize = pCKeyEntry->ContentSize;

            // Save the number of file frames
            pFileSpan->FrameCount = 1;
        }
    }

    // If we didn't load any frames, return error
    return ((pFileSpan->pFrames = pFrames) != NULL) ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

static DWORD LoadEncodedHeaderAndSpanFrames(PCASC_FILE_SPAN pFileSpan, PCASC_CKEY_ENTRY pCKeyEntry)
{
    LPBYTE pbEncodedBuffer;
    size_t cbEncodedBuffer = MAX_ENCODED_HEADER;
    DWORD dwErrCode = ERROR_SUCCESS;

    // Should only be called when the file frames are NOT loaded
    assert(pFileSpan->pFrames == NULL);
    assert(pFileSpan->FrameCount == 0);

    // Allocate the initial buffer for the encoded headers
    pbEncodedBuffer = CASC_ALLOC<BYTE>(MAX_ENCODED_HEADER);
    if (pbEncodedBuffer != NULL)
    {
        ULONGLONG ReadOffset = pFileSpan->ArchiveOffs;
        size_t cbTotalHeaderSize;
        size_t cbHeaderSize = 0;

        // At this point, we expect encoded size to be known
        assert(pCKeyEntry->EncodedSize != CASC_INVALID_SIZE);

        // Do not read more than encoded size
        cbEncodedBuffer = CASCLIB_MIN(cbEncodedBuffer, pCKeyEntry->EncodedSize);

        // Load the entire (eventual) header area. This is faster than doing
        // two read operations in a row. Read as much as possible. If the file is cut,
        // the FileStream will pad it with zeros
        if (FileStream_Read(pFileSpan->pStream, &ReadOffset, pbEncodedBuffer, (DWORD)cbEncodedBuffer))
        {
            // Parse the BLTE header
            dwErrCode = ParseBlteHeader(pFileSpan, pCKeyEntry, ReadOffset, pbEncodedBuffer, cbEncodedBuffer, &cbHeaderSize);
            if (dwErrCode == ERROR_SUCCESS)
            {
                // If the headers are larger than the initial read size, we read the missing data
                cbTotalHeaderSize = cbHeaderSize + (pFileSpan->FrameCount * sizeof(BLTE_FRAME));
                if (cbTotalHeaderSize > cbEncodedBuffer)
                {
                    pbEncodedBuffer = ReadMissingHeaderData(pFileSpan, ReadOffset, pbEncodedBuffer, cbEncodedBuffer, cbTotalHeaderSize);
                    if (pbEncodedBuffer == NULL)
                        dwErrCode = GetLastError();
                    cbEncodedBuffer = cbTotalHeaderSize;
                }

                // Load the array of frame headers
                if (dwErrCode == ERROR_SUCCESS)
                {
                    assert((DWORD)(ReadOffset + cbHeaderSize) > (DWORD)ReadOffset);
                    dwErrCode = LoadSpanFrames(pFileSpan, pCKeyEntry, (DWORD)(ReadOffset + cbHeaderSize), pbEncodedBuffer + cbHeaderSize, pbEncodedBuffer + cbEncodedBuffer);
                }
            }
        }
        else
        {
            dwErrCode = ERROR_FILE_CORRUPT;
        }

        // Free the frame buffer
        CASC_FREE(pbEncodedBuffer);
    }
    else
    {
        dwErrCode = ERROR_NOT_ENOUGH_MEMORY;
    }

    return dwErrCode;
}

static DWORD LoadFileFrames(TCascFile * hf, PCASC_FILE_SPAN pFileSpan, PCASC_CKEY_ENTRY pCKeyEntry)
{
    DWORD dwErrCode = ERROR_SUCCESS;

    // Sanity check
    assert(pFileSpan->pFrames == NULL);

    // Make sure that the data stream is open for that span
    if(pFileSpan->pStream == NULL)
    {
        dwErrCode = OpenDataStream(hf->hs, pFileSpan, pCKeyEntry, hf->bDownloadFileIf);
        if(dwErrCode != ERROR_SUCCESS)
            return dwErrCode;
    }

    // Make sure we have header area loaded
    return LoadEncodedHeaderAndSpanFrames(pFileSpan, pCKeyEntry);
}

static int LoadEncodedFrame(TFileStream * pStream, PCASC_FILE_FRAME pFrame, LPBYTE pbEncodedFrame, bool bVerifyIntegrity)
{
    ULONGLONG FileOffset = pFrame->DataFileOffset;
    int nError = ERROR_SUCCESS;

    // Load the encoded frame to memory
    if(FileStream_Read(pStream, &FileOffset, pbEncodedFrame, pFrame->EncodedSize))
    {
        if (bVerifyIntegrity)
        {
            if (!CascVerifyDataBlockHash(pbEncodedFrame, pFrame->EncodedSize, pFrame->FrameHash.Value))
                nError = ERROR_FILE_CORRUPT;
        }
    }
    else
    {
        nError = GetLastError();
    }

    return nError;
}

static int ProcessFileFrame(
    TCascStorage * hs,
    LPBYTE pbOutBuffer,
    DWORD  cbOutBuffer,
    LPBYTE pbInBuffer,
    DWORD cbInBuffer,
    DWORD dwFrameIndex)
{
    LPBYTE pbWorkBuffer = NULL;
    DWORD cbOutBufferExpected = 0;
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
                pbWorkBuffer = CASC_ALLOC<BYTE>(cbInBuffer - 1);
                cbWorkBuffer = cbInBuffer - 1;
                if(pbWorkBuffer == NULL)
                    return ERROR_NOT_ENOUGH_MEMORY;

                // Decrypt the stream to the work buffer
                nError = CascDecrypt(hs, pbWorkBuffer, &cbWorkBuffer, pbInBuffer + 1, cbInBuffer - 1, dwFrameIndex);
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
                cbOutBufferExpected = cbOutBuffer;
                nError = CascDecompress(pbOutBuffer, &cbOutBuffer, pbInBuffer + 1, cbInBuffer - 1);

                // We exactly know what the output buffer size will be.
                // If the uncompressed data is smaller, fill the rest with zeros
                if(cbOutBuffer < cbOutBufferExpected)
                    memset(pbOutBuffer + cbOutBuffer, 0, (cbOutBufferExpected - cbOutBuffer));
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
    CASC_FREE(pbWorkBuffer);
    return nError;
}

static bool GetFileFullInfo(TCascFile * hf, void * pvFileInfo, size_t cbFileInfo, size_t * pcbLengthNeeded)
{
    PCASC_FILE_FULL_INFO pFileInfo;
    PCASC_CKEY_ENTRY pCKeyEntry = hf->pCKeyEntry;
    TCascStorage * hs = hf->hs;

    // Verify whether we have enough space in the buffer
    pFileInfo = (PCASC_FILE_FULL_INFO)ProbeOutputBuffer(pvFileInfo, cbFileInfo, sizeof(CASC_FILE_FULL_INFO), pcbLengthNeeded);
    if(pFileInfo != NULL)
    {
        // Reset the entire structure
        CopyMemory16(pFileInfo->CKey, pCKeyEntry->CKey);
        CopyMemory16(pFileInfo->EKey, pCKeyEntry->EKey);
        pFileInfo->FileDataId = CASC_INVALID_ID;
        pFileInfo->LocaleFlags = CASC_INVALID_ID;
        pFileInfo->ContentFlags = CASC_INVALID_ID;

        // Supply information not depending on root
        CascStrPrintf(pFileInfo->DataFileName, _countof(pFileInfo->DataFileName), "data.%03u", hf->pSpans->ArchiveIndex);
        pFileInfo->StorageOffset = pCKeyEntry->StorageOffset;
        pFileInfo->SegmentOffset = hf->pSpans->ArchiveOffs;
        pFileInfo->FileNameHash = 0;
        pFileInfo->TagBitMask = pCKeyEntry->TagBitMask;
        pFileInfo->ContentSize = hf->ContentSize;
        pFileInfo->EncodedSize = hf->EncodedSize;
        pFileInfo->SegmentIndex = hf->pSpans->ArchiveIndex;
        pFileInfo->SpanCount = hf->SpanCount;

        // Supply the root-specific information
        hs->pRootHandler->GetInfo(pCKeyEntry, pFileInfo);
    }

    return (pFileInfo != NULL);
}

static DWORD ReadDataFromSpan(TCascFile * hf, PCASC_FILE_SPAN pFileSpan, PCASC_CKEY_ENTRY pCKeyEntry, ULONGLONG FilePointer, LPBYTE pbBuffer, DWORD dwBytesToRead)
{
    DWORD dwBytesRead = 0;
    DWORD dwErrCode;

    // If the file span does not contain loaded frames, do it now
    if(pFileSpan->pFrames == NULL)
    {
        dwErrCode = LoadFileFrames(hf, pFileSpan, pCKeyEntry);
        if(dwErrCode != ERROR_SUCCESS)
        {
            SetLastError(dwErrCode);
            return 0;
        }
    }

    // Parse all file frames and read data from them
    for(DWORD i = 0; i < pFileSpan->FrameCount && dwBytesToRead != 0; i++)
    {
        PCASC_FILE_FRAME pFileFrame = pFileSpan->pFrames + i;
        LPBYTE pbEncodedFrame;
        LPBYTE pbDecodedFrame;
        bool bKeepDecodedFrame = false;

        if(pFileFrame->StartOffset <= FilePointer && FilePointer < pFileFrame->EndOffset)
        {
            // Allocate buffers for both
            pbEncodedFrame = CASC_ALLOC<BYTE>(pFileFrame->EncodedSize);
            pbDecodedFrame = CASC_ALLOC<BYTE>(pFileFrame->ContentSize);

            // Load the frame and decode it
            if(pbEncodedFrame && pbDecodedFrame)
            {
                // Load the encoded frame data
                dwErrCode = LoadEncodedFrame(pFileSpan->pStream, pFileFrame, pbEncodedFrame, hf->bVerifyIntegrity);
                if (dwErrCode == ERROR_SUCCESS)
                {
                    // Decode the frame
                    dwErrCode = ProcessFileFrame(hf->hs,
                                                 pbDecodedFrame,
                                                 pFileFrame->ContentSize,
                                                 pbEncodedFrame,
                                                 pFileFrame->EncodedSize,
                                         (DWORD)(pFileFrame - pFileSpan->pFrames));

                    // Some people find it handy to extract data from partially encrypted file,
                    // even at the cost producing files that are corrupt.
                    // We overcome missing decryption key by zeroing the encrypted portions
                    if(dwErrCode == ERROR_FILE_ENCRYPTED && hf->bOvercomeEncrypted)
                    {
                        memset(pbDecodedFrame, 0, pFileFrame->ContentSize);
                        dwErrCode = ERROR_SUCCESS;
                    }

                    // Copy the frame to the result buffer
                    if (dwErrCode == ERROR_SUCCESS)
                    {
                        DWORD dwStartOffset = (DWORD)(pFileFrame->StartOffset - FilePointer);
                        DWORD dwBytesInFrame = pFileFrame->ContentSize - dwStartOffset;

                        // If there is more bytes than we need, keep the loaded buffer for later use;
                        // There is high chance that the reading will continue from the same position
                        // where this reading ended.
                        if(dwBytesToRead > dwBytesInFrame)
                        {
                            memcpy(pbBuffer, pbDecodedFrame + dwStartOffset, dwBytesInFrame);
                            FilePointer = FilePointer + dwBytesInFrame;
                            dwBytesToRead -= dwBytesInFrame;
                            dwBytesRead += dwBytesInFrame;
                            pbBuffer += dwBytesInFrame;
                        }
                        else
                        {
                            memcpy(pbBuffer, pbDecodedFrame + dwStartOffset, dwBytesToRead);
                            bKeepDecodedFrame = (dwBytesToRead < dwBytesInFrame);
                            dwBytesRead += dwBytesToRead;
                        }
                    }
                }

                // If we shall keep the decoded frame, do it. Most probably,
                // we will utilize the already-decompressed (decrypted) on next read
                if(bKeepDecodedFrame)
                {
                    // Free the previous file cache, if any
                    CASC_FREE(hf->pbFileCache);

                    // Setup the new file cache
                    hf->FileCacheStart = pFileFrame->StartOffset;
                    hf->FileCacheEnd = pFileFrame->EndOffset;
                    hf->pbFileCache = pbDecodedFrame;

                    // Prevent the buffer from freeing
                    pbDecodedFrame = NULL;
                }
            }

            // Free both buffers
            CASC_FREE(pbDecodedFrame);
            CASC_FREE(pbEncodedFrame);
        }
    }

    return dwBytesRead;
}

// Reads the file data from cache. Returns the number of bytes read
static DWORD ReadFile_Cache(TCascFile * hf, LPBYTE pbBuffer, ULONGLONG StartOffset, ULONGLONG EndOffset)
{
    DWORD dwBytesToCopy;

    // Is there a file cache at all?
    if(hf->pbFileCache != NULL && hf->FileCacheStart <= StartOffset && StartOffset < hf->FileCacheEnd)
    {
        LPBYTE pbStartBlock = hf->pbFileCache + (size_t)(StartOffset - hf->FileCacheStart);

        // Can we handle the entire request from the cache?
        if(EndOffset <= hf->FileCacheEnd)
        {
            DWORD dwBytesToCopy = (DWORD)(EndOffset - StartOffset);

            memcpy(pbBuffer, pbStartBlock, dwBytesToCopy);
            return dwBytesToCopy;
        }

        // We copy as much bytes as available. The rest is handled by normal read
        else
        {
            DWORD dwBytesToCopy = (DWORD)(hf->FileCacheEnd - StartOffset);

            memcpy(pbBuffer, pbStartBlock, dwBytesToCopy);
            return dwBytesToCopy;
        }
    }

    // Can't handle the request from the cache
    return 0;
}

// No cache at all. The entire file will be read directly to the user buffer
static DWORD ReadFile_NoCache(TCascFile * hf, LPBYTE pbBuffer, ULONGLONG StartOffset, ULONGLONG EndOffset)
{
    PCASC_FILE_SPAN pFileSpan;

    // Reading the whole file?
    if(StartOffset == 0 && EndOffset == hf->ContentSize)
    {
        for(size_t SpanIndex = 0; SpanIndex < hf->SpanCount; SpanIndex++)
        {
            for(size_t FrameIndex = 0; FrameIndex < hf->pSpansSpanIndex;


        }
    }

    // Reading just a part of the file?
    else
    {

    }
}

//-----------------------------------------------------------------------------
// Public functions

bool WINAPI CascGetFileInfo(HANDLE hFile, CASC_FILE_INFO_CLASS InfoClass, void * pvFileInfo, size_t cbFileInfo, size_t * pcbLengthNeeded)
{
    TCascFile * hf;
    LPBYTE pbOutputValue = NULL;
    LPBYTE pbInfoValue = NULL;
    size_t cbInfoValue = 0;

    // Validate the file handle
    if((hf = TCascFile::IsValid(hFile)) == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Differentiate between info classes
    switch(InfoClass)
    {
        case CascFileContentKey:

            // Do we have content key at all?
            if(hf->pCKeyEntry == NULL || (hf->pCKeyEntry->Flags & CASC_CE_HAS_CKEY) == 0)
            {
                SetLastError(ERROR_NOT_SUPPORTED);
                return false;
            }

            // Give the content key
            pbInfoValue = hf->pCKeyEntry->CKey;
            cbInfoValue = CASC_CKEY_SIZE;
            break;

        case CascFileEncodedKey:

            // Do we have content key at all?
            if(hf->pCKeyEntry == NULL || (hf->pCKeyEntry->Flags & CASC_CE_HAS_EKEY) == 0)
            {
                SetLastError(ERROR_NOT_SUPPORTED);
                return false;
            }

            // Give the content key
            pbInfoValue = hf->pCKeyEntry->EKey;
            cbInfoValue = CASC_CKEY_SIZE;
            break;

        case CascFileFullInfo:
            return GetFileFullInfo(hf, pvFileInfo, cbFileInfo, pcbLengthNeeded);

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
    }

    // Sanity check
    assert(pbInfoValue != NULL);
    assert(cbInfoValue != 0);

    // Give the result
    pbOutputValue = (LPBYTE)ProbeOutputBuffer(pvFileInfo, cbFileInfo, cbInfoValue, pcbLengthNeeded);
    if(pbOutputValue != NULL)
        memcpy(pbOutputValue, pbInfoValue, cbInfoValue);
    return (pbOutputValue != NULL);
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

bool WINAPI CascGetFileSize64(HANDLE hFile, PULONGLONG PtrFileSize)
{
    TCascFile * hf;

    // Validate the file handle
    if((hf = TCascFile::IsValid(hFile)) == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // Validate the file pointer
    if(PtrFileSize == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // The content size must be known at this point
    if(hf->ContentSize == CASC_INVALID_SIZE)
    {
        SetLastError(ERROR_CAN_NOT_COMPLETE);
        assert(false);
        return false;
    }

    // Give the file size to the caller
    PtrFileSize[0] = hf->ContentSize;
    return true;
}

DWORD WINAPI CascGetFileSize(HANDLE hFile, PDWORD PtrFileSizeHigh)
{
    ULONGLONG FileSize = 0;

    // Retrieve the 64-bit file size
    if(!CascGetFileSize64(hFile, &FileSize))
        return CASC_INVALID_SIZE;

    // Give the file size to the caller
    if(PtrFileSizeHigh != NULL)
        PtrFileSizeHigh[0] = (DWORD)(FileSize >> 32);
    return (DWORD)(FileSize);
}

bool WINAPI CascSetFilePointer64(HANDLE hFile, LONGLONG DistanceToMove, PULONGLONG PtrNewPos, DWORD dwMoveMethod)
{
    ULONGLONG FilePosition;
    TCascFile * hf;

    // If the hFile is not a valid file handle, return an error.
    hf = TCascFile::IsValid(hFile);
    if(hf == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
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
            return false;
    }

    // Now calculate the new file pointer
    if(DistanceToMove >= 0)
    {
        // Do not allow the file pointer to overflow 64-bit range
        if((FilePosition + DistanceToMove) < FilePosition)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }

        // Do not allow the file pointer to overflow the file size
        if((FilePosition = FilePosition + DistanceToMove) > hf->ContentSize)
            FilePosition = hf->ContentSize;
        hf->FilePointer = FilePosition;
    }
    else
    {
        // Do not allow the file pointer to underflow 64-bit range
        if((FilePosition + DistanceToMove) > FilePosition)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }

        // Do not allow the file pointer to move to negative values
        if((FilePosition = FilePosition + DistanceToMove) < 0)
            FilePosition = 0;
        hf->FilePointer = FilePosition;
    }

    // Give the result size to the caller
    if(PtrNewPos != NULL)
        PtrNewPos[0] = hf->FilePointer;
    return true;
}

DWORD WINAPI CascSetFilePointer(HANDLE hFile, LONG lFilePos, LONG * PtrFilePosHigh, DWORD dwMoveMethod)
{
    ULONGLONG NewPos = 0;
    LONGLONG DistanceToMove;
    
    // Assemble the 64-bit distance to move
    DistanceToMove = (PtrFilePosHigh != NULL) ? MAKE_OFFSET64(PtrFilePosHigh[0], lFilePos) : (LONGLONG)(LONG)lFilePos;

    // Set the file offset
    if(!CascSetFilePointer64(hFile, DistanceToMove, &NewPos, dwMoveMethod))
        return CASC_INVALID_POS;

    // Give the result to the caller
    if(PtrFilePosHigh != NULL)
        PtrFilePosHigh[0] = (LONG)(NewPos >> 32);
    return (DWORD)(NewPos);
}

bool WINAPI CascReadFile(HANDLE hFile, void * pvBuffer, DWORD dwBytesToRead, PDWORD PtrBytesRead)
{
    TCascFile * hf;
    DWORD dwBytesRead;
    bool bResult = true;

    // The buffer must be valid
    if(pvBuffer == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Validate the file handle
    if((hf = TCascFile::IsValid(hFile)) == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    // If the file position is at or beyond end of file, do nothing
//  if(dwErrCode == ERROR_SUCCESS)
    {
        ULONGLONG StartOffset = hf->FilePointer;
        ULONGLONG EndOffset;
        LPBYTE pbBuffer = (LPBYTE)pvBuffer;
        DWORD dwBytesRead;

        // Check the starting position
        if(StartOffset >= hf->ContentSize)
        {
            PtrBytesRead[0] = 0;
            return true;
        }

        // Check the ending position
        EndOffset = StartOffset + dwBytesToRead;
        if(EndOffset > hf->ContentSize)
        {
            EndOffset = hf->ContentSize;
            dwBytesToRead = (DWORD)(hf->ContentSize - StartOffset);
        }

        // Can we handle the request (at least partially) from the cache?
        if((dwBytesRead = ReadFile_Cache(hf, pbBuffer, StartOffset, EndOffset)) != 0)
        {
            // Has the read request been fully satisfied?
            if(dwBytesRead == dwBytesToRead)
            {
                if(PtrBytesRead != NULL)
                    PtrBytesRead[0] = dwBytesToRead;
                return true;
            }

            // Move pointers
            StartOffset = StartOffset + dwBytesRead;
            dwBytesToRead -= dwBytesRead;
            pbBuffer += dwBytesRead;
        }

        // Perform the cache-strategy-specific read
        switch(hf->CacheStrategy)
        {
            case CascCacheNothing:              // No cache at all. The entire file will be read directly to the user buffer
                dwBytesRead = ReadFile_NoCache(hf, pbBuffer, StartOffset, EndOffset);
                break;
        }


/*
        // Find the file span to read from
        for(DWORD i = 0; i < hf->SpanCount && dwBytesToRead != 0; i++)
        {
            if(hf->pSpans[i].StartOffset <= StartOffset && StartOffset < hf->pSpans[i].EndOffset)
            {
                // Read the data from the file span
                dwBytesRead = ReadDataFromSpan(hf, hf->pSpans + i, hf->pCKeyEntry + i, StartOffset, pbBuffer, dwBytesToRead);
                if(dwBytesRead == 0)
                {
                    bResult = false;
                    break;
                }

                // Move pointers
                dwBytesToRead = dwBytesToRead - dwBytesRead;
                StartOffset = StartOffset + dwBytesRead;
                pbBuffer = pbBuffer + dwBytesRead;
            }
        }

        // Update the file pointer
        hf->FilePointer = StartOffset;
    }
*/
    return bResult;

/*
    // Allocate cache buffer for the entire file. This is the fastest approach
    // (without reallocations). However, this may consume quite a lot of memory
    // (Storage: "2016 - Starcraft II/45364", file: "3d815f40c0413701aa2bd214070d0062"
    // needs 0x239a09b3 bytes of memory (~600 MB)
    if(nError == ERROR_SUCCESS)
    {
        if(hf->pbFileCache == NULL)
        {
            // Allocate buffer
            hf->pbFileCache = CASC_ALLOC<BYTE>(hf->ContentSize);
            hf->cbFileCache = hf->ContentSize;
            if(hf->pbFileCache == NULL)
                nError = ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    // Load all frames that are not loaded yet
    if(nError == ERROR_SUCCESS)
    {
        PCASC_FILE_FRAME pFrame = hf->pFrames;
        DWORD StartFrameOffset = 0;
        DWORD StartReadOffset = hf->FilePointer;
        DWORD EndReadOffset = hf->FilePointer + dwBytesToRead;

        for(DWORD i = 0; (i < pFileSpan->FrameCount) && (nError == ERROR_SUCCESS); i++, pFrame++)
        {
            LPBYTE pbDecodedFrame = hf->pbFileCache + StartFrameOffset;
            LPBYTE pbEncodedFrame;
            DWORD EndFrameOffset = StartFrameOffset + pFrame->ContentSize;

            // Does that frame belong to the range?
            if(StartReadOffset < EndFrameOffset && EndReadOffset > StartFrameOffset)
            {
                // Is the frame already loaded?
                if (pFrame->FileOffset == CASC_INVALID_POS)
                {
                    // Allocate space for the encoded frame
                    pbEncodedFrame = CASC_ALLOC<BYTE>(pFrame->EncodedSize);
                    if (pbEncodedFrame != NULL)
                    {
                        // Load the encoded frame data
                        nError = LoadEncodedFrame(hf->pStream, pFrame, pbEncodedFrame, hf->bVerifyIntegrity);
                        if (nError == ERROR_SUCCESS)
                        {
                            // Decode the frame
                            nError = ProcessFileFrame(hf->hs,
                                                      pbDecodedFrame,
                                                      pFrame->ContentSize,
                                                      pbEncodedFrame,
                                                      pFrame->EncodedSize,
                                              (DWORD)(pFrame - hf->pFrames));

                            // Some people find it handy to extract data from partially encrypted file,
                            // even at the cost producing files that are corrupt.
                            // We overcome missing decryption key by zeroing the encrypted portions
                            if(nError == ERROR_FILE_ENCRYPTED && hf->bOvercomeEncrypted)
                            {
                                memset(pbDecodedFrame, 0, pFrame->ContentSize);
                                nError = ERROR_SUCCESS;
                            }

                            if (nError == ERROR_SUCCESS)
                            {
                                // Mark the frame as loaded
                                pFrame->FileOffset = StartFrameOffset;
                            }
                        }

                        // Free the frame buffer
                        CASC_FREE(pbEncodedFrame);
                    }
                    else
                    {
                        nError = ERROR_NOT_ENOUGH_MEMORY;
                    }
                }
            }

            // If the frame start is past the read offset, stop the loop
            if ((StartFrameOffset + pFrame->ContentSize) >= EndReadOffset)
                break;
            StartFrameOffset += pFrame->ContentSize;
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
*/
}
