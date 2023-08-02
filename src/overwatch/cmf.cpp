/*****************************************************************************/
/* cmf.cpp                                Copyright (c) Ladislav Zezula 2023 */
/*---------------------------------------------------------------------------*/
/* Support for Content Manifest Files (.cmf)                                 */
/* Know-how from https://github.com/overtools/TACTLib                        */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.07.23  1.00  Lad  Created                                              */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

#include "aes.h"

//-----------------------------------------------------------------------------
// Structures related to the CMF file

#define CASC_OVERWATCH_VERSION_122_PTR  47161
#define CASC_OVERWATCH_VERSION_148_PTR  68309
#define CASC_OVERWATCH_VERSION_152_PTR  72317

#define CASC_CMF_ENCRYPTED_MAGIC  0x636D66

#define CASC_AES_KEY_LENGTH     0x20
#define CASC_AES_IV_LENGTH      0x10
#define SHA1_DIGESTSIZE         SHA1_HASH_SIZE      // Used in cmf-key.cpp

typedef unsigned char  byte;
typedef unsigned short ushort;
typedef unsigned int   uint;                        // Used in cmf-key.cpp

// 1.00+
struct CASC_CMF_HEADER_100
{
    unsigned dwBuildVersion;
    unsigned dwField04;
    unsigned dwField08;
    unsigned dwField10;
    unsigned dwField14;
    int      nDataCount;
    unsigned dwField1C;
    int      nEntryCount;
    unsigned dwHeaderMagic;
};

// 1.22+
struct CASC_CMF_HEADER_122
{
    unsigned dwBuildVersion;
    unsigned dwField04;
    unsigned dwField08;
    unsigned dwField0C;
    unsigned dwField10;
    unsigned dwField14;
    int      nDataCount;
    unsigned dwField1C;
    int      nEntryCount;
    unsigned dwHeaderMagic;
};

// 1.48+
struct CASC_CMF_HEADER_148
{
    unsigned dwBuildVersion;
    unsigned dwField04;
    unsigned dwField08;
    unsigned dwField0C;
    unsigned dwField10;
    unsigned dwField14;
    unsigned dwField18;
    int      nDataPatchRecordCount;
    int      nDataCount;
    int      nEntryPatchRecordCount;
    int      nEntryCount;
    unsigned dwHeaderMagic;
};

// This structure has the members with the same names like CMFHeader in TACTLib
// This is because we reuse the C# code from that library as C++
struct CASC_CMF_HEADER
{
    bool IsEncrypted() const
    {
        return ((m_magic >> 0x08) == CASC_CMF_ENCRYPTED_MAGIC);
    }

    byte GetVersion() const
    {
        return IsEncrypted() ? (byte)(m_magic & 0x000000FF) : (byte)((m_magic & 0xFF000000) >> 24);
    }

    uint GetNonEncryptedMagic() const
    {
        return (uint)(0x00666D63u | (GetVersion() << 24));
    }

    void operator = (const CASC_CMF_HEADER_100 & src)
    {
        memset(this, 0, sizeof(CASC_CMF_HEADER_148));
        m_buildVersion = src.dwBuildVersion;
        m_dataCount = src.nDataCount;
        m_entryCount = src.nEntryCount;
        m_magic = src.dwHeaderMagic;
    }

    void operator = (const CASC_CMF_HEADER_122 & src)
    {
        memset(this, 0, sizeof(CASC_CMF_HEADER_148));
        m_buildVersion = src.dwBuildVersion;
        m_dataCount = src.nDataCount;
        m_entryCount = src.nEntryCount;
        m_magic = src.dwHeaderMagic;
    }

    void operator = (const CASC_CMF_HEADER_148 & src)
    {
        // Copy 1:1
        memcpy(this, &src, sizeof(CASC_CMF_HEADER_148));
    }

