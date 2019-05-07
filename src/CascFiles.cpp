/*****************************************************************************/
/* CascFiles.cpp                          Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Various text file parsers                                                 */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 29.04.14  1.00  Lad  The first version of CascBuildCfg.cpp                */
/* 30.10.15  1.00  Lad  Renamed to CascFiles.cpp                             */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

//-----------------------------------------------------------------------------
// Local defines

typedef int (*PARSEINFOFILE)(TCascStorage * hs, void * pvListFile);
typedef int (*PARSE_VARIABLE)(TCascStorage * hs, const char * szVariableName, const char * szDataBegin, const char * szDataEnd, void * pvParam);

#define MAX_VAR_NAME 80

//-----------------------------------------------------------------------------
// Local structures

struct TBuildFileInfo
{
    const TCHAR * szFileName;
    CBLD_TYPE BuildFileType;
};

struct TGameLocaleString
{
    const char * szLocale;
    DWORD dwLocale;
};

static const TBuildFileInfo BuildTypes[] =
{
    {_T(".build.info"), CascBuildInfo},             // Since HOTS build 30027, the game uses .build.info file for storage info
    {_T(".build.db"),   CascBuildDb},               // Older CASC storages
    {NULL, CascBuildNone}
};

static const TCHAR * DataDirs[] =
{
    _T("data") _T(PATH_SEP_STRING) _T("casc"),      // Overwatch
    _T("data"),                                     // TACT casc (for Linux systems)
    _T("Data"),                                     // World of Warcraft, Diablo
    _T("SC2Data"),                                  // Starcraft II (Legacy of the Void) build 38749
    _T("HeroesData"),                               // Heroes of the Storm
    _T("BNTData"),                                  // Heroes of the Storm, until build 30414
    NULL,
};

static const TGameLocaleString LocaleStrings[] = 
{
    {"enUS", CASC_LOCALE_ENUS},
    {"koKR", CASC_LOCALE_KOKR},
    {"frFR", CASC_LOCALE_FRFR},
    {"deDE", CASC_LOCALE_DEDE},
    {"zhCN", CASC_LOCALE_ZHCN},
    {"esES", CASC_LOCALE_ESES},
    {"zhTW", CASC_LOCALE_ZHTW},
    {"enGB", CASC_LOCALE_ENGB},
    {"enCN", CASC_LOCALE_ENCN},
    {"enTW", CASC_LOCALE_ENTW},
    {"esMX", CASC_LOCALE_ESMX},
    {"ruRU", CASC_LOCALE_RURU},
    {"ptBR", CASC_LOCALE_PTBR},
    {"itIT", CASC_LOCALE_ITIT},
    {"ptPT", CASC_LOCALE_PTPT},
    {NULL, 0}
};

//-----------------------------------------------------------------------------
// Local functions

static bool inline IsWhiteSpace(const char * szVarValue)
{
    return (0 <= szVarValue[0] && szVarValue[0] <= 0x20);
}

static bool inline IsValueSeparator(const char * szVarValue)
{
    return (IsWhiteSpace(szVarValue) || (szVarValue[0] == '|'));
}

static bool IsCharDigit(BYTE OneByte)
{
    return ('0' <= OneByte && OneByte <= '9');
}

static void InitCKeyEntry(PCASC_CKEY_ENTRY pCKeyEntry)
{
    memset(pCKeyEntry, 0, sizeof(CASC_CKEY_ENTRY));
    pCKeyEntry->EncodedSize = CASC_INVALID_SIZE;
    pCKeyEntry->ContentSize = CASC_INVALID_SIZE;
}

static const char * CaptureDecimalInteger(const char * szDataPtr, const char * szDataEnd, PDWORD PtrValue)
{
    const char * szSaveDataPtr = szDataPtr;
    DWORD TotalValue = 0;
    DWORD AddValue = 0;

    // Skip all spaces
    while (szDataPtr < szDataEnd && szDataPtr[0] == ' ')
        szDataPtr++;

    // Load the number
    while (szDataPtr < szDataEnd && szDataPtr[0] != ' ')
    {
        // Must only contain decimal digits ('0' - '9')
        if (!IsCharDigit(szDataPtr[0]))
            break;

        // Get the next value and verify overflow
        AddValue = szDataPtr[0] - '0';
        if ((TotalValue + AddValue) < TotalValue)
            return NULL;

        TotalValue = (TotalValue * 10) + AddValue;
        szDataPtr++;
    }

    // If there were no decimal digits, we consider it failure
    if(szDataPtr == szSaveDataPtr)
        return NULL;

    // Give the result
    PtrValue[0] = TotalValue;
    return szDataPtr;
}

