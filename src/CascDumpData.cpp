/*****************************************************************************/
/* CascDumpData.cpp                       Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* System-dependent directory functions for CascLib                          */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 07.05.14  1.00  Lad  The first version of CascDumpData.cpp                */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "CascLib.h"
#include "CascCommon.h"

#ifdef _DEBUG       // The entire feature is only valid for debug purposes

//-----------------------------------------------------------------------------
// Forward definitions

void DumpEncodingFile(TCascStorage * hs, FILE * fp);
void DumpEKeyEntries(TCascStorage * hs, FILE * fp);

//-----------------------------------------------------------------------------
// Local functions

static char * StringFromLPTSTR(const TCHAR * szString, char * szBuffer, size_t cchBuffer)
{
    char * szSaveBuffer = szBuffer;
    char * szBufferEnd = szBuffer + cchBuffer - 1;

    while (szBuffer < szBufferEnd && szString[0] != 0)
        *szBuffer++ = (char)*szString++;
    szBuffer[0] = 0;

    return szSaveBuffer;
}

/*
void CascDumpSparseArray(const char * szFileName, void * pvSparseArray)
{
    TSparseArray * pSparseArray = (TSparseArray *)pvSparseArray;
    FILE * fp;

    // Create the dump file
    fp = fopen(szFileName, "wt");
    if(fp != NULL)
    {
        // Write header
        fprintf(fp, "##   Value\n--   -----\n");

        // Write the values
        for(DWORD i = 0; i < pSparseArray->TotalItemCount; i++)
        {
            DWORD Value = 0;

            if(pSparseArray->IsItemPresent(i))
            {
                Value = pSparseArray->GetItemValue(i);
                fprintf(fp, "%02X    %02X\n", i, Value);
            }
            else
            {
                fprintf(fp, "%02X    --\n", i);
            }
        }

        fclose(fp);
    }
}

void CascDumpNameFragTable(const char * szFileName, void * pMarFile)
{
    TFileNameDatabase * pDB = ((PMAR_FILE)pMarFile)->pDatabasePtr->pDB;
    FILE * fp;

    // Create the dump file
    fp = fopen(szFileName, "wt");
    if(fp != NULL)
    {
        PNAME_FRAG pNameTable = pDB->NameFragTable.ItemArray;
        const char * szNames = pDB->IndexStruct_174.ItemArray;
        const char * szLastEntry;
        char szMatchType[0x100];

        // Dump the table header
        fprintf(fp, "Indx  ThisHash NextHash FragOffs\n");
        fprintf(fp, "----  -------- -------- --------\n");

        // Dump all name entries
        for(DWORD i = 0; i < pDB->NameFragTable.ItemCount; i++)
        {
            // Reset both match types
            szMatchType[0] = 0;
            szLastEntry = "";

            // Only if the table entry is not empty
            if(pNameTable->ItemIndex != 0xFFFFFFFF)
            {
                // Prepare the entry
                if(IS_SINGLE_CHAR_MATCH(pDB->NameFragTable, i))
                    sprintf(szMatchType, "SINGLE_CHAR (\'%c\')", (pNameTable->FragOffs & 0xFF));
                else
                    sprintf(szMatchType, "NAME_FRAGMT (\"%s\")", szNames + pNameTable->FragOffs);
            }

            // Dump the entry
            fprintf(fp, "0x%02X  %08x %08x %08x %s%s\n", i, pNameTable->ItemIndex,
                                                            pNameTable->NextIndex,
                                                            pNameTable->FragOffs,
                                                            szMatchType,
                                                            szLastEntry);
            pNameTable++;
        }
        fclose(fp);
    }
}

void CascDumpFileNames(const char * szFileName, void * pvMarFile)
{
    TMndxFindResult Struct1C;
    PMAR_FILE pMarFile = (PMAR_FILE)pvMarFile;
    FILE * fp;
    char szNameBuff[0x200];
    bool bFindResult;

    // Create the dump file
    fp = fopen(szFileName, "wt");
    if(fp != NULL)
    {
        // Set an empty path as search mask (?)
        Struct1C.SetSearchPath("", 0);

        // Keep searching
        for(;;)
        {
            // Search the next file name
            pMarFile->pDatabasePtr->sub_1956CE0(&Struct1C, &bFindResult);

            // Stop the search in case of failure
            if(!bFindResult)
                break;

            // Printf the found file name
            memcpy(szNameBuff, Struct1C.szFoundPath, Struct1C.cchFoundPath);
            szNameBuff[Struct1C.cchFoundPath] = 0;
            fprintf(fp, "%s\n", szNameBuff);
        }

        fclose(fp);
    }

    // Free the search structures
    Struct1C.FreeStruct40();
}
*/

