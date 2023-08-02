/*****************************************************************************/
/* CascRootFile_Text.cpp                  Copyright (c) Ladislav Zezula 2017 */
/*---------------------------------------------------------------------------*/
/* Support for loading ROOT files in plain text                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 28.10.15  1.00  Lad  The first version of CascRootFile_Text.cpp           */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

// Implemented in "overwatch/apm.cpp"
DWORD LoadApplicationPackageManifestFile(TCascStorage * hs, CASC_FILE_TREE & FileTree, PCASC_CKEY_ENTRY pCKeyEntry, const char * szApmFileName);

// Implemented in "overwatch/cmf.cpp"
DWORD LoadContentManifestFile(TCascStorage * hs, CASC_FILE_TREE & FileTree, PCASC_CKEY_ENTRY pCKeyEntry, const char * szFileName);

//-----------------------------------------------------------------------------
// Structure definitions for APM files

typedef struct _APM_HEADER_V3
{
    ULONGLONG BuildNumber;              // Build number of the game
    ULONGLONG ZeroValue1;
    DWORD ZeroValue2;
    DWORD PackageCount;
    DWORD ZeroValue3;
    DWORD EntryCount;
    DWORD Checksum;

    // Followed by the array of APM_ENTRY (count is in "EntryCount")
    // Followed by the array of APM_PACKAGE (count is in "PackageCount")

} APM_HEADER_V3, * PAPM_HEADER_V3;

typedef struct _APM_HEADER_V2
{
    ULONGLONG BuildNumber;              // Build number of the game
    ULONGLONG ZeroValue1;
    DWORD PackageCount;
    DWORD ZeroValue2;
    DWORD EntryCount;
    DWORD Checksum;

    // Followed by the array of APM_ENTRY (count is in "EntryCount")
    // Followed by the array of APM_PACKAGE (count is in "PackageCount")

} APM_HEADER_V2, * PAPM_HEADER_V2;

typedef struct _APM_HEADER_V1
{
    ULONGLONG BuildNumber;              // Build number of the game
    DWORD BuildVersion;
    DWORD PackageCount;
    DWORD EntryCount;
    DWORD Checksum;

    // Followed by the array of APM_ENTRY (count is in "EntryCount")
    // Followed by the array of APM_PACKAGE (count is in "PackageCount")

} APM_HEADER_V1, * PAPM_HEADER_V1;

// On-disk format, size = 0x0C
typedef struct _APM_ENTRY_V1
{
    DWORD     Index;
    DWORD     HashA_Lo;                     // Must split the hashes in order to make this structure properly aligned
    DWORD     HashA_Hi;
} APM_ENTRY_V1, * PAPM_ENTRY_V1;

// On-disk format, size = 0x14
typedef struct _APM_ENTRY_V2
{
    DWORD     Index;
    DWORD     HashA_Lo;                     // Must split the hashes in order to make this structure properly aligned
    DWORD     HashA_Hi;
    DWORD     HashB_Lo;
    DWORD     HashB_Hi;
} APM_ENTRY_V2, *PAPM_ENTRY_V2;

// On-disk format
typedef struct _APM_PACKAGE_ENTRY_V1
{
    ULONGLONG EntryPointGUID;               // virtual most likely
    ULONGLONG PrimaryGUID;                  // real
    ULONGLONG SecondaryGUID;                // real
    ULONGLONG Key;                          // encryption
    ULONGLONG PackageGUID;                  // 077 file
    ULONGLONG Unknown1;
    DWORD Unknown2;
} APM_PACKAGE_ENTRY_V1, * PAPM_PACKAGE_ENTRY_V1;

// On-disk format
typedef struct _APM_PACKAGE_ENTRY_V2
{
    ULONGLONG PackageGUID;                  // 077 file
    ULONGLONG Unknown1;
    DWORD Unknown2;
    DWORD Unknown3;
    ULONGLONG Unknown4;
} APM_PACKAGE_ENTRY_V2, *PAPM_PACKAGE_ENTRY_V2;

//-----------------------------------------------------------------------------
// Handler classes

static bool IsManifestFolderName(const char * szFileName, const char * szManifestFolder, size_t nLength)
{
    if(!_strnicmp(szFileName, szManifestFolder, nLength))
    {
        return (szFileName[nLength] == '\\' || szFileName[nLength] == '/');
    }
    return false;
}