static const char * CaptureSingleString(const char * szDataPtr, const char * szDataEnd, char * szBuffer, size_t ccBuffer)
{
    char * szBufferEnd = szBuffer + ccBuffer - 1;

    // Skip all whitespaces
    while (szDataPtr < szDataEnd && IsWhiteSpace(szDataPtr))
        szDataPtr++;

    // Copy the string
    while (szDataPtr < szDataEnd && szBuffer < szBufferEnd && !IsWhiteSpace(szDataPtr) && szDataPtr[0] != '=')
        *szBuffer++ = *szDataPtr++;

    szBuffer[0] = 0;
    return szDataPtr;
}

static const char * CaptureSingleHash(const char * szDataPtr, const char * szDataEnd, LPBYTE HashValue, size_t HashLength)
{
    const char * szHashString;
    size_t HashStringLength = HashLength * 2;

    // Skip all whitespaces
    while (szDataPtr < szDataEnd && IsWhiteSpace(szDataPtr))
        szDataPtr++;
    szHashString = szDataPtr;

    // Count all hash characters
    for (size_t i = 0; i < HashStringLength; i++)
    {
        if (szDataPtr >= szDataEnd || isxdigit(szDataPtr[0]) == 0)
            return NULL;
        szDataPtr++;
    }

    // There must be a separator or end-of-string
    if (szDataPtr > szDataEnd || IsWhiteSpace(szDataPtr) == false)
        return NULL;

    // Give the values
    ConvertStringToBinary(szHashString, HashStringLength, HashValue);
    return szDataPtr;
}

static const char * CaptureHashCount(const char * szDataPtr, const char * szDataEnd, size_t * PtrHashCount)
{
    BYTE HashValue[MD5_HASH_SIZE];
    size_t HashCount = 0;

    // Capculate the hash count
    while (szDataPtr < szDataEnd)
    {
        // Check one hash
        szDataPtr = CaptureSingleHash(szDataPtr, szDataEnd, HashValue, MD5_HASH_SIZE);
        if (szDataPtr == NULL)
            return NULL;

        // Skip all whitespaces
        while (szDataPtr < szDataEnd && IsWhiteSpace(szDataPtr))
            szDataPtr++;

        HashCount++;
    }

    // Give results
    PtrHashCount[0] = HashCount;
    return szDataPtr;
}

static DWORD GetLocaleMask(const char * szTag)
{
    for(size_t i = 0; LocaleStrings[i].szLocale != NULL; i++)
    {
        if(!strncmp(LocaleStrings[i].szLocale, szTag, 4))
        {
            return LocaleStrings[i].dwLocale;
        }
    }

    return 0;
}

TCHAR * AppendBlobText(TCHAR * szBuffer, LPBYTE pbData, size_t cbData, TCHAR chSeparator)
{
    // Put the separator, if any
    if(chSeparator != 0)
        *szBuffer++ = chSeparator;

    // Copy the blob data as text
    for(size_t i = 0; i < cbData; i++)
    {
        *szBuffer++ = IntToHexChar[pbData[0] >> 0x04];
        *szBuffer++ = IntToHexChar[pbData[0] & 0x0F];
        pbData++;
    }

    // Terminate the string
    *szBuffer = 0;

    // Return new buffer position
    return szBuffer;
}

static bool CheckConfigFileVariable(
    TCascStorage * hs,                  // Pointer to storage structure
    const char * szLinePtr,             // Pointer to the begin of the line
    const char * szLineEnd,             // Pointer to the end of the line
    const char * szVarName,             // Pointer to the variable to check
    PARSE_VARIABLE PfnParseProc,        // Pointer to the parsing function
    void * pvParseParam)                // Pointer to the parameter passed to parsing function
{
    char szVariableName[MAX_VAR_NAME];

    // Capture the variable from the line
    szLinePtr = CaptureSingleString(szLinePtr, szLineEnd, szVariableName, sizeof(szVariableName));
    if (szLinePtr == NULL)
        return false;

    // Verify whether this is the variable
    if (!CheckWildCard(szVariableName, szVarName))
        return false;

    // Skip the spaces and '='
    while (szLinePtr < szLineEnd && (IsWhiteSpace(szLinePtr) || szLinePtr[0] == '='))
        szLinePtr++;

    // Call the parsing function only if there is some data
    if (szLinePtr >= szLineEnd)
        return false;

    return (PfnParseProc(hs, szVariableName, szLinePtr, szLineEnd, pvParseParam) == ERROR_SUCCESS);
}