    uint m_buildVersion;
    uint m_unk04;
    uint m_unk08;
    uint m_unk0C;
    uint m_unk10;
    uint m_unk14;
    uint m_unk18;
    int  m_dataPatchRecordCount;
    int  m_dataCount;
    int  m_entryPatchRecordCount;
    int  m_entryCount;
    uint m_magic;
};

typedef struct _CASC_APM_ENTRY
{
    BYTE m_index[4];
    BYTE m_hashA[8];
    BYTE m_hashB[8];
} CASC_APM_ENTRY, *PCASC_APM_ENTRY;

typedef struct _CASC_CMF_HASH_ENTRY_100
{
    BYTE GUID[8];
    BYTE Size[4];
    BYTE CKey[CASC_CKEY_SIZE];
} CASC_CMF_HASH_ENTRY_100, *PCASC_CMF_HASH_ENTRY_100;

typedef struct _CASC_CMF_HASH_ENTRY_135
{
    BYTE GUID[8];
    BYTE Size[4];
    BYTE field_C;
    BYTE CKey[CASC_CKEY_SIZE];
} CASC_CMF_HASH_ENTRY_135, *PCASC_CMF_HASH_ENTRY_135;

//-----------------------------------------------------------------------------
// Encryption key providers for CMF files. These are taken from TACTLib
// with the kind permission of the TACTLib authors
// (https://github.com/overtools/TACTLib)

// Key and IV provider functions
typedef LPBYTE(*GET_KEY)(const CASC_CMF_HEADER & Header, LPBYTE pbKey, int nLength);
typedef LPBYTE(*GET_IV)(const CASC_CMF_HEADER & Header, LPBYTE nameSha1, LPBYTE pbKey, int nLength);

// Structure for the single provider
typedef struct _CASC_CMF_KEY_PROVIDER
{
    DWORD   dwBuildNumber;
    GET_KEY PfnGetKey;
    GET_IV  PfnGetIV;
} CASC_CMF_KEY_PROVIDER;
typedef const CASC_CMF_KEY_PROVIDER *PCASC_CMF_KEY_PROVIDER;

// Needed by various providers in the cmf-key.cpp file
struct TMath
{
    template <typename TYPE>
    TYPE Max(TYPE value1, TYPE value2)
    {
        return (value1 > value2) ? value1 : value2;
    }
    DWORD dwDummy;
} Math;

// Needed by various providers in the cmf-key.cpp file
static uint Constrain(LONGLONG value)
{
    return (uint)(value % 0xFFFFFFFFULL);
}

// Needed by various providers in the cmf-key.cpp file
static int SignedMod(LONGLONG p1, LONGLONG p2)
{
    int a = (int)p1;
    int b = (int)p2;
    return (a % b) < 0 ? (a % b + b) : (a % b);
}

// Include the CMF key provider functions and the table of providers
// This file is created by the "cmf-update.py" script, DO NOT EDIT.
#include "cmf-key.cpp"

//-----------------------------------------------------------------------------
// Local functions

static void BinaryReverse64(LPBYTE GuidReversed, LPBYTE pbGuid)
{
    GuidReversed[0] = pbGuid[7];
    GuidReversed[1] = pbGuid[6];
    GuidReversed[2] = pbGuid[5];
    GuidReversed[3] = pbGuid[4];
    GuidReversed[4] = pbGuid[3];
    GuidReversed[5] = pbGuid[2];
    GuidReversed[6] = pbGuid[1];
    GuidReversed[7] = pbGuid[0];
}