// Either opens a dump file or returns "stdout"
static FILE * OpenDumpFile(const char * szDumpFile)
{
    if(szDumpFile != NULL)
    {
        return fopen(szDumpFile, "wt");
    }
    else
    {
        return stdout;
    }
}

// Closes the dump file if it was open by OpenDumpFile
static void CloseDumpFile(const char * szDumpFile, FILE * fp)
{
    if(szDumpFile != NULL && fp != NULL)
        fclose(fp);
}

//-----------------------------------------------------------------------------
// Public functions

void CascDumpFile(const char * szDumpFile, HANDLE hFile)
{
    FILE * fp;
    DWORD dwBytesRead = 1;
    DWORD dwFilePos = CascSetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    BYTE Buffer[0x1000];

    // Create/open the dump file
    fp = OpenDumpFile(szDumpFile);
    if(fp != NULL)
    {
        // Read data as long as we can, write as long as we can
        while(dwBytesRead != 0)
        {
            // Read from the source file
            if(!CascReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead))
                break;

            // Write to the destination file
            if(fwrite(Buffer, 1, dwBytesRead, fp) != dwBytesRead)
                break;
        }

        // Restore the file pointer
        CascSetFilePointer(hFile, dwFilePos, NULL, FILE_BEGIN);

        // Close the dump file
        CloseDumpFile(szDumpFile, fp);
    }
}

void CascDumpStorage(HANDLE hStorage, const char * szDumpFile)
{
    TCascStorage * hs;
    FILE * fp = stdout;
    char szStringBuff[0x800];

    // Verify the storage handle
    hs = IsValidCascStorageHandle(hStorage);
    if(hs == NULL)
        return;

    // Create/open the dump file
    fp = OpenDumpFile(szDumpFile);
    if(fp != NULL)
    {
        // Dump the basic storage info
        fprintf(fp, "=== Basic Storage Info ======================================================\n");
        fprintf(fp, "DataPath:  %s\n", StringFromLPTSTR(hs->szDataPath, szStringBuff, sizeof(szStringBuff)));
        fprintf(fp, "IndexPath: %s\n", StringFromLPTSTR(hs->szIndexPath, szStringBuff, sizeof(szStringBuff)));
        fprintf(fp, "CDN List:  %s\n", StringFromLPTSTR(hs->szCdnList, szStringBuff, sizeof(szStringBuff)));
        fprintf(fp, "BuildFile: %s\n", StringFromLPTSTR(hs->szBuildFile, szStringBuff, sizeof(szStringBuff)));
        fprintf(fp, "CDN Config Key: %s\n", StringFromBinary(hs->CdnConfigKey.pbData, hs->CdnConfigKey.cbData, szStringBuff));
        fprintf(fp, "CDN Build Key:  %s\n", StringFromBinary(hs->CdnBuildKey.pbData, hs->CdnBuildKey.cbData, szStringBuff));
        fprintf(fp, "Archives Key:   %s\n", StringFromBinary(hs->ArchivesKey.pbData, hs->ArchivesKey.cbData, szStringBuff));
        fprintf(fp, "ROOT file:      %s\n", StringFromBinary(hs->RootFile.CKey, CASC_CKEY_SIZE, szStringBuff));
        fprintf(fp, "PATCH file:     %s\n", StringFromBinary(hs->PatchFile.CKey, CASC_CKEY_SIZE, szStringBuff));
        fprintf(fp, "ENCODING file:  %s\n", StringFromBinary(hs->EncodingFile.CKey, MD5_HASH_SIZE, szStringBuff));
        fprintf(fp, "DOWNLOAD file:  %s\n", StringFromBinary(hs->DownloadFile.CKey, MD5_HASH_SIZE, szStringBuff));
        fprintf(fp, "INSTALL file:   %s\n\n", StringFromBinary(hs->InstallFile.CKey, MD5_HASH_SIZE, szStringBuff));

        // Dump the complete ENCODING file
        DumpEncodingFile(hs, fp);

        // Dump the list of encoded key entries
        DumpEKeyEntries(hs, fp);

        // Close the dump file
        CloseDumpFile(szDumpFile, fp);
    }
}

#endif // _DEBUG