static void AppendConfigFilePath(TCHAR * szFileName, PQUERY_KEY pFileKey)
{
    size_t nLength = _tcslen(szFileName);

    // If there is no slash, append if
    if(nLength > 0 && szFileName[nLength - 1] != '\\' && szFileName[nLength - 1] != '/')
        szFileName[nLength++] = _T('/');

    // Get to the end of the file name
    szFileName = szFileName + nLength;

    // Append the "config" directory
    _tcscpy(szFileName, _T("config"));
    szFileName += 6;

    // Append the first level directory
    szFileName = AppendBlobText(szFileName, pFileKey->pbData, 1, _T('/'));
    szFileName = AppendBlobText(szFileName, pFileKey->pbData + 1, 1, _T('/'));
    szFileName = AppendBlobText(szFileName, pFileKey->pbData, pFileKey->cbData, _T('/'));
}

static int LoadHashArray(
    PQUERY_KEY pBlob,
    const char * szLinePtr,
    const char * szLineEnd,
    size_t HashCount)
{
    LPBYTE pbBuffer = pBlob->pbData;
    int nError = ERROR_SUCCESS;

    // Sanity check
    assert(pBlob->pbData != NULL);
    assert(pBlob->cbData == HashCount * MD5_HASH_SIZE);

    for (size_t i = 0; i < HashCount; i++)
    {
        // Capture the hash value
        szLinePtr = CaptureSingleHash(szLinePtr, szLineEnd, pbBuffer, MD5_HASH_SIZE);
        if (szLinePtr == NULL)
            return ERROR_BAD_FORMAT;

        // Move buffer
        pbBuffer += MD5_HASH_SIZE;
    }

    return nError;
}

static int LoadMultipleHashes(PQUERY_KEY pBlob, const char * szLineBegin, const char * szLineEnd)
{
    size_t HashCount = 0;
    int nError = ERROR_SUCCESS;

    // Retrieve the hash count
    if (CaptureHashCount(szLineBegin, szLineEnd, &HashCount) == NULL)
        return ERROR_BAD_FORMAT;

    // Do nothing if there is no data
    if(HashCount != 0)
    {
        // Allocate the blob buffer
        pBlob->pbData = CASC_ALLOC(BYTE, HashCount * MD5_HASH_SIZE);
        if(pBlob->pbData == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Set the buffer size and load the blob array
        pBlob->cbData = (DWORD)(HashCount * MD5_HASH_SIZE);
        nError = LoadHashArray(pBlob, szLineBegin, szLineEnd, HashCount);
    }

    return nError;
}

// Loads a query key from the text form
// A QueryKey is an array of "ContentKey EncodedKey1 ... EncodedKeyN"
static int LoadQueryKey(TCascStorage * /* hs */, const char * /* szVariableName */, const char * szDataBegin, const char * szDataEnd, void * pvParam)
{
    return LoadMultipleHashes((PQUERY_KEY)pvParam, szDataBegin, szDataEnd);
}

static int LoadCKeyEntry(TCascStorage * /* hs */, const char * szVariableName, const char * szDataPtr, const char * szDataEnd, void * pvParam)
{
    PCASC_CKEY_ENTRY pCKeyEntry = (PCASC_CKEY_ENTRY)pvParam;
    size_t nLength = strlen(szVariableName);
    size_t HashCount = 0;

    // If the variable ends at "-size", it means we need to capture the size
    if((nLength > 5) && !strcmp(szVariableName + nLength - 5, "-size"))
    {
        DWORD ContentSize = CASC_INVALID_SIZE;
        DWORD EncodedSize = CASC_INVALID_SIZE;

        // Load the content size
        szDataPtr = CaptureDecimalInteger(szDataPtr, szDataEnd, &ContentSize);
        if(szDataPtr != NULL)
        {
            CaptureDecimalInteger(szDataPtr, szDataEnd, &EncodedSize);
            pCKeyEntry->ContentSize = ContentSize;
            pCKeyEntry->EncodedSize = EncodedSize;
            return ERROR_SUCCESS;
        }
    }
    else
    {
        // Get the number of hashes. It is expected to be 1 or 2
        if(CaptureHashCount(szDataPtr, szDataEnd, &HashCount) != NULL)
        {
            // Capture the CKey
            if(HashCount >= 1)
            {
                // Load the CKey. This should alway be there
                szDataPtr = CaptureSingleHash(szDataPtr, szDataEnd, pCKeyEntry->CKey, MD5_HASH_SIZE);
                if(szDataPtr == NULL)
                    return ERROR_BAD_FORMAT;
                pCKeyEntry->Flags |= CASC_CE_HAS_CKEY;
            }

            // Capture EKey, if any
            if(HashCount == 2)
            {
                // Load the EKey into the structure
                szDataPtr = CaptureSingleHash(szDataPtr, szDataEnd, pCKeyEntry->EKey, MD5_HASH_SIZE);
                if(szDataPtr == NULL)
                    return ERROR_BAD_FORMAT;
                pCKeyEntry->Flags |= CASC_CE_HAS_EKEY;
            }

            return (HashCount == 1 || HashCount == 2) ? ERROR_SUCCESS : ERROR_BAD_FORMAT;
        }
    }

    // Unrecognized
    return ERROR_BAD_FORMAT;
}

static int LoadVfsRootEntry(TCascStorage * hs, const char * szVariableName, const char * szDataPtr, const char * szDataEnd, void * pvParam)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    CASC_ARRAY * pArray = (CASC_ARRAY *)pvParam;
    const char * szVarPtr = szVariableName;
    const char * szVarEnd = szVarPtr + strlen(szVarPtr);
    DWORD VfsRootIndex = CASC_INVALID_INDEX;

    // Skip the "vfs-" part
    if (!strncmp(szVariableName, "vfs-", 4))
    {
        // Then, there must be a decimal number as index
        if ((szVarPtr = CaptureDecimalInteger(szVarPtr + 4, szVarEnd, &VfsRootIndex)) != NULL)
        {
            // We expect the array to be initialized
            assert(pArray->IsInitialized());

            // The index of the key must not be NULL
            if(VfsRootIndex != 0)
            {
                // Make sure that he entry is in the array
                pCKeyEntry = (PCASC_CKEY_ENTRY)pArray->ItemAt(VfsRootIndex - 1);
                if(pCKeyEntry == NULL)
                {
                    // Insert a new entry
                    pCKeyEntry = (PCASC_CKEY_ENTRY)pArray->InsertAt(VfsRootIndex - 1);
                    if(pCKeyEntry == NULL)
                        return ERROR_NOT_ENOUGH_MEMORY;
                    
                    // Initialize the new entry
                    InitCKeyEntry(pCKeyEntry);
                }

                // Call just a normal parse function
                return LoadCKeyEntry(hs, szVariableName, szDataPtr, szDataEnd, pCKeyEntry);
            }
        }
    }

    return ERROR_SUCCESS;
}

