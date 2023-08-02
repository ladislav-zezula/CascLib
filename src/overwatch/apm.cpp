/*****************************************************************************/
/* apm.cpp                                Copyright (c) Ladislav Zezula 2023 */
/*---------------------------------------------------------------------------*/
/* Support for Application Package Manifest (.apm)                           */
/* Know-how from https://github.com/overtools/TACTLib                        */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 02.08.23  1.00  Lad  Created                                              */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Structures related to the APM files

#define CASC_APM_HEADER_MAGIC       0x00636D66

typedef struct _CASC_APM_HEADER_V3
{
    ULONGLONG BuildNumber;              // Build number of the game
    ULONGLONG ZeroValue1;
    DWORD ZeroValue2;
    DWORD PackageCount;
    DWORD ZeroValue3;
    DWORD EntryCount;
    DWORD HeaderMagic;

    // Followed by the array of APM_ENTRY (count is in "EntryCount")
    // Followed by the array of APM_PACKAGE (count is in "PackageCount")

} CASC_APM_HEADER_V3, *PCASC_APM_HEADER_V3;

typedef struct _CASC_APM_HEADER_V2
{
    ULONGLONG BuildVersion;             // Build number of the game
    ULONGLONG ZeroValue1;
    DWORD PackageCount;
    DWORD ZeroValue2;
    DWORD EntryCount;
    DWORD HeaderMagic;

    // Followed by the array of APM_ENTRY (count is in "EntryCount")
    // Followed by the array of APM_PACKAGE (count is in "PackageCount")

} CASC_APM_HEADER_V2, *PCASC_APM_HEADER_V2;

typedef struct _CASC_APM_HEADER_V1
{
    ULONGLONG BuildVersion;              
    DWORD BuildNumber;                   // Build number of the game
    DWORD PackageCount;
    DWORD EntryCount;
    DWORD HeaderMagic;

    // Followed by the array of APM_ENTRY (count is in "EntryCount")
    // Followed by the array of APM_PACKAGE (count is in "PackageCount")

} CASC_APM_HEADER_V1, *PCASC_APM_HEADER_V1;

typedef struct _CASC_APM_HEADER
{
    ULONGLONG BuildNumber;
    DWORD PackageCount;
    DWORD EntryCount;
    DWORD HeaderMagic;
} CASC_APM_HEADER, *PCASC_APM_HEADER;

//-----------------------------------------------------------------------------
// Local functions

static LPBYTE CaptureApmHeader(TCascStorage * hs, LPBYTE pbDataPtr, LPBYTE pbDataEnd, CASC_APM_HEADER & ApmHeader)
{
    PCASC_APM_HEADER_V3 pHeader_V3 = NULL;
    PCASC_APM_HEADER_V2 pHeader_V2 = NULL;
    PCASC_APM_HEADER_V1 pHeader_V1 = NULL;

    // Build 47161
    if(CaptureStructure<CASC_APM_HEADER_V3>(pbDataPtr, pbDataEnd, &pHeader_V3) != NULL)
    {
        if(pHeader_V3->BuildNumber == hs->dwBuildNumber &&
           pHeader_V3->ZeroValue1 == 0 &&
           pHeader_V3->ZeroValue2 == 0 &&
           pHeader_V3->ZeroValue3 == 0)
        {
            ApmHeader.BuildNumber  = pHeader_V3->BuildNumber;
            ApmHeader.PackageCount = pHeader_V3->PackageCount;
            ApmHeader.PackageCount = pHeader_V3->PackageCount;
            ApmHeader.EntryCount   = pHeader_V3->EntryCount;
            ApmHeader.HeaderMagic  = pHeader_V3->HeaderMagic;
            return pbDataPtr + sizeof(CASC_APM_HEADER_V3);
        }
    }
/*
    if(CaptureStructure<CASC_APM_HEADER_V2>(pbDataPtr, pbDataEnd, &pHeader_V2) != NULL)
    {

    }
*/

    // Build 24919
    if(CaptureStructure<CASC_APM_HEADER_V1>(pbDataPtr, pbDataEnd, &pHeader_V1) != NULL)
    {
        if((pHeader_V1->HeaderMagic & 0x00FFFFFF) == CASC_APM_HEADER_MAGIC)
        {
            ApmHeader.BuildNumber  = pHeader_V1->BuildNumber;
            ApmHeader.PackageCount = pHeader_V1->PackageCount;
            ApmHeader.PackageCount = pHeader_V1->PackageCount;
            ApmHeader.EntryCount   = pHeader_V1->EntryCount;
            ApmHeader.HeaderMagic  = pHeader_V1->HeaderMagic;
            return pbDataPtr + sizeof(CASC_APM_HEADER_V1);
        }
    }
    return NULL;
}


//-----------------------------------------------------------------------------
// Public functions

DWORD LoadApplicationPackageManifestFile(TCascStorage * hs, CASC_FILE_TREE & FileTree, PCASC_CKEY_ENTRY pCKeyEntry, const char * szApmFileName)
{
    CASC_BLOB ApmFile;
    const char * szApmPlainName = GetPlainFileName(szApmFileName);
    DWORD dwErrCode;

    //if(!_stricmp(szCmfPlainName, "Win_SPWin_RDEV_EExt.cmf"))
    //    __debugbreak();

    // Load the entire internal file to memory
    if((dwErrCode = LoadInternalFileToMemory(hs, pCKeyEntry, ApmFile)) == ERROR_SUCCESS)
    {
        CASC_APM_HEADER ApmHeader = {0};
        LPBYTE pbDataEnd = ApmFile.pbData + ApmFile.cbData;
        LPBYTE pbDataPtr = ApmFile.pbData;

        // Capture the header
        if((pbDataPtr = CaptureApmHeader(hs, pbDataPtr, pbDataEnd, ApmHeader)) == NULL)
            return ERROR_BAD_FORMAT;


    }
    return ERROR_SUCCESS;
}
