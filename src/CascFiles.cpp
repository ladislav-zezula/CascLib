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
// Local functions

typedef int (*PARSEINFOFILE)(TCascStorage * hs, void * pvListFile);
typedef int (*PARSE_VARIABLE)(TCascStorage * hs, const char * szDataBegin, const char * szDataEnd, void * pvParam);

//-----------------------------------------------------------------------------
// Local structures

struct TBuildFileInfo
{
    const TCHAR * szFileName;
    CBLD_TYPE BuildFileType;
};

struct TGameIdString
{
    const char * szGameInfo;
    size_t cchGameInfo;
    DWORD dwGameInfo;
};

static const TBuildFileInfo BuildTypes[] =
{
    {_T(".build.info"), CascBuildInfo},             // Since HOTS build 30027, the game uses .build.info file for storage info
    {_T(".build.db"),   CascBuildDb},               // Older CASC storages
    {NULL, CascBuildNone}
};

static const TCHAR * DataDirs[] =
{
    _T("SC2Data"),                                  // Starcraft II (Legacy of the Void) build 38749
    _T("Data\\Casc"),                               // Overwatch
    _T("Data"),                                     // World of Warcraft, Diablo
    _T("HeroesData"),                               // Heroes of the Storm
    _T("BNTData"),                                  // Heroes of the Storm, until build 30414
    NULL,
};

static const TGameIdString GameIds[] =
{
    {"Hero",       0x04, CASC_GAME_HOTS},           // Alpha build of Heroes of the Storm
    {"WoW",        0x03, CASC_GAME_WOW6},           // Alpha build of World of Warcraft - Warlords of Draenor
    {"Diablo3",    0x07, CASC_GAME_DIABLO3},        // Diablo III BETA 2.2.0
    {"Prometheus", 0x0A, CASC_GAME_OVERWATCH},      // Overwatch BETA since build 24919
    {"SC2",        0x03, CASC_GAME_STARCRAFT2},     // Starcraft II - Legacy of the Void
    {"Starcraft1", 0x0A, CASC_GAME_STARCRAFT1},     // Starcraft 1 (remake)
    {NULL, 0, 0},
};

//-----------------------------------------------------------------------------
// Local functions

static bool inline IsValueSeparator(const char * szVarValue)
{
    return ((0 <= szVarValue[0] && szVarValue[0] <= 0x20) || (szVarValue[0] == '|'));
}

static bool IsCharDigit(BYTE OneByte)
{
    return ('0' <= OneByte && OneByte <= '9');
}

static DWORD GetLocaleMask(const char * szTag)
{
    if(!strcmp(szTag, "enUS"))
        return CASC_LOCALE_ENUS;

    if(!strcmp(szTag, "koKR"))
        return CASC_LOCALE_KOKR;

    if(!strcmp(szTag, "frFR"))
        return CASC_LOCALE_FRFR;

    if(!strcmp(szTag, "deDE"))
        return CASC_LOCALE_DEDE;

    if(!strcmp(szTag, "zhCN"))
        return CASC_LOCALE_ZHCN;

    if(!strcmp(szTag, "esES"))
        return CASC_LOCALE_ESES;

    if(!strcmp(szTag, "zhTW"))
        return CASC_LOCALE_ZHTW;

    if(!strcmp(szTag, "enGB"))
        return CASC_LOCALE_ENGB;

    if(!strcmp(szTag, "enCN"))
        return CASC_LOCALE_ENCN;

    if(!strcmp(szTag, "enTW"))
        return CASC_LOCALE_ENTW;

    if(!strcmp(szTag, "esMX"))
        return CASC_LOCALE_ESMX;

    if(!strcmp(szTag, "ruRU"))
        return CASC_LOCALE_RURU;

    if(!strcmp(szTag, "ptBR"))
        return CASC_LOCALE_PTBR;

    if(!strcmp(szTag, "itIT"))
        return CASC_LOCALE_ITIT;

    if(!strcmp(szTag, "ptPT"))
        return CASC_LOCALE_PTPT;

    return 0;
}