static int LoadBuildProductId(TCascStorage * hs, const char * /* szVariableName */, const char * szDataBegin, const char * szDataEnd, void * /* pvParam */)
{
    DWORD dwBuildUid = 0;

    // Convert up to 4 chars to DWORD
    for(size_t i = 0; i < 4 && szDataBegin < szDataEnd; i++)
    {
        dwBuildUid = (dwBuildUid << 0x08) | (BYTE)szDataBegin[0];
        szDataBegin++;
    }

    // Product-specific. See https://wowdev.wiki/TACT#Products
    switch(dwBuildUid)
    {
        case 'd3':
        case 'd3b':     // Diablo 3 Beta (2013) 
        case 'd3cn':    // Diablo 3 China
        case 'd3t':     // Diablo 3 Test
            hs->szProductName = "Diablo 3";
            hs->Product = Diablo3;
            break;

        case 'dst2':
            hs->szProductName = "Destiny 2";
            hs->Product = Destiny2;
            break;

        case 'bnt':     // Heroes of the Storm Alpha 
        case 'hero':    // Heroes of the Storm Retail
        case 'stor':    // Heroes of the Storm (deprecated)
            hs->szProductName = "Heroes Of The Storm";
            hs->Product = HeroesOfTheStorm;
            break;

        case 'pro':
        case 'proc':
        case 'prod':    // "prodev": Overwatch Dev
        case 'proe':    // Not on public CDNs
        case 'prot':    // Overwatch Test
        case 'prov':    // Overwatch Vendor
        case 'prom':    // "proms": Overwatch World Cup Viewer
            hs->szProductName = "Overwatch";
            hs->Product = Overwatch;
            break;

        case 's1':      // StarCraft 1
        case 's1a':     // Starcraft 1 Alpha
        case 's1t':     // StarCraft 1 Test
            hs->szProductName = "Starcraft 1";
            hs->Product = StarCraft1;
            break;

        case 's2':      // StarCraft 2
        case 's2b':     // Starcraft 2 Beta
        case 's2t':     // StarCraft 2 Test
        case 'sc2':     // StarCraft 2 (deprecated)
            hs->szProductName = "Starcraft 2";
            hs->Product = StarCraft2;
            break;

        case 'vipe':    // "viper", "viperdev", "viperv1": Call of Duty Black Ops 4
            hs->szProductName = "Call Of Duty Black Ops 4";
            hs->Product = CallOfDutyBlackOps4;
            break;

        case 'w3':      // Warcraft III
        case 'w3t':     // Warcraft III Public Test
        case 'war3':    // Warcraft III (old)
            hs->szProductName = "WarCraft 3";
            hs->Product = WarCraft3;
            break;

        case 'wow':     // World of Warcraft
        case 'wow_':    // "wow_beta", "wow_classic", "wow_classic_beta"
        case 'wowd':    // "wowdev", "wowdemo"
        case 'wowe':    // "wowe1", "wowe3", "wowe3", 
        case 'wowt':    // World of Warcraft Test
        case 'wowv':    // World of Warcraft Vendor
        case 'wowz':    // World of Warcraft Submission (previously Vendor)
            hs->szProductName = "World Of Warcraft";
            hs->Product = WorldOfWarcraft;
            break;

        default:
            hs->szProductName = "Unknown Product";
            hs->Product = UnknownProduct;
            assert(false);
            break;
    }
    
    return ERROR_SUCCESS;
}

