/*****************************************************************************/
/* CascDecrypt.cpp                        Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Decryption functions for CascLib                                          */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 31.10.15  1.00  Lad  The first version of CascDecrypt.cpp                 */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local structures

typedef struct _CASC_ENCRYPTION_KEY
{
    ULONGLONG KeyName;                  // "Name" of the key
    BYTE Key[0x10];                     // The key itself
} CASC_ENCRYPTION_KEY, *PCASC_ENCRYPTION_KEY;

typedef struct _CASC_SALSA20
{
    DWORD Key[0x10];
    DWORD dwRounds;

} CASC_SALSA20, *PCASC_SALSA20;

//-----------------------------------------------------------------------------
// Known encryption keys. See https://wowdev.wiki/CASC for updates

static CASC_ENCRYPTION_KEY CascKeys[] =
{
//  Key Name                Encryption key                                                                                       Seen in
//  ----------------------  ------------------------------------------------------------------------------------------------     -----------
    {0xFB680CB6A8BF81F3ULL, {0x62, 0xD9, 0x0E, 0xFA, 0x7F, 0x36, 0xD7, 0x1C, 0x39, 0x8A, 0xE2, 0xF1, 0xFE, 0x37, 0xBD, 0xB9}},   // overwatch 0.8.0.24919_retailx64 (hardcoded)
    {0x402CD9D8D6BFED98ULL, {0xAE, 0xB0, 0xEA, 0xDE, 0xA4, 0x76, 0x12, 0xFE, 0x6C, 0x04, 0x1A, 0x03, 0x95, 0x8D, 0xF2, 0x41}},   // overwatch 0.8.0.24919_retailx64 (hardcoded)
    {0xDBD3371554F60306ULL, {0x34, 0xE3, 0x97, 0xAC, 0xE6, 0xDD, 0x30, 0xEE, 0xFD, 0xC9, 0x8A, 0x2A, 0xB0, 0x93, 0xCD, 0x3C}},   // overwatch 0.8.0.24919_retailx64 (streamed from server)
    {0x11A9203C9881710AULL, {0x2E, 0x2C, 0xB8, 0xC3, 0x97, 0xC2, 0xF2, 0x4E, 0xD0, 0xB5, 0xE4, 0x52, 0xF1, 0x8D, 0xC2, 0x67}},   // overwatch 0.8.0.24919_retailx64 (streamed from server)
    {0xA19C4F859F6EFA54ULL, {0x01, 0x96, 0xCB, 0x6F, 0x5E, 0xCB, 0xAD, 0x7C, 0xB5, 0x28, 0x38, 0x91, 0xB9, 0x71, 0x2B, 0x4B}},   // overwatch 0.8.0.24919_retailx64 (streamed from server)
    {0x87AEBBC9C4E6B601ULL, {0x68, 0x5E, 0x86, 0xC6, 0x06, 0x3D, 0xFD, 0xA6, 0xC9, 0xE8, 0x52, 0x98, 0x07, 0x6B, 0x3D, 0x42}},   // overwatch 0.8.0.24919_retailx64 (streamed from server)
    {0xDEE3A0521EFF6F03ULL, {0xAD, 0x74, 0x0C, 0xE3, 0xFF, 0xFF, 0x92, 0x31, 0x46, 0x81, 0x26, 0x98, 0x57, 0x08, 0xE1, 0xB9}},   // overwatch 0.8.0.24919_retailx64 (streamed from server)
    {0x8C9106108AA84F07ULL, {0x53, 0xD8, 0x59, 0xDD, 0xA2, 0x63, 0x5A, 0x38, 0xDC, 0x32, 0xE7, 0x2B, 0x11, 0xB3, 0x2F, 0x29}},   // overwatch 0.8.0.24919_retailx64 (streamed from server)
    {0x49166D358A34D815ULL, {0x66, 0x78, 0x68, 0xCD, 0x94, 0xEA, 0x01, 0x35, 0xB9, 0xB1, 0x6C, 0x93, 0xB1, 0x12, 0x4A, 0xBA}},   // overwatch 0.8.0.24919_retailx64 (streamed from server)

    {0x1463A87356778D14ULL, {0x69, 0xBD, 0x2A, 0x78, 0xD0, 0x5C, 0x50, 0x3E, 0x93, 0x99, 0x49, 0x59, 0xB3, 0x0E, 0x5A, 0xEC}},   // overwatch (streamed from server) 
    {0x5E152DE44DFBEE01ULL, {0xE4, 0x5A, 0x17, 0x93, 0xB3, 0x7E, 0xE3, 0x1A, 0x8E, 0xB8, 0x5C, 0xEE, 0x0E, 0xEE, 0x1B, 0x68}},   // overwatch (streamed from server) 
    {0x9B1F39EE592CA415ULL, {0x54, 0xA9, 0x9F, 0x08, 0x1C, 0xAD, 0x0D, 0x08, 0xF7, 0xE3, 0x36, 0xF4, 0x36, 0x8E, 0x89, 0x4C}},   // overwatch (streamed from server)
    {0x24C8B75890AD5917ULL, {0x31, 0x10, 0x0C, 0x00, 0xFD, 0xE0, 0xCE, 0x18, 0xBB, 0xB3, 0x3F, 0x3A, 0xC1, 0x5B, 0x30, 0x9F}},   // overwatch (included in game)
    {0xEA658B75FDD4890FULL, {0xDE, 0xC7, 0xA4, 0xE7, 0x21, 0xF4, 0x25, 0xD1, 0x33, 0x03, 0x98, 0x95, 0xC3, 0x60, 0x36, 0xF8}},   // overwatch (included in game)
    {0x026FDCDF8C5C7105ULL, {0x8F, 0x41, 0x80, 0x9D, 0xA5, 0x53, 0x66, 0xAD, 0x41, 0x6D, 0x3C, 0x33, 0x74, 0x59, 0xEE, 0xE3}},   // overwatch (included in game)
    {0xCAE3FAC925F20402ULL, {0x98, 0xB7, 0x8E, 0x87, 0x74, 0xBF, 0x27, 0x50, 0x93, 0xCB, 0x1B, 0x5F, 0xC7, 0x14, 0x51, 0x1B}},   // overwatch (included in game)
    {0x3ECB6A12785050FAULL, {0xBD, 0xC5, 0x18, 0x62, 0xAB, 0xED, 0x79, 0xB2, 0xDE, 0x48, 0xC8, 0xE7, 0xE6, 0x6C, 0x62, 0x00}},   // WOW-20740patch7.0.1_Beta (db2 id: 16, lookup in db)
    {0x00,                  {0xAA, 0x0B, 0x5C, 0x77, 0xF0, 0x88, 0xCC, 0xC2, 0xD3, 0x90, 0x49, 0xBD, 0x26, 0x7F, 0x06, 0x6D}},   // WOW-20740patch7.0.1_Beta (db2 id: 25, lookup streamed from server)
    {0xD1E9B5EDF9283668ULL, {0x8E, 0x4A, 0x25, 0x79, 0x89, 0x4E, 0x38, 0xB4, 0xAB, 0x90, 0x58, 0xBA, 0x5C, 0x73, 0x28, 0xEE}},   // WOW-20740patch7.0.1_Beta (db2 id: 39, lookup streamed from server)
    {0xB76729641141CB34ULL, {0x98, 0x49, 0xD1, 0xAA, 0x7B, 0x1F, 0xD0, 0x98, 0x19, 0xC5, 0xC6, 0x62, 0x83, 0xA3, 0x26, 0xEC}},   // WOW-20740patch7.0.1_Beta (db2 id: 40, lookup streamed from server)
    {0x00,                  {0xD5, 0x14, 0xBD, 0x19, 0x09, 0xA9, 0xE5, 0xDC, 0x87, 0x03, 0xF4, 0xB8, 0xBB, 0x1D, 0xFD, 0x9A}},   // WOW-20740patch7.0.1_Beta (db2 id: 41, lookup streamed from server)
    {0x23C5B5DF837A226CULL, {0x14, 0x06, 0xE2, 0xD8, 0x73, 0xB6, 0xFC, 0x99, 0x21, 0x7A, 0x18, 0x08, 0x81, 0xDA, 0x8D, 0x62}},   // WOW-20740patch7.0.1_Beta (db2 id: 42, lookup streamed from server)

    {0, {0}}
};
                                                                                                                           