static bool IsInfoVariable(const char * szLineBegin, const char * szLineEnd, const char * szVarName, const char * szVarType)
{
    size_t nLength;

    // Check the variable name
    nLength = strlen(szVarName);
    if((size_t)(szLineEnd - szLineBegin) > nLength)
    {
        // Check the variable name
        if(!_strnicmp(szLineBegin, szVarName, nLength))
        {
            // Skip variable name and the exclamation mark
            szLineBegin += nLength;
            if(szLineBegin < szLineEnd && szLineBegin[0] == '!')
            {
                // Skip the exclamation mark
                szLineBegin++;

                // Check the variable type
                nLength = strlen(szVarType);
                if((size_t)(szLineEnd - szLineBegin) > nLength)
                {
                    // Check the variable name
                    if(!_strnicmp(szLineBegin, szVarType, nLength))
                    {
                        // Skip variable type and the doublecolon
                        szLineBegin += nLength;
                        return (szLineBegin < szLineEnd && szLineBegin[0] == ':');
                    }
                }
            }
        }
    }

    return false;
}

// Note that this must support empty variables ("||")
static const char * SkipInfoVariable(const char * szLinePtr, const char * szLineEnd)
{
    // Go until we find separator
    while(szLinePtr < szLineEnd && szLinePtr[0] != '|')
        szLinePtr++;

    // Did we find a separator?
    if(szLinePtr < szLineEnd && szLinePtr[0] == '|')
        szLinePtr++;

    return (szLinePtr < szLineEnd) ? szLinePtr : NULL;
}

static TCHAR * CheckForIndexDirectory(TCascStorage * hs, const TCHAR * szSubDir)
{
    TCHAR * szIndexPath;

    // Combine the index path
    szIndexPath = CombinePath(hs->szDataPath, szSubDir);
    if(DirectoryExists(szIndexPath))
    {
        hs->szIndexPath = szIndexPath;
        return hs->szIndexPath;
    }

    CASC_FREE(szIndexPath);
    return NULL;
}

TCHAR * AppendBlobText(TCHAR * szBuffer, LPBYTE pbData, DWORD cbData, TCHAR chSeparator)
{
    // Put the separator, if any
    if(chSeparator != 0)
        *szBuffer++ = chSeparator;

    // Copy the blob data as text
    for(DWORD i = 0; i < cbData; i++)
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
    const char * szLineBegin,           // Pointer to the begin of the line
    const char * szLineEnd,             // Pointer to the end of the line
    const char * szVarName,             // Pointer to the variable to check
    PARSE_VARIABLE PfnParseProc,        // Pointer to the parsing function
    void * pvParseParam)                // Pointer to the parameter passed to parsing function
{
    size_t nLineLength = (size_t)(szLineEnd - szLineBegin);
    size_t nNameLength = strlen(szVarName);

    // Is the line longer than the variable name?
    if(nLineLength > nNameLength)
    {
        // Verify the variable name
        if(!_strnicmp(szLineBegin, szVarName, nNameLength) && IsValueSeparator(szLineBegin + nNameLength))
        {
            // Skip the variable name
            szLineBegin += nNameLength;

            // Skip the separator(s)
            while(szLineBegin < szLineEnd && IsValueSeparator(szLineBegin))
                szLineBegin++;

            // Check if there is "="
            if(szLineBegin < szLineEnd && szLineBegin[0] == '=')
            {
                // Skip the "="
                szLineBegin++;

                // Skip the separator(s)
                while(szLineBegin < szLineEnd && IsValueSeparator(szLineBegin))
                    szLineBegin++;

                // Call the parsing function only if there is some data
                if(szLineBegin < szLineEnd)
                {
                    return (PfnParseProc(hs, szLineBegin, szLineEnd, pvParseParam) == ERROR_SUCCESS);
                }
            }
        }
    }

    return false;
}