static PCASC_CMF_KEY_PROVIDER FindCmfKeyProvider(DWORD dwBuildNumber)
{
    PCASC_CMF_KEY_PROVIDER pStartEntry = CmfKeyProviders;
    PCASC_CMF_KEY_PROVIDER pMidleEntry = NULL;
    PCASC_CMF_KEY_PROVIDER pFinalEntry = &CmfKeyProviders[_countof(CmfKeyProviders)];

    // Perform binary search on the table
    while(pStartEntry < pFinalEntry)
    {
        // Calculate the middle of the interval
        pMidleEntry = pStartEntry + ((pFinalEntry - pStartEntry) / 2);

        // Did we find it?
        if(dwBuildNumber == pMidleEntry->dwBuildNumber)
            return pMidleEntry;

        // Move the interval to the left or right
        if(dwBuildNumber > pMidleEntry->dwBuildNumber)
            pStartEntry = pMidleEntry + 1;
        else
            pFinalEntry = pMidleEntry;
    }
/*
    for(size_t i = 0; i < _countof(CmfKeyProviders); i++)
    {
        if(CmfKeyProviders[i].dwBuildNumber == dwBuildNumber)
        {
            return &CmfKeyProviders[i];
        }
    }
*/
    return NULL;
}

static DWORD DecryptCmfStream(const CASC_CMF_HEADER & Header, const char * szPlainName, LPBYTE pbDataPtr, LPBYTE pbDataEnd)
{
    PCASC_CMF_KEY_PROVIDER pKeyProvider;
    AES_KEY AesKey;
    BYTE RawKey[CASC_AES_KEY_LENGTH];
    BYTE RawIV[CASC_AES_IV_LENGTH];
    BYTE nameDigest[SHA1_HASH_SIZE];

    // Find the provider for that Overwatch build
    if((pKeyProvider = FindCmfKeyProvider(Header.m_buildVersion)) == NULL)
        return ERROR_FILE_ENCRYPTED;

    // Create SHA1 from the file name
    CascHash_SHA1(szPlainName, strlen(szPlainName), nameDigest);

    // Retrieve key and IV
    pKeyProvider->PfnGetKey(Header, RawKey, sizeof(RawKey));
    pKeyProvider->PfnGetIV(Header, nameDigest, RawIV, sizeof(RawIV));

    // Decrypt the stream using AES
    AES_set_decrypt_key(RawKey, 256, &AesKey);
    AES_cbc_decrypt(pbDataPtr, pbDataPtr, (pbDataEnd - pbDataPtr), &AesKey, RawIV);
    return ERROR_SUCCESS;
}

static const char * ExtractAssetSubString(char * szBuffer, size_t ccBuffer, const char * szPlainName)
{
    char * szBufferEnd = szBuffer + ccBuffer - 1;

    while(szBuffer < szBufferEnd && szPlainName[0] != 0 && szPlainName[0] != '.' && szPlainName[0] != '_')
        *szBuffer++ = *szPlainName++;

    if(szBuffer <= szBufferEnd)
        szBuffer[0] = 0;
    return szPlainName;
}

static const char * AppendAssetSubString(char * szBuffer, size_t ccBuffer, const char * szPlainName)
{
    char * szBufferPtr = szBuffer + strlen(szBuffer);
    char * szBufferEnd = szBuffer + ccBuffer - 1;

    if(szBufferPtr < szBufferEnd)
        *szBufferPtr++ = '-';

    while(szBufferPtr < szBufferEnd && szPlainName[0] != '_')
        *szBufferPtr++ = *szPlainName++;

    szBufferPtr[0] = 0;
    return szPlainName;
}