// "B29049"
// "WOW-18125patch6.0.1"
// "30013_Win32_2_2_0_Ptr_ptr"
// "prometheus-0_8_0_0-24919"
static int LoadBuildNumber(TCascStorage * hs, const char * /* szVariableName */, const char * szDataBegin, const char * szDataEnd, void * /* pvParam */)
{
    DWORD dwBuildNumber = 0;
    DWORD dwMaxValue = 0;

    // Parse the string and take the largest decimal numeric value
    // "build-name = 1.21.5.4037-retail"
    while(szDataBegin < szDataEnd)
    {
        // There must be at least three digits (build 99 anyone?)
        if(IsCharDigit(szDataBegin[0]))
        {
            dwBuildNumber = (dwBuildNumber * 10) + (szDataBegin[0] - '0');
            dwMaxValue = CASCLIB_MAX(dwBuildNumber, dwMaxValue);
        }
        else
        {
            // Reset build number when we find non-digit char
            dwBuildNumber = 0;
        }

        // Move to the next
        szDataBegin++;
    }

    // If not there, just take value from build file
    if((hs->dwBuildNumber = dwMaxValue) == 0)
        hs->dwBuildNumber = hs->CdnBuildKey.pbData[0] % 100;
    return ERROR_BAD_FORMAT;
}

static int GetDefaultLocaleMask(TCascStorage * hs, PQUERY_KEY pTagsString)
{
    char * szTagEnd = (char *)pTagsString->pbData + pTagsString->cbData;
    char * szTagPtr = (char *)pTagsString->pbData;
    DWORD dwLocaleMask = 0;

    while(szTagPtr < szTagEnd)
    {
        // Could this be a locale string?
        if((szTagPtr + 4) <= szTagEnd && (szTagPtr[4] == ',' ||  szTagPtr[4] == ' '))
        {
            // Check whether the current tag is a language identifier
            dwLocaleMask = dwLocaleMask | GetLocaleMask(szTagPtr);
            szTagPtr += 4;
        }
        else
        {
            szTagPtr++;
        }
    }

    hs->dwDefaultLocale = dwLocaleMask;
    return ERROR_SUCCESS;
}

static TCHAR * MakeCdnList(CASC_CSV & Csv, size_t * Indices)
{
    TCHAR * szCdnList = NULL;
    TCHAR * szCdnPtr;
    TCHAR * szCdnEnd;
    const char * szHostPtr;
    char szHostsList[0x100] = { 0 };
    char szHostPath[0x80] = { 0 };

    // Retrieve both strings
    if (Csv.GetString(szHostsList, sizeof(szHostsList) - 1, Indices[0]) != ERROR_SUCCESS)
        return NULL;
    if (Csv.GetString(szHostPath, sizeof(szHostPath), Indices[1]) != ERROR_SUCCESS)
        return NULL;
    
    // Did we retrieve something?
    if (szHostsList[0] != 0)
    {
        size_t nCombinedLength;
        size_t nHostsLength = strlen(szHostsList) + 1;
        size_t nPathLength = strlen(szHostPath);
        size_t cbToAllocate;
        size_t nHostsCount = 1;

        // Cut the hosts list to pieces
        for (size_t i = 0; i < nHostsLength; i++)
        {
            if (szHostsList[i] == ' ')
            {
                szHostsList[i] = 0;
                nHostsCount++;
            }
        }

        // Merge the hosts list
        cbToAllocate = nHostsLength + (nPathLength + 1) * nHostsCount + 1;
        szCdnList = szCdnPtr = CASC_ALLOC(TCHAR, cbToAllocate);
        szCdnEnd = szCdnList + cbToAllocate - 1;
        if (szCdnList != NULL)
        {
            for (szHostPtr = szHostsList; szHostPtr[0] != 0; szHostPtr += strlen(szHostPtr) + 1)
            {
                nCombinedLength = CombineUrlPath(szCdnPtr, (szCdnEnd - szCdnPtr), szHostPtr, szHostPath);
                if (nCombinedLength == 0)
                    break;

                szCdnPtr += nCombinedLength + 1;
                szCdnPtr[0] = 0;
            }
        }
    }

    return szCdnList;
}