static int LoadInfoVariable(PQUERY_KEY pVarBlob, const char * szLineBegin, const char * szLineEnd, bool bHexaValue)
{
    const char * szLinePtr = szLineBegin;

    // Sanity checks
    assert(pVarBlob->pbData == NULL);
    assert(pVarBlob->cbData == 0);

    // Check length of the variable
    while(szLinePtr < szLineEnd && szLinePtr[0] != '|')
        szLinePtr++;

    // Allocate space for the blob
    if(bHexaValue)
    {
        // Initialize the blob
        pVarBlob->pbData = CASC_ALLOC(BYTE, (szLinePtr - szLineBegin) / 2);
        pVarBlob->cbData = (DWORD)((szLinePtr - szLineBegin) / 2);
        return ConvertStringToBinary(szLineBegin, (size_t)(szLinePtr - szLineBegin), pVarBlob->pbData);
    }

    // Initialize the blob
    pVarBlob->pbData = CASC_ALLOC(BYTE, (szLinePtr - szLineBegin) + 1);
    pVarBlob->cbData = (DWORD)(szLinePtr - szLineBegin);

    // Check for success
    if(pVarBlob->pbData == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Copy the string
    memcpy(pVarBlob->pbData, szLineBegin, pVarBlob->cbData);
    pVarBlob->pbData[pVarBlob->cbData] = 0;
    return ERROR_SUCCESS;
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

static size_t GetBlobCount(const char * szLineBegin, const char * szLineEnd)
{
    size_t nBlobCount = 0;

    // Until we find an end of the line
    while(szLineBegin < szLineEnd)
    {
        // Check whether there is a proper blob
        if((szLineBegin + MD5_STRING_SIZE) <= szLineEnd)
        {
            // Verify all characters of the hash
            for(size_t i = 0; i < MD5_STRING_SIZE; i++)
            {
                if(IsValueSeparator(szLineBegin + i))
                    break;
            }

            // Increment the number of blobs
            nBlobCount++;

            // Skip the blob
            szLineBegin += MD5_STRING_SIZE;

            // Skip all eventual spaces
            while(szLineBegin < szLineEnd && IsValueSeparator(szLineBegin))
                szLineBegin++;
        }
    }

    return nBlobCount;
}

static int LoadBlobArray(
    PQUERY_KEY pBlob,
    const char * szLineBegin,
    const char * szLineEnd,
    size_t nBlobCount)
{
    LPBYTE pbBuffer = pBlob->pbData;
    int nError = ERROR_SUCCESS;

    // Sanity check
    assert(pBlob->pbData != NULL);
    assert(pBlob->cbData != 0);

    // Until we find an end of the line
    while(szLineBegin < szLineEnd && nBlobCount > 0)
    {
        // Check whether there is a proper blob
        if((szLineBegin + MD5_STRING_SIZE) <= szLineEnd)
        {
            // Perform the conversion of the blob to the binary array
            nError = ConvertStringToBinary(szLineBegin, MD5_STRING_SIZE, pbBuffer);
            if(nError != ERROR_SUCCESS)
                return nError;

            // Skip the blob
            szLineBegin += MD5_STRING_SIZE;

            // Skip all eventual spaces
            while(szLineBegin < szLineEnd && IsValueSeparator(szLineBegin))
                szLineBegin++;
        }

        // Move pointers
        pbBuffer += MD5_HASH_SIZE;
        nBlobCount--;
    }

    return nError;
}

static int LoadMultipleBlobs(PQUERY_KEY pBlob, const char * szLineBegin, const char * szLineEnd)
{
    size_t nBlobCount = GetBlobCount(szLineBegin, szLineEnd);
    size_t nLength = (szLineEnd - szLineBegin);
    int nError = ERROR_SUCCESS;

    // Do nothing if there is no data
    if(nLength && nBlobCount)
    {
        // We expect each blob to have length of the content key and one space between
        if(nLength > (nBlobCount * MD5_STRING_SIZE) + ((nBlobCount - 1) * sizeof(char)))
            return ERROR_INVALID_PARAMETER;

        // Allocate the blob buffer
        pBlob->pbData = CASC_ALLOC(BYTE, nBlobCount * MD5_HASH_SIZE);
        if(pBlob->pbData == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Set the buffer size and load the blob array
        pBlob->cbData = (DWORD)(nBlobCount * MD5_HASH_SIZE);
        nError = LoadBlobArray(pBlob, szLineBegin, szLineEnd, nBlobCount);
    }

    return nError;
}

// Loads a query key from the text form
// A QueryKey is an array of "ContentKey EncodedKey1 ... EncodedKeyN"
static int LoadQueryKey(TCascStorage * /* hs */, const char * szDataBegin, const char * szDataEnd, void * pvParam)
{
    return LoadMultipleBlobs((PQUERY_KEY)pvParam, szDataBegin, szDataEnd);
}

static int LoadBuildProduct(TCascStorage * hs, const char * szDataBegin, const char * szDataEnd, void * /* pvParam */)
{
    // Go through all games that we support
    for(size_t i = 0; GameIds[i].szGameInfo != NULL; i++)
    {
        // Check the length of the variable
        if((size_t)(szDataEnd - szDataBegin) == GameIds[i].cchGameInfo)
        {
            // Check the string
            if(!_strnicmp(szDataBegin, GameIds[i].szGameInfo, GameIds[i].cchGameInfo))
            {
                hs->dwGameInfo = GameIds[i].dwGameInfo;
                return ERROR_SUCCESS;
            }
        }
    }

    // Unknown/unsupported game
    assert(false);
    return ERROR_BAD_FORMAT;
}

// "B29049"
// "WOW-18125patch6.0.1"
// "30013_Win32_2_2_0_Ptr_ptr"
// "prometheus-0_8_0_0-24919"
static int LoadBuildName(TCascStorage * hs, const char * szDataBegin, const char * szDataEnd, void * /* pvParam */)
{
    DWORD dwBuildNumber = 1;

    // Skip all non-digit characters
    while(szDataBegin < szDataEnd)
    {
        // There must be at least three digits (build 99 anyone?)
        if(IsCharDigit(szDataBegin[0]) && IsCharDigit(szDataBegin[1]) && IsCharDigit(szDataBegin[2]))
        {
            // Convert the build number string to value
            while(szDataBegin < szDataEnd && IsCharDigit(szDataBegin[0]))
                dwBuildNumber = (dwBuildNumber * 10) + (*szDataBegin++ - '0');
            break;
        }

        // Move to the next
        szDataBegin++;
    }

    hs->dwBuildNumber = dwBuildNumber;
    return (dwBuildNumber != 0) ? ERROR_SUCCESS : ERROR_BAD_FORMAT;
}

static int GetDefaultLocaleMask(TCascStorage * hs, PQUERY_KEY pTagsString)
{
    char * szTagEnd = (char *)pTagsString->pbData + pTagsString->cbData;
    char * szTagPtr = (char *)pTagsString->pbData;
    char * szNext;
    DWORD dwLocaleMask = 0;

    while(szTagPtr < szTagEnd)
    {
        // Get the next part
        szNext = strchr(szTagPtr, ' ');
        if(szNext != NULL)
            *szNext++ = 0;

        // Check whether the current tag is a language identifier
        dwLocaleMask = dwLocaleMask | GetLocaleMask(szTagPtr);

        // Get the next part
        if(szNext == NULL)
            break;

        // Skip spaces
        while(szNext < szTagEnd && szNext[0] == ' ')
            szNext++;
        szTagPtr = szNext;
    }

    hs->dwDefaultLocale = dwLocaleMask;
    return ERROR_SUCCESS;
}

static void * FetchAndVerifyConfigFile(TCascStorage * hs, PQUERY_KEY pFileKey)
{
    TCHAR * szFileName;
    void * pvListFile = NULL;

    // Construct the local file name
    szFileName = CascNewStr(hs->szDataPath, 8 + 3 + 3 + 32);
    if(szFileName != NULL)
    {
        // Add the part where the config file path is
        AppendConfigFilePath(szFileName, pFileKey);

        // Load and verify the external listfile
        pvListFile = ListFile_OpenExternal(szFileName);
        if(pvListFile != NULL)
        {
            if(!ListFile_VerifyMD5(pvListFile, pFileKey->pbData))
            {
                ListFile_Free(pvListFile);
                pvListFile = NULL;
            }
        }

        // Free the file name
        CASC_FREE(szFileName);
    }

    return pvListFile;
}

static int ParseFile_BuildInfo(TCascStorage * hs, void * pvListFile)
{
    QUERY_KEY Active = {NULL, 0};
    QUERY_KEY TagString = {NULL, 0};
    QUERY_KEY CdnHost = {NULL, 0};
    QUERY_KEY CdnPath = {NULL, 0};
    const char * szLineBegin1;
    const char * szLinePtr1;
    const char * szLineEnd1;
    const char * szLinePtr2;
    const char * szLineEnd2;
    size_t nLength1;
    size_t nLength2;

    // Extract the first line, cotaining the headers
    nLength1 = ListFile_GetNextLine(pvListFile, &szLineBegin1, &szLineEnd1);
    if(nLength1 == 0)
        return ERROR_BAD_FORMAT;

    // Now parse the second and the next lines. We are looking for line
    // with "Active" set to 1
    for(;;)
    {
        // Read the next line and reset header line
        nLength2 = ListFile_GetNextLine(pvListFile, &szLinePtr2, &szLineEnd2);
        if(nLength2 == 0)
            break;
        szLinePtr1 = szLineBegin1;

        // Parse all variables
        while(szLinePtr1 < szLineEnd1)
        {
            // Check for variables we need
            if(IsInfoVariable(szLinePtr1, szLineEnd1, "Active", "DEC"))
                LoadInfoVariable(&Active, szLinePtr2, szLineEnd2, false);
            else if(IsInfoVariable(szLinePtr1, szLineEnd1, "Build Key", "HEX"))
                LoadInfoVariable(&hs->CdnBuildKey, szLinePtr2, szLineEnd2, true);
            else if(IsInfoVariable(szLinePtr1, szLineEnd1, "CDN Key", "HEX"))
                LoadInfoVariable(&hs->CdnConfigKey, szLinePtr2, szLineEnd2, true);
            else if(IsInfoVariable(szLinePtr1, szLineEnd1, "CDN Hosts", "STRING"))
                LoadInfoVariable(&CdnHost, szLinePtr2, szLineEnd2, false);
            else if(IsInfoVariable(szLinePtr1, szLineEnd1, "CDN Path", "STRING"))
                LoadInfoVariable(&CdnPath, szLinePtr2, szLineEnd2, false);
            else if(IsInfoVariable(szLinePtr1, szLineEnd1, "Tags", "STRING"))
                LoadInfoVariable(&TagString, szLinePtr2, szLineEnd2, false);

            // Move both line pointers
            szLinePtr1 = SkipInfoVariable(szLinePtr1, szLineEnd1);
            if(szLinePtr1 == NULL)
                break;

            szLinePtr2 = SkipInfoVariable(szLinePtr2, szLineEnd2);
            if(szLinePtr2 == NULL)
                break;
        }

        // Stop parsing if found active config
        if(Active.pbData != NULL && *Active.pbData == '1')
            break;

        // Free the blobs
        FreeCascBlob(&Active);
        FreeCascBlob(&hs->CdnBuildKey);
        FreeCascBlob(&hs->CdnConfigKey);
        FreeCascBlob(&CdnHost);
        FreeCascBlob(&CdnPath);
        FreeCascBlob(&TagString);
    }

    // If we have CDN path and CDN host, then we get the URL from it
    if(CdnHost.pbData && CdnHost.cbData && CdnPath.pbData && CdnPath.cbData)
    {
        // Merge the CDN host and CDN path
        hs->szUrlPath = CASC_ALLOC(TCHAR, CdnHost.cbData + CdnPath.cbData + 1);
        if(hs->szUrlPath != NULL)
        {
            CopyString(hs->szUrlPath, (char *)CdnHost.pbData, CdnHost.cbData);
            CopyString(hs->szUrlPath + CdnHost.cbData, (char *)CdnPath.pbData, CdnPath.cbData);
        }
    }

    // If we found tags, we can extract language build from it
    if(TagString.pbData != NULL)
    {
        GetDefaultLocaleMask(hs, &TagString);
    }

    // Free the blobs that we used
    FreeCascBlob(&CdnHost);
    FreeCascBlob(&CdnPath);
    FreeCascBlob(&TagString);
    FreeCascBlob(&Active);

    // Build and Config keys are the ones we do really need
    return (hs->CdnConfigKey.cbData == MD5_HASH_SIZE && hs->CdnBuildKey.cbData == MD5_HASH_SIZE) ? ERROR_SUCCESS : ERROR_BAD_FORMAT;
}

static int ParseFile_BuildDb(TCascStorage * hs, void * pvListFile)
{
    const char * szLinePtr;
    const char * szLineEnd;
    char szOneLine[0x200];
    size_t nLength;
    int nError;

    // Load the single line from the text file
    nLength = ListFile_GetNextLine(pvListFile, szOneLine, _maxchars(szOneLine));
    if(nLength == 0)
        return ERROR_BAD_FORMAT;

    // Set the line range
    szLinePtr = szOneLine;
    szLineEnd = szOneLine + nLength;

    // Extract the CDN build key
    nError = LoadInfoVariable(&hs->CdnBuildKey, szLinePtr, szLineEnd, true);
    if(nError == ERROR_SUCCESS)
    {
        // Skip the variable
        szLinePtr = SkipInfoVariable(szLinePtr, szLineEnd);

        // Load the CDN config hash
        nError = LoadInfoVariable(&hs->CdnConfigKey, szLinePtr, szLineEnd, true);
        if(nError == ERROR_SUCCESS)
        {
            // Skip the variable
            szLinePtr = SkipInfoVariable(szLinePtr, szLineEnd);

            // Skip the Locale/OS/code variable
            szLinePtr = SkipInfoVariable(szLinePtr, szLineEnd);

            // Load the URL
            hs->szUrlPath = CascNewStrFromAnsi(szLinePtr, szLineEnd);
            if(hs->szUrlPath == NULL)
                nError = ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    // Verify all variables
    if(hs->CdnBuildKey.pbData == NULL || hs->CdnConfigKey.pbData == NULL || hs->szUrlPath == NULL)
        nError = ERROR_BAD_FORMAT;
    return nError;
}

static int LoadCdnConfigFile(TCascStorage * hs, void * pvListFile)
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

static int LoadCdnBuildFile(TCascStorage * hs, void * pvListFile)
{
    const char * szLineBegin;
    const char * szLineEnd = NULL;
    int nError = ERROR_SUCCESS;

    for(;;)
    {
        // Get the next line
        if(!ListFile_GetNextLine(pvListFile, &szLineBegin, &szLineEnd))
            break;

        // Product name and build name
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "build-product", LoadBuildProduct, NULL))
            continue;
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "build-name", LoadBuildName, NULL))
            continue;

        // Content key of the ROOT file. Look this up in ENCODING to get the encoded key
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "root", LoadQueryKey, &hs->RootFile))
            continue;

        // Content key [+ encoded key] of the INSTALL file
        // If CKey is absent, you need to query the ENCODING file for it
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "install", LoadQueryKey, &hs->InstallFile))
            continue;

        // Content key [+ encoded key] of the DOWNLOAD file
        // If CKey is absent, you need to query the ENCODING file for it
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "download", LoadQueryKey, &hs->DownloadFile))
            continue;

        // Content key + encoded key of the ENCODING file. Contains CKey+EKey
        // If either none or 1 is found, the game (at least Wow) switches to plain-data(?). Seen in build 20173 
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "encoding", LoadQueryKey, &hs->EncodingFile))
            continue;

        // TODO: Maybe load the size of the ENCODING file from "encoding-size" ?

        // PATCH file
        if(CheckConfigFileVariable(hs, szLineBegin, szLineEnd, "patch", LoadQueryKey, &hs->PatchFile))
            continue;
    }

    // Check the encoding file data
    if(hs->EncodingFile.pbData == NULL || hs->EncodingFile.cbData != MD5_HASH_SIZE * 2)
        return ERROR_BAD_FORMAT;
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