static size_t BuildFileNameTemplate(
    char * szNameTemplate,
    size_t ccNameTemplate,
    const char * szPrefix,
    const char * szFileName,
    const char * szAssetName)
{
    const char * szFileExt = NULL;
    char * szBufferEnd = szNameTemplate + ccNameTemplate;
    char * szBufferPtr = szNameTemplate;
    char * szPlainName;
    char szPlatform[64] = {0};
    char szLocale[64] = {0};
    char szAsset[64] = {0};

    // Parse the plain name
    while(szAssetName[0] != '.')
    {
        // Watch start of the new field
        if(szAssetName[0] == '_')
        {
            // Extract platform from "_SP"
            if(szAssetName[1] == 'S' && szAssetName[2] == 'P' && !_strnicmp(szAssetName, "_SPWin_", 7))
            {
                CascStrCopy(szPlatform, _countof(szPlatform), "Windows");
                szAssetName += 6;
                continue;
            }

            // Extract "RDEV" or "RCN"
            if(szAssetName[1] == 'R')
            {
                szAssetName = AppendAssetSubString(szPlatform, _countof(szPlatform), szAssetName + 1);
                continue;
            }

            // Extract locale
            if(szAssetName[1] == 'L')
            {
                szAssetName = ExtractAssetSubString(szLocale, _countof(szLocale), szAssetName + 2);
                continue;
            }

            // Ignore "_EExt"
            if(szAssetName[1] == 'E' && szAssetName[2] == 'E')
            {
                szAssetName += 5;
                continue;
            }

            // Extract the asset name
            szAssetName = ExtractAssetSubString(szAsset, _countof(szAsset), szAssetName + 1);

            // Extract a possible extension
            //if(!_stricmp(szAsset, "speech"))
            //    szFileExt = ".wav";
            //if(!_stricmp(szAsset, "text"))
            //    szFileExt = ".text";
            continue;
        }
        szAssetName++;
    }

    // Combine the path like "%PREFIX%\\%PLATFORM%-%DEV%\\%LOCALE%\\%ASSET%\\%PLAIN_NAME%.%EXTENSSION%"
    if(szPrefix && szPrefix[0])
        szBufferPtr += CascStrPrintf(szBufferPtr, (szBufferEnd - szBufferPtr), "%s\\", szPrefix);
    if(szPlatform[0])
        szBufferPtr += CascStrPrintf(szBufferPtr, (szBufferEnd - szBufferPtr), "%s\\", szPlatform);
    if(szLocale[0])
        szBufferPtr += CascStrPrintf(szBufferPtr, (szBufferEnd - szBufferPtr), "%s\\", szLocale);
    if(szAsset[0])
        szBufferPtr += CascStrPrintf(szBufferPtr, (szBufferEnd - szBufferPtr), "%s\\", szAsset);
    szPlainName = szBufferPtr;

    // Append file name and extension
    if(szFileName && szFileName[0])
        szBufferPtr += CascStrPrintf(szBufferPtr, (szBufferEnd - szBufferPtr), "%s", szFileName);
    if(szFileExt && szFileExt[0])
        CascStrPrintf(szBufferPtr, (szBufferEnd - szBufferPtr), "%s", szFileExt);

    // Return the length of the path
    return (szPlainName - szNameTemplate);
}

static void InsertAssetFile(
    TCascStorage * hs,
    CASC_FILE_TREE & FileTree,
    char * szFileName,
    size_t nPlainName,              // Offset of the plain name in the name template
    LPBYTE pbCKey,
    LPBYTE pbGuid)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    BYTE GuidReversed[8];

    // Try to find the CKey
    if((pCKeyEntry = FindCKeyEntry_CKey(hs, pbCKey)) != NULL)
    {
        // Save the character at the end of the name (dot or EOS)
        char chSaveChar = szFileName[nPlainName + 16];

        // Imprint the GUID as binary value
        BinaryReverse64(GuidReversed, pbGuid);
        StringFromBinary(GuidReversed, sizeof(GuidReversed), szFileName + nPlainName);
        szFileName[nPlainName + 16] = chSaveChar;

        // Insert the asset to the file tree
        FileTree.InsertByName(pCKeyEntry, szFileName);
    }
}

//-----------------------------------------------------------------------------
// Public functions