static int ParseFile_BuildInfo(TCascStorage * hs, void * pvListFile)
{
    CASC_CSV Csv;
    size_t Indices[4];
    char szActive[4];
    int nError;

    // Extract the first line, containing the headers
    if (Csv.LoadHeader(pvListFile) == 0)
        return ERROR_BAD_FORMAT;

    // Retrieve the column indices
    if (!Csv.GetColumnIndices(Indices, "Active!DEC:1", "Build Key!HEX:16", "CDN Key!HEX:16", NULL))
        return ERROR_BAD_FORMAT;

    // Find the active config
    for (;;)
    {
        // Load a next line
        if (Csv.LoadNextLine(pvListFile) == 0)
            break;

        // Is that build config active?
        if (Csv.GetString(szActive, 4, Indices[0]) == ERROR_SUCCESS && !strcmp(szActive, "1"))
        {
            // Retrieve the build key
            nError = Csv.GetData(hs->CdnBuildKey, Indices[1], true);
            if (nError != ERROR_SUCCESS)
                return nError;

            // Retrieve the config key
            nError = Csv.GetData(hs->CdnConfigKey, Indices[2], true);
            if (nError != ERROR_SUCCESS)
                return nError;

            // "CDN Servers"?
            if (Csv.GetColumnIndices(Indices, "CDN Servers!STRING:0", "CDN Path!STRING:0", NULL))
            {
                hs->szCdnList = MakeCdnList(Csv, Indices);
            }

            // "CDN Hosts"?
            else if (Csv.GetColumnIndices(Indices, "CDN Hosts!STRING:0", "CDN Path!STRING:0", NULL))
            {
                hs->szCdnList = MakeCdnList(Csv, Indices);
            }

            // If we found tags, we can extract language build from it
            if (Csv.GetColumnIndices(Indices, "Tags!STRING:0", NULL))
            {
                QUERY_KEY TagString = { NULL, 0 };

                if (Csv.GetData(TagString, Indices[0], false) == ERROR_SUCCESS && TagString.cbData != 0)
                {
                    GetDefaultLocaleMask(hs, &TagString);
                    FreeCascBlob(&TagString);
                }
            }

            // Build and Config keys are the ones we do really need
            return (hs->CdnConfigKey.cbData == MD5_HASH_SIZE && hs->CdnBuildKey.cbData == MD5_HASH_SIZE) ? ERROR_SUCCESS : ERROR_BAD_FORMAT;
        }
    }

    return ERROR_FILE_NOT_FOUND;
}

static int ParseFile_BuildDb(TCascStorage * hs, void * pvListFile)
{
    QUERY_KEY TagString = {NULL, 0};
    CASC_CSV Csv;
    int nError;

    // Load the single line from the text file
    if(Csv.LoadNextLine(pvListFile) == 0)
        return ERROR_BAD_FORMAT;

    // Extract the CDN build key
    nError = Csv.GetData(hs->CdnBuildKey, 0, true);
    if (nError != ERROR_SUCCESS)
        return nError;

    // Load the CDN config key
    nError = Csv.GetData(hs->CdnConfigKey, 1, true);
    if (nError != ERROR_SUCCESS)
        return nError;

    // Load the the tags
    nError = Csv.GetData(TagString, 2, false);
    if (nError == ERROR_SUCCESS && TagString.pbData != NULL)
    {
        GetDefaultLocaleMask(hs, &TagString);
        FreeCascBlob(&TagString);
    }

    // Load the URL
    hs->szCdnList = Csv.GetString(3);
    if (hs->szCdnList == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Verify all variables
    if (hs->CdnBuildKey.pbData == NULL || hs->CdnConfigKey.pbData == NULL)
        nError = ERROR_BAD_FORMAT;
    return nError;
}

static int ParseFile_CdnConfig(TCascStorage * hs, void * pvListFile)
{
    const char * szLineBegin;
    const char * szLineEnd;
    int nError = ERROR_SUCCESS;

    // Keep parsing the listfile while there is something in there
    for(;;)
    {
        // Get the next line
        if(!ListFile_GetNextLine(pvListFile, &szLineBegin, &szLineEnd))
            break;

        // CDN key of ARCHIVE-GROUP. Archive-group is actually a very special '.index' file.
        // It is essentially a merger of all .index files, with a structure change
        // When ".index" added after the ARCHIVE-GROUP, we get file name in "indices" folder
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "archive-group", LoadQueryKey, &hs->ArchiveGroup))
            continue;

        // CDN key of all archives. When ".index" added to the string, we get file name in "indices" folder
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "archives", LoadQueryKey, &hs->ArchivesKey))
            continue;

        // CDN keys of patch archives (needs research) 
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "patch-archives", LoadQueryKey, &hs->PatchArchivesKey))
            continue;

        // CDN key of probably the combined patch index file (needs research)
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "patch-archive-group", LoadQueryKey, &hs->PatchArchivesGroup))
            continue;

        // List of build configs this config supports (optional)
        // Points to file: "data\config\%02X\%02X\%s
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "builds", LoadQueryKey, &hs->BuildFiles))
            continue;
    }

    // Check if all required fields are present
    if(hs->ArchivesKey.pbData == NULL || hs->ArchivesKey.cbData == 0)
        return ERROR_BAD_FORMAT;

    return nError;
}