//-----------------------------------------------------------------------------
// Public functions

int LoadBuildInfo(TCascStorage * hs)
{
    PARSEINFOFILE PfnParseProc = NULL;
    void * pvListFile;
    int nError = ERROR_SUCCESS;

    switch(hs->BuildFileType)
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

    // Parse the appropriate build file
    if(nError == ERROR_SUCCESS)
    {
        pvListFile = ListFile_OpenExternal(hs->szBuildFile);
        if(pvListFile != NULL)
        {
            // Parse the info file
            nError = PfnParseProc(hs, pvListFile);
            ListFile_Free(pvListFile);
        }
        else
            nError = ERROR_FILE_NOT_FOUND;
    }

    // If the .build.info OR .build.db file has been loaded,
    // proceed with loading the CDN config file and CDN build file
    if(nError == ERROR_SUCCESS)
    {
        // Load the configuration file. Note that we don't
        // need it for anything, really, so we don't care if it fails
        pvListFile = FetchAndVerifyConfigFile(hs, &hs->CdnConfigKey);
        if(pvListFile != NULL)
        {
            nError = LoadCdnConfigFile(hs, pvListFile);
            ListFile_Free(pvListFile);
        }
    }

    // Load the build file
    if(nError == ERROR_SUCCESS)
    {
        pvListFile = FetchAndVerifyConfigFile(hs, &hs->CdnBuildKey);
        if(pvListFile != NULL)
        {
            nError = LoadCdnBuildFile(hs, pvListFile);
            ListFile_Free(pvListFile);
        }
        else
            nError = ERROR_FILE_NOT_FOUND;
    }

    // Fill the index directory
    if(nError == ERROR_SUCCESS)
    {
        // First, check for more common "data" subdirectory
        if((hs->szIndexPath = CheckForIndexDirectory(hs, _T("data"))) != NULL)
            return ERROR_SUCCESS;

        // Second, try the "darch" subdirectory (older builds of HOTS - Alpha)
        if((hs->szIndexPath = CheckForIndexDirectory(hs, _T("darch"))) != NULL)
            return ERROR_SUCCESS;

        nError = ERROR_FILE_NOT_FOUND;
    }

    return nError;
}