DWORD LoadContentManifestFile(TCascStorage * hs, CASC_FILE_TREE & FileTree, PCASC_CKEY_ENTRY pCKeyEntry, const char * szCmfFileName)
{
    CASC_BLOB CmfFile;
    const char * szCmfPlainName = GetPlainFileName(szCmfFileName);
    DWORD dwErrCode;

    //if(!_stricmp(szCmfPlainName, "Win_SPWin_RDEV_EExt.cmf"))
    //    __debugbreak();

    // Load the entire internal file to memory
    if((dwErrCode = LoadInternalFileToMemory(hs, pCKeyEntry, CmfFile)) == ERROR_SUCCESS)
    {
        CASC_CMF_HEADER CmfHeader = {0};
        PCASC_APM_ENTRY pApmEntries = NULL;
        LPBYTE pbDataEnd = CmfFile.pbData + CmfFile.cbData;
        LPBYTE pbDataPtr = CmfFile.pbData;
        size_t nPlainName;
        DWORD dwBuildVersion;
        char szFileName[MAX_PATH];

        // Get the build version
        if((pbDataPtr = CaptureInteger32(pbDataPtr, pbDataEnd, &dwBuildVersion)) == NULL)
            return ERROR_BAD_FORMAT;
        pbDataPtr = CmfFile.pbData;

        // Parse headers of various versions
        if(dwBuildVersion > CASC_OVERWATCH_VERSION_148_PTR)
        {
            CASC_CMF_HEADER_148 * pHeader148;

            if((pbDataPtr = CaptureStructure(pbDataPtr, pbDataEnd, &pHeader148)) == NULL)
                return ERROR_BAD_FORMAT;
            CmfHeader = *pHeader148;
        }
        else if(dwBuildVersion > CASC_OVERWATCH_VERSION_122_PTR)
        {
            CASC_CMF_HEADER_122 * pHeader122;

            if((pbDataPtr = CaptureStructure(pbDataPtr, pbDataEnd, &pHeader122)) == NULL)
                return ERROR_BAD_FORMAT;
            CmfHeader = *pHeader122;
        }
        else
        {
            CASC_CMF_HEADER_100 * pHeader100;

            if((pbDataPtr = CaptureStructure(pbDataPtr, pbDataEnd, &pHeader100)) == NULL)
                return ERROR_BAD_FORMAT;
            CmfHeader = *pHeader100;
        }

        // Decrypt the stream, if needed
        if(CmfHeader.IsEncrypted())
        {
            if((dwErrCode = DecryptCmfStream(CmfHeader, szCmfPlainName, pbDataPtr, pbDataEnd)) != ERROR_SUCCESS)
            {
                return dwErrCode;
            }
        }

        // Skip APM entries. We don't need them for anything, really
        if((pbDataPtr = CaptureArray(pbDataPtr, pbDataEnd, &pApmEntries, CmfHeader.m_entryCount)) == NULL)
        {
            return ERROR_BAD_FORMAT;
        }

        // Create the name template of the assets
        nPlainName = BuildFileNameTemplate(szFileName,
                                           _countof(szFileName),
                                           "ContentManifestFiles",
                                           "0000000000000000",
                                           szCmfPlainName);

        // Load the hash list This is the list of Asset ID -> CKey
        if(CmfHeader.m_buildVersion >= 57230)
        {
            PCASC_CMF_HASH_ENTRY_135 pHashList;

            if((pbDataPtr = CaptureArray(pbDataPtr, pbDataEnd, &pHashList, CmfHeader.m_dataCount)) == NULL)
                return ERROR_BAD_FORMAT;

            for(int i = 0; i < CmfHeader.m_dataCount; i++)
                InsertAssetFile(hs, FileTree, szFileName, nPlainName, pHashList[i].CKey, pHashList[i].GUID);
        }
        else
        {
            PCASC_CMF_HASH_ENTRY_100 pHashList;

            if((pbDataPtr = CaptureArray(pbDataPtr, pbDataEnd, &pHashList, CmfHeader.m_dataCount)) == NULL)
                return ERROR_BAD_FORMAT;

            for(int i = 0; i < CmfHeader.m_dataCount; i++)
                InsertAssetFile(hs, FileTree, szFileName, nPlainName, pHashList[i].CKey, pHashList[i].GUID);
        }
    }
    return ERROR_SUCCESS;
}