static int ParseFile_CdnBuild(TCascStorage * hs, void * pvListFile)
{
    const char * szLineBegin;
    const char * szLineEnd = NULL;
    int nError;

    // Initialize the empty VFS array
    nError = hs->VfsRootList.Create<CASC_CKEY_ENTRY>(0x10);
    if (nError != ERROR_SUCCESS)
        return nError;

    // Initialize entries for the internal files
    InitCKeyEntry(&hs->EncodingCKey);
    InitCKeyEntry(&hs->DownloadCKey);
    InitCKeyEntry(&hs->InstallFile);
    InitCKeyEntry(&hs->PatchFile);
    InitCKeyEntry(&hs->RootFile);
    InitCKeyEntry(&hs->SizeFile);
    InitCKeyEntry(&hs->VfsRoot);

    // Parse all variables
    while(ListFile_GetNextLine(pvListFile, &szLineBegin, &szLineEnd) != 0)
    {
        // Product name and build name
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "build-uid", LoadBuildProductId, NULL))
            continue;
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "build-name", LoadBuildNumber, NULL))
            continue;

        // Content key of the ROOT file. Look this up in ENCODING to get the encoded key
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "root*", LoadCKeyEntry, &hs->RootFile))
            continue;

        // Content key [+ encoded key] of the INSTALL file
        // If EKey is absent, you need to query the ENCODING file for it
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "install*", LoadCKeyEntry, &hs->InstallFile))
            continue;

        // Content key [+ encoded key] of the DOWNLOAD file
        // If EKey is absent, you need to query the ENCODING file for it
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "download*", LoadCKeyEntry, &hs->DownloadCKey))
            continue;

        // Content key + encoded key of the ENCODING file. Contains CKey+EKey
        // If either none or 1 is found, the game (at least Wow) switches to plain-data(?). Seen in build 20173 
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "encoding*", LoadCKeyEntry, &hs->EncodingCKey))
            continue;

        // PATCH file
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "patch*", LoadCKeyEntry, &hs->PatchFile))
            continue;

        // SIZE file
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "size*", LoadCKeyEntry, &hs->SizeFile))
            continue;

        // VFS root file (the root file of the storage VFS)
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "vfs-root*", LoadCKeyEntry, &hs->VfsRoot))
            continue;

        // Load a directory entry to the VFS
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "vfs-*", LoadVfsRootEntry, &hs->VfsRootList))
            continue;
    }

    // Both CKey and EKey of ENCODING file is required
    if((hs->EncodingCKey.Flags & (CASC_CE_HAS_CKEY | CASC_CE_HAS_EKEY)) != (CASC_CE_HAS_CKEY | CASC_CE_HAS_EKEY))
        return ERROR_BAD_FORMAT;
    return nError;
}