static const char * szKeyConstant16 = "expand 16-byte k";
static const char * szKeyConstant32 = "expand 32-byte k";

//-----------------------------------------------------------------------------
// Local functions

static DWORD Rol32(DWORD dwValue, DWORD dwRolCount)
{
    return (dwValue << dwRolCount) | (dwValue >> (32 - dwRolCount));
}

static LPBYTE FindCascKey(ULONGLONG KeyName)
{
    // Search the known keys
    for(size_t i = 0; CascKeys[i].KeyName != 0; i++)
    {
        if(CascKeys[i].KeyName == KeyName)
            return CascKeys[i].Key;
    }

    // Key not found
    return NULL;
}

static void Initialize(PCASC_SALSA20 pState, LPBYTE pbKey, DWORD cbKeyLength, LPBYTE pbVector)
{
    const char * szConstants = (cbKeyLength == 32) ? szKeyConstant32 : szKeyConstant16;
    DWORD KeyIndex = cbKeyLength - 0x10;

    memset(pState, 0, sizeof(CASC_SALSA20));
    pState->Key[0]  = *(PDWORD)(szConstants + 0x00);
    pState->Key[1]  = *(PDWORD)(pbKey + 0x00);
    pState->Key[2]  = *(PDWORD)(pbKey + 0x04);
    pState->Key[3]  = *(PDWORD)(pbKey + 0x08);
    pState->Key[4]  = *(PDWORD)(pbKey + 0x0C);
    pState->Key[5]  = *(PDWORD)(szConstants + 0x04);
    pState->Key[6]  = *(PDWORD)(pbVector + 0x00);
    pState->Key[7]  = *(PDWORD)(pbVector + 0x04);
    pState->Key[8]  = 0;
    pState->Key[9]  = 0;
    pState->Key[10] = *(PDWORD)(szConstants + 0x08);
    pState->Key[11] = *(PDWORD)(pbKey + KeyIndex + 0x00);
    pState->Key[12] = *(PDWORD)(pbKey + KeyIndex + 0x04);
    pState->Key[13] = *(PDWORD)(pbKey + KeyIndex + 0x08);
    pState->Key[14] = *(PDWORD)(pbKey + KeyIndex + 0x0C);
    pState->Key[15] = *(PDWORD)(szConstants + 0x0C);

    pState->dwRounds = 20;
}