// Checks whether there is a ".build.info" or ".build.db".
// If yes, the function sets "szRootPath" and "szDataPath"
// in the storage structure and returns ERROR_SUCCESS
int CheckGameDirectory(TCascStorage * hs, TCHAR * szDirectory)
{
    TFileStream * pStream;
    TCHAR * szBuildFile;
    int nError = ERROR_FILE_NOT_FOUND;

    // Try to find any of the root files used in the history
    for(size_t i = 0; BuildTypes[i].szFileName != NULL; i++)
    {
        // Create the full name of the .agent.db file
        szBuildFile = CombinePath(szDirectory, BuildTypes[i].szFileName);
        if(szBuildFile != NULL)
        {
            // Attempt to open the file
            pStream = FileStream_OpenFile(szBuildFile, STREAM_FLAG_READ_ONLY);
            if(pStream != NULL)
            {
                // Free the stream
                FileStream_Close(pStream);

                // Check for the data directory
                nError = CheckDataDirectory(hs, szDirectory);
                if(nError == ERROR_SUCCESS)
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

//-----------------------------------------------------------------------------
// Helpers for a CSV files that have multiple variables separated by "|".
// All preceding whitespaces have been removed
//
// Example (Overwatch 24919): "#MD5|CHUNK_ID|FILENAME|INSTALLPATH"
// Example (Overwatch 27759): "#MD5|CHUNK_ID|PRIORITY|MPRIORITY|FILENAME|INSTALLPATH"
// Example (Overwatch 47161): "#FILEID|MD5|CHUNK_ID|PRIORITY|MPRIORITY|FILENAME|INSTALLPATH"

// Parses a CSV file header line and retrieves index of a variable
int CSV_GetHeaderIndex(const char * szLinePtr, const char * szLineEnd, const char * szVariableName, int * PtrIndex)
{
    size_t nLength = strlen(szVariableName);
    int nIndex = 0;

    // Skip the hashtag at the beginning
    while((szLinePtr < szLineEnd) && (szLinePtr[0] == '#'))
        szLinePtr++;

    // Parse the line header
    while(szLinePtr < szLineEnd)
    {
        // Check the variable there
        if(!_strnicmp(szLinePtr, szVariableName, nLength))
        {
            // Does the length match?
            if(szLinePtr[nLength] == '|' || szLinePtr[nLength] == '0')
            {
                PtrIndex[0] = nIndex;
                return ERROR_SUCCESS;
            }
        }

        // Get the next variable
        szLinePtr = SkipInfoVariable(szLinePtr, szLineEnd);
        if(szLinePtr == NULL)
            break;
        nIndex++;
    }

    return ERROR_BAD_FORMAT;
}

// Parses CSV line and returns a file name and CKey
int CSV_GetNameAndCKey(const char * szLinePtr, const char * szLineEnd, int nFileNameIndex, int nCKeyIndex, char * szFileName, size_t nMaxChars, PCONTENT_KEY pCKey)
{
    char * szFileNameEnd = szFileName + nMaxChars - 1;
    int nVarsRetrieved = 0;
    int nVarIndex = 0;

    // Parse the entire line. Note that the line is not zero-terminated
    while(szLinePtr < szLineEnd && nVarsRetrieved < 2)
    {
        const char * szVarBegin = szLinePtr;
        const char * szVarEnd;

        // Get the variable span
        while(szLinePtr < szLineEnd && szLinePtr[0] != '|')
            szLinePtr++;
        szVarEnd = szLinePtr;

        // Check the variable indices
        if(nVarIndex == nFileNameIndex)
        {
            // Check the length of the variable
            if(szVarBegin >= szVarEnd)
                return ERROR_BAD_FORMAT;

            // Copy the file name
            while(szVarBegin < szVarEnd && szFileName < szFileNameEnd)
            {
                if(szVarBegin[0] == 0)
                    return ERROR_BAD_FORMAT;
                *szFileName++ = *szVarBegin++;
            }

            // Increment the number of variables retrieved
            szFileName[0] = 0;
            nVarsRetrieved++;
        }

        // Check the index of the CKey
        if(nVarIndex == nCKeyIndex)
        {
            // Check the length
            if((szVarEnd - szVarBegin) != MD5_STRING_SIZE)
                return ERROR_BAD_FORMAT;

            // Convert the CKey
            if(ConvertStringToBinary(szVarBegin, MD5_STRING_SIZE, pCKey->Value) != ERROR_SUCCESS)
                return ERROR_BAD_FORMAT;

            // Increment the number of variables retrieved
            nVarsRetrieved++;
        }

        // Skip the separator, if any
        if(szLinePtr < szLineEnd && szLinePtr[0] == '|')
            szLinePtr++;
        nVarIndex++;
    }

    return (nVarsRetrieved == 2) ? ERROR_SUCCESS : ERROR_BAD_FORMAT;
}