static int FetchAndLoadConfigFile(TCascStorage * hs, PQUERY_KEY pFileKey, PARSEINFOFILE PfnParseProc)
{
    TCHAR * szFileName;
    void * pvListFile = NULL;
    int nError = ERROR_CAN_NOT_COMPLETE;

    // Construct the local file name
    szFileName = CascNewStr(hs->szDataPath, 8 + 3 + 3 + 32);
    if (szFileName != NULL)
    {
        // Add the part where the config file path is
        AppendConfigFilePath(szFileName, pFileKey);

        // Load and verify the external listfile
        pvListFile = ListFile_OpenExternal(szFileName);
        if (pvListFile != NULL)
        {
            if (ListFile_VerifyMD5(pvListFile, pFileKey->pbData))
            {
                nError = PfnParseProc(hs, pvListFile);
            }
            else
            {
                nError = ERROR_FILE_CORRUPT;
            }
            ListFile_Free(pvListFile);
        }
        else
        {
            nError = ERROR_FILE_NOT_FOUND;
        }
        CASC_FREE(szFileName);
    }
    else
    {
        nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    return nError;
}

static int CheckDataDirectory(TCascStorage * hs, TCHAR * szDirectory)
{
    TCHAR * szDataPath;
    int nError = ERROR_FILE_NOT_FOUND;

    // Try all known subdirectories
    for(size_t i = 0; DataDirs[i] != NULL; i++)
    {
        // Create the eventual data path
        szDataPath = CombinePath(szDirectory, DataDirs[i]);
        if(szDataPath != NULL)
        {
            // Does that directory exist?
            if(DirectoryExists(szDataPath))
            {
                hs->szDataPath = szDataPath;
                return ERROR_SUCCESS;
            }

            // Free the data path
            CASC_FREE(szDataPath);
        }
    }

    return nError;
}

static int LoadTextFile(TCascStorage * hs, PARSEINFOFILE PfnParseProc)
{
    void * pvListFile;
    int nError = ERROR_FILE_NOT_FOUND;

    // Load the text file for line-to-line parsing
    pvListFile = ListFile_OpenExternal(hs->szBuildFile);
    if (pvListFile != NULL)
    {
        // Parse the text file
        nError = PfnParseProc(hs, pvListFile);
        ListFile_Free(pvListFile);
    }

    return nError;
}

//-----------------------------------------------------------------------------
// Public functions

// Checks whether there is a ".build.info" or ".build.db".
// If yes, the function sets "szDataPath" and "szIndexPath"
// in the storage structure and returns ERROR_SUCCESS
int CheckGameDirectory(TCascStorage * hs, TCHAR * szDirectory)
{
    TFileStream * pStream;
    TCHAR * szBuildFile;
    int nError = ERROR_FILE_NOT_FOUND;

    // Try to find any of the root files used in the history
    for (size_t i = 0; BuildTypes[i].szFileName != NULL; i++)
    {
        // Create the full name of the .agent.db file
        szBuildFile = CombinePath(szDirectory, BuildTypes[i].szFileName);
        if (szBuildFile != NULL)
        {
            // Attempt to open the file
            pStream = FileStream_OpenFile(szBuildFile, STREAM_FLAG_READ_ONLY);
            if (pStream != NULL)
            {
                // Free the stream
                FileStream_Close(pStream);

                // Check for the data directory
                nError = CheckDataDirectory(hs, szDirectory);
                if (nError == ERROR_SUCCESS)
                {
                    hs->szBuildFile = szBuildFile;
                    hs->BuildFileType = BuildTypes[i].BuildFileType;
                    return ERROR_SUCCESS;
                }
            }

            CASC_FREE(szBuildFile);
        }
    }

    return nError;
}

int LoadBuildInfo(TCascStorage * hs)
{
    PARSEINFOFILE PfnParseProc = NULL;
    int nError = ERROR_NOT_SUPPORTED;

    // We support either ".build.info" or ".build.db"
    switch (hs->BuildFileType)
    {
        case CascBuildInfo:
            PfnParseProc = ParseFile_BuildInfo;
            break;

        case CascBuildDb:
            PfnParseProc = ParseFile_BuildDb;
            break;

        default:
            nError = ERROR_NOT_SUPPORTED;
            break;
    }

    return LoadTextFile(hs, PfnParseProc);
}

int LoadCdnConfigFile(TCascStorage * hs)
{
    // The CKey for the CDN config should have been loaded already
    assert(hs->CdnConfigKey.pbData != NULL && hs->CdnConfigKey.cbData == MD5_HASH_SIZE);

    // Load the CDN config file. Note that we don't
    // need it for anything, really, so we don't care if it fails
    FetchAndLoadConfigFile(hs, &hs->CdnConfigKey, ParseFile_CdnConfig);
    return ERROR_SUCCESS;
}

int LoadCdnBuildFile(TCascStorage * hs)
{
    // The CKey for the CDN config should have been loaded already
    assert(hs->CdnBuildKey.pbData != NULL && hs->CdnBuildKey.cbData == MD5_HASH_SIZE);

    // Load the CDN config file. Note that we don't
    // need it for anything, really, so we don't care if it fails
    return FetchAndLoadConfigFile(hs, &hs->CdnBuildKey, ParseFile_CdnBuild);
}