static int Decrypt(PCASC_SALSA20 pState, LPBYTE pbOutBuffer, LPBYTE pbInBuffer, size_t cbInBuffer)
{
    LPBYTE pbXorValue;
    DWORD KeyMirror[0x10];
    DWORD XorValue[0x10];
    DWORD BlockSize;
    DWORD i;

    // Repeat until we have data to read
    while(cbInBuffer > 0)
    {
        // Create the copy of the key
        memcpy(KeyMirror, pState->Key, sizeof(KeyMirror));

        // Shuffle the key
        for(i = 0; i < pState->dwRounds; i += 2)
        {
            KeyMirror[0x04] ^= Rol32((KeyMirror[0x00] + KeyMirror[0x0C]), 0x07);
            KeyMirror[0x08] ^= Rol32((KeyMirror[0x04] + KeyMirror[0x00]), 0x09);
            KeyMirror[0x0C] ^= Rol32((KeyMirror[0x08] + KeyMirror[0x04]), 0x0D);
            KeyMirror[0x00] ^= Rol32((KeyMirror[0x0C] + KeyMirror[0x08]), 0x12);

            KeyMirror[0x09] ^= Rol32((KeyMirror[0x05] + KeyMirror[0x01]), 0x07);
            KeyMirror[0x0D] ^= Rol32((KeyMirror[0x09] + KeyMirror[0x05]), 0x09);
            KeyMirror[0x01] ^= Rol32((KeyMirror[0x0D] + KeyMirror[0x09]), 0x0D);
            KeyMirror[0x05] ^= Rol32((KeyMirror[0x01] + KeyMirror[0x0D]), 0x12);

            KeyMirror[0x0E] ^= Rol32((KeyMirror[0x0A] + KeyMirror[0x06]), 0x07);
            KeyMirror[0x02] ^= Rol32((KeyMirror[0x0E] + KeyMirror[0x0A]), 0x09);
            KeyMirror[0x06] ^= Rol32((KeyMirror[0x02] + KeyMirror[0x0E]), 0x0D);
            KeyMirror[0x0A] ^= Rol32((KeyMirror[0x06] + KeyMirror[0x02]), 0x12);

            KeyMirror[0x03] ^= Rol32((KeyMirror[0x0F] + KeyMirror[0x0B]), 0x07);
            KeyMirror[0x07] ^= Rol32((KeyMirror[0x03] + KeyMirror[0x0F]), 0x09);
            KeyMirror[0x0B] ^= Rol32((KeyMirror[0x07] + KeyMirror[0x03]), 0x0D);
            KeyMirror[0x0F] ^= Rol32((KeyMirror[0x0B] + KeyMirror[0x07]), 0x12);

            KeyMirror[0x01] ^= Rol32((KeyMirror[0x00] + KeyMirror[0x03]), 0x07);
            KeyMirror[0x02] ^= Rol32((KeyMirror[0x01] + KeyMirror[0x00]), 0x09);
            KeyMirror[0x03] ^= Rol32((KeyMirror[0x02] + KeyMirror[0x01]), 0x0D);
            KeyMirror[0x00] ^= Rol32((KeyMirror[0x03] + KeyMirror[0x02]), 0x12);

            KeyMirror[0x06] ^= Rol32((KeyMirror[0x05] + KeyMirror[0x04]), 0x07);
            KeyMirror[0x07] ^= Rol32((KeyMirror[0x06] + KeyMirror[0x05]), 0x09);
            KeyMirror[0x04] ^= Rol32((KeyMirror[0x07] + KeyMirror[0x06]), 0x0D);
            KeyMirror[0x05] ^= Rol32((KeyMirror[0x04] + KeyMirror[0x07]), 0x12);

            KeyMirror[0x0B] ^= Rol32((KeyMirror[0x0A] + KeyMirror[0x09]), 0x07);
            KeyMirror[0x08] ^= Rol32((KeyMirror[0x0B] + KeyMirror[0x0A]), 0x09);
            KeyMirror[0x09] ^= Rol32((KeyMirror[0x08] + KeyMirror[0x0B]), 0x0D);
            KeyMirror[0x0A] ^= Rol32((KeyMirror[0x09] + KeyMirror[0x08]), 0x12);

            KeyMirror[0x0C] ^= Rol32((KeyMirror[0x0F] + KeyMirror[0x0E]), 0x07);
            KeyMirror[0x0D] ^= Rol32((KeyMirror[0x0C] + KeyMirror[0x0F]), 0x09);
            KeyMirror[0x0E] ^= Rol32((KeyMirror[0x0D] + KeyMirror[0x0C]), 0x0D);
            KeyMirror[0x0F] ^= Rol32((KeyMirror[0x0E] + KeyMirror[0x0D]), 0x12);
        }

        // Set the number of remaining bytes
        pbXorValue = (LPBYTE)XorValue;
        BlockSize = (DWORD)CASCLIB_MIN(cbInBuffer, 0x40);

        // Prepare the XOR constants
        for(i = 0; i < 16; i++)
        {
            XorValue[i] = KeyMirror[i] + pState->Key[i];
        }

        // Decrypt the block
        for(i = 0; i < BlockSize; i++)
        {
            pbOutBuffer[i] = pbInBuffer[i] ^ pbXorValue[i];
        }

        pState->Key[8] = pState->Key[8] + 1;
        if(pState->Key[8] == 0)
            pState->Key[9] = pState->Key[9] + 1;

        // Adjust buffers
        pbOutBuffer += BlockSize;
        pbInBuffer += BlockSize;
        cbInBuffer -= BlockSize;
    }

    return ERROR_SUCCESS;
}