static bool IsSpecialContentFile(const char * szFileName, const char * szExtension)
{
    if(IsManifestFolderName(szFileName, "Manifest", 8) || IsManifestFolderName(szFileName, "TactManifest", 12))
    {
        if(!_stricmp(GetFileExtension(szFileName), szExtension))
        {
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
// Handler definition for OVERWATCH root file
//
// -------------------------------------
// Overwatch ROOT file (build 24919):
// -------------------------------------
// #MD5|CHUNK_ID|FILENAME|INSTALLPATH
// FE3AD8A77EEF77B383DF4929AED816FD|0|RetailClient/GameClientApp.exe|GameClientApp.exe
// 5EDDEFECA544B6472C5CD52BE63BC02F|0|RetailClient/Overwatch Launcher.exe|Overwatch Launcher.exe
// 6DE09F0A67F33F874F2DD8E2AA3B7AAC|0|RetailClient/ca-bundle.crt|ca-bundle.crt
// 99FE9EB6A4BB20209202F8C7884859D9|0|RetailClient/ortp_x64.dll|ortp_x64.dll
//
// -------------------------------------
// Overwatch ROOT file (build 47161):
// -------------------------------------
// #FILEID|MD5|CHUNK_ID|PRIORITY|MPRIORITY|FILENAME|INSTALLPATH
// RetailClient/Overwatch.exe|807F96661280C07E762A8C129FEBDA6F|0|0|255|RetailClient/Overwatch.exe|Overwatch.exe
// RetailClient/Overwatch Launcher.exe|5EDDEFECA544B6472C5CD52BE63BC02F|0|0|255|RetailClient/Overwatch Launcher.exe|Overwatch Launcher.exe
// RetailClient/ortp_x64.dll|7D1B5DEC267480F3E8DAD6B95143A59C|0|0|255|RetailClient/ortp_x64.dll|ortp_x64.dll
//

struct TRootHandler_OW : public TFileTreeRoot
{
    TRootHandler_OW() : TFileTreeRoot(0)
    {
        // We have file names and return CKey as result of search
        dwFeatures |= (CASC_FEATURE_FILE_NAMES | CASC_FEATURE_ROOT_CKEY);
    }

    int Load(TCascStorage * hs, CASC_CSV & Csv, size_t nFileNameIndex, size_t nCKeyIndex)
    {
        PCASC_CKEY_ENTRY pCKeyEntry;
        size_t nFileCount;
        BYTE CKey[MD5_HASH_SIZE];

        CASCLIB_UNUSED(hs);

        // Keep loading every line until there is something
        while(Csv.LoadNextLine())
        {
            const CASC_CSV_COLUMN & FileName = Csv[CSV_ZERO][nFileNameIndex];
            const CASC_CSV_COLUMN & CKeyStr = Csv[CSV_ZERO][nCKeyIndex];

            // Retrieve the file name and the content key
            if(FileName.szValue && CKeyStr.szValue && CKeyStr.nLength == MD5_STRING_SIZE)
            {
                // Convert the string CKey to binary
                if(BinaryFromString(CKeyStr.szValue, MD5_STRING_SIZE, CKey) == ERROR_SUCCESS)
                {
                    // Find the item in the tree
                    if((pCKeyEntry = FindCKeyEntry_CKey(hs, CKey)) != NULL)
                    {
                        // Insert the file name and the CKey into the tree
                        FileTree.InsertByName(pCKeyEntry, FileName.szValue);
                    }
                }
            }
        }

        // Get the total file count that we loaded so far
        nFileCount = FileTree.GetCount();

        // Parse Content Manifest Files (.cmf)
        for(size_t i = 0; i < nFileCount; i++)
        {
            PCASC_FILE_NODE pFileNode;
            char szFileName[MAX_PATH];

            // Get the n-th file
            pFileNode = (PCASC_FILE_NODE)FileTree.PathAt(szFileName, _countof(szFileName), i);
            if(pFileNode != NULL)
            {
                if(IsSpecialContentFile(szFileName, ".cmf"))
                {
                    LoadContentManifestFile(hs, FileTree, pFileNode->pCKeyEntry, szFileName);
                    continue;
                }
                //if(IsSpecialContentFile(szFileName, ".apm"))
                //{
                //    LoadApplicationPackageManifestFile(hs, FileTree, pFileNode->pCKeyEntry, szFileName);
                //}
            }
        }
        return ERROR_SUCCESS;
    }
};

//-----------------------------------------------------------------------------
// Public functions

// TODO: There is way more files in the Overwatch CASC storage than present in the ROOT file.
DWORD RootHandler_CreateOverwatch(TCascStorage * hs, CASC_BLOB & RootFile)
{
    TRootHandler_OW * pRootHandler = NULL;
    CASC_CSV Csv(0, true);
    size_t Indices[2];
    DWORD dwErrCode;

    // Load the ROOT file
    dwErrCode = Csv.Load(RootFile.pbData, RootFile.cbData);
    if(dwErrCode == ERROR_SUCCESS)
    {
        // Retrieve the indices of the file name and MD5 columns
        Indices[0] = Csv.GetColumnIndex("FILENAME");
        Indices[1] = Csv.GetColumnIndex("MD5");

        // If both indices were found OK, then load the root file
        if(Indices[0] != CSV_INVALID_INDEX && Indices[1] != CSV_INVALID_INDEX)
        {
            pRootHandler = new TRootHandler_OW();
            if(pRootHandler != NULL)
            {
                // Load the root directory. If load failed, we free the object
                dwErrCode = pRootHandler->Load(hs, Csv, Indices[0], Indices[1]);
                if(dwErrCode != ERROR_SUCCESS)
                {
                    delete pRootHandler;
                    pRootHandler = NULL;
                }
            }
        }
        else
        {
            dwErrCode = ERROR_BAD_FORMAT;
        }
    }

    // Assign the root directory (or NULL) and return error
    hs->pRootHandler = pRootHandler;
    return dwErrCode;
}