static int Decrypt_Salsa20(LPBYTE pbOutBuffer, LPBYTE pbInBuffer, size_t cbInBuffer, LPBYTE pbKey, DWORD cbKeySize, LPBYTE pbVector)
{
    CASC_SALSA20 SalsaState;

    Initialize(&SalsaState, pbKey, cbKeySize, pbVector);
    return Decrypt(&SalsaState, pbOutBuffer, pbInBuffer, cbInBuffer);
}

//-----------------------------------------------------------------------------
// Public functions

int CascDecrypt(LPBYTE pbOutBuffer, PDWORD pcbOutBuffer, LPBYTE pbInBuffer, DWORD cbInBuffer, DWORD dwFrameIndex)
{
    ULONGLONG KeyName = 0;
    LPBYTE pbBufferEnd = pbInBuffer + cbInBuffer;
    LPBYTE pbKey;
    DWORD KeyNameSize;
    DWORD dwShift = 0;
    DWORD IVSize;
    BYTE Vector[0x08];
    BYTE EncryptionType;
    int nError;

    // Verify and retrieve the key name size
    if(pbInBuffer >= pbBufferEnd)
        return ERROR_FILE_CORRUPT;
    if(pbInBuffer[0] != 0 && pbInBuffer[0] != 8)
        return ERROR_NOT_SUPPORTED;
    KeyNameSize = *pbInBuffer++;

    // Copy the key name
    if((pbInBuffer + KeyNameSize) >= pbBufferEnd)
        return ERROR_FILE_CORRUPT;
    memcpy(&KeyName, pbInBuffer, KeyNameSize);
    pbInBuffer += KeyNameSize;

    // Verify and retrieve the Vector size
    if(pbInBuffer >= pbBufferEnd)
        return ERROR_FILE_CORRUPT;
    if(pbInBuffer[0] != 4 && pbInBuffer[0] != 8)
        return ERROR_NOT_SUPPORTED;
    IVSize = *pbInBuffer++;

    // Copy the initialization vector
    if((pbInBuffer + IVSize) >= pbBufferEnd)
        return ERROR_FILE_CORRUPT;
    memset(Vector, 0, sizeof(Vector));
    memcpy(Vector, pbInBuffer, IVSize);
    pbInBuffer += IVSize;

    // Verify and retrieve the encryption type
    if(pbInBuffer >= pbBufferEnd)
        return ERROR_FILE_CORRUPT;
    if(pbInBuffer[0] != 'S' && pbInBuffer[0] != 'A')
        return ERROR_NOT_SUPPORTED;
    EncryptionType = *pbInBuffer++;

    // Do we have enough space in the output buffer?
    if((DWORD)(pbBufferEnd - pbInBuffer) > pcbOutBuffer[0])
        return ERROR_INSUFFICIENT_BUFFER;

    // Check if we know the key
    pbKey = FindCascKey(KeyName);
    if(pbKey == NULL)
        return ERROR_FILE_ENCRYPTED;

    // Shuffle the Vector with the block index
    // Note that there's no point to go beyond 32 bits, unless the file has
    // more than 0xFFFFFFFF frames.
    for(int i = 0; i < sizeof(dwFrameIndex); i++)
    {
        Vector[i] = Vector[i] ^ (BYTE)((dwFrameIndex >> dwShift) & 0xFF);
        dwShift += 8;
    }

    // Perform the decryption-specific action
    switch(EncryptionType)
    {
        case 'S':   // Salsa20
            nError = Decrypt_Salsa20(pbOutBuffer, pbInBuffer, (pbBufferEnd - pbInBuffer), pbKey, 0x10, Vector);
            if(nError != ERROR_SUCCESS)
                return nError;

            // Supply the size of the output buffer
            pcbOutBuffer[0] = (DWORD)(pbBufferEnd - pbInBuffer);
            return ERROR_SUCCESS;

//      case 'A':   
//          return ERROR_NOT_SUPPORTED;
    }

    assert(false);
    return ERROR_NOT_SUPPORTED;
}

int CascDirectCopy(LPBYTE pbOutBuffer, PDWORD pcbOutBuffer, LPBYTE pbInBuffer, DWORD cbInBuffer)
{
    // Check the buffer size
    if((cbInBuffer - 1) > pcbOutBuffer[0])
        return ERROR_INSUFFICIENT_BUFFER;

    // Copy the data
    memcpy(pbOutBuffer, pbInBuffer, cbInBuffer);
    pcbOutBuffer[0] = cbInBuffer;
    return ERROR_SUCCESS;
}

