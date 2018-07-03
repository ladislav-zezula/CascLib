/*****************************************************************************/
/* Csv.h                                  Copyright (c) Ladislav Zezula 2018 */
/*---------------------------------------------------------------------------*/
/* Implementation for the CSV handler class                                  */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 30.06.18  1.00  Lad  Created                                              */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Local defines

#define INVALID_COLUMNS  ((size_t)-1)

//-----------------------------------------------------------------------------
// Constructor and destructor

CASC_CSV::CASC_CSV()
{
    memset(this, 0, sizeof(CASC_CSV));
    nColumns = INVALID_COLUMNS;
}

CASC_CSV::~CASC_CSV()
{
    nColumns = INVALID_COLUMNS;
}

//-----------------------------------------------------------------------------
// Loading headers

size_t CASC_CSV::LoadElements(PCSV_ELEMENT pElements, size_t nMaxElements, const char * szLinePtr, const char * szLineEnd)
{
    size_t nCount = 0;

    // Parse the entire line
    while (szLinePtr < szLineEnd)
    {
        const char * szString = szLinePtr;

        // Verify overflow
        if (nCount >= nMaxElements)
            return 0;

        // Find the end of the element
        while (szLinePtr < szLineEnd && szLinePtr[0] != '|')
            szLinePtr++;

        // Put the pointer and length of the element
        pElements[nCount].szString = szString;
        pElements[nCount].nLength = (szLinePtr - szString);
        nCount++;

        // Skip the end of the element
        if (szLinePtr < szLineEnd && szLinePtr[0] == '|')
            szLinePtr++;
    }

    // Save the number of elements, if not done yet
    if (nColumns == INVALID_COLUMNS)
        nColumns = nCount;
    return nCount;
}

size_t CASC_CSV::LoadHeader(const char * szLinePtr, const char * szLineEnd)
{
    return LoadElements(Headers, MAX_CSV_ELEMENTS, szLinePtr, szLineEnd);
}

size_t CASC_CSV::LoadHeader(void * pvListFile)
{
    const char * szLineBegin;
    const char * szLineEnd;
    size_t nCount = 0;

    ListFile_GetNextLine(pvListFile, &szLineBegin, &szLineEnd);
    if (szLineBegin < szLineEnd)
    {
        nCount = LoadHeader(szLineBegin, szLineEnd);
    }

    return nCount;
}

bool CASC_CSV::GetColumnIndices(size_t * Indices, ...)
{
    const char * szColumnName;
    va_list argList;
    bool bResult = true;

    va_start(argList, Indices);
    while ((szColumnName = va_arg(argList, const char *)) != NULL)
    {
        size_t nLength = strlen(szColumnName);
        size_t nIndex = INVALID_COLUMNS;

        // Search the array of fields
        for (size_t i = 0; i < nColumns; i++)
        {
            if (nLength == Headers[i].nLength && _strnicmp(Headers[i].szString, szColumnName, nLength) == 0)
            {
                nIndex = i;
                break;
            }
        }

        // Fill the index
        if (nIndex == INVALID_COLUMNS)
            bResult = false;
        *Indices++ = nIndex;
    }
    va_end(argList);
    return bResult;
}

size_t CASC_CSV::LoadNextLine(const char * szLinePtr, const char * szLineEnd)
{
    size_t nCount;

    // Retrieve as much items as possible
    nCount = LoadElements(Columns, MAX_CSV_ELEMENTS, szLinePtr, szLineEnd);

    // If there are some missing, we need to set them as 0
    if (nColumns != INVALID_COLUMNS)
    {
        while (nCount < nColumns)
        {
            Columns[nCount].szString = szLineEnd;
            Columns[nCount].nLength = 0;
            nCount++;
        }
    }

    return (nCount == nColumns) ? nColumns : 0;
}

size_t CASC_CSV::LoadNextLine(void * pvListFile)
{
    const char * szLineBegin;
    const char * szLineEnd;
    size_t nCount = 0;

    if (ListFile_GetNextLine(pvListFile, &szLineBegin, &szLineEnd))
    {
        nCount = LoadNextLine(szLineBegin, szLineEnd);
    }

    return nCount;
}

int CASC_CSV::GetString(char * szBufferPtr, size_t nMaxChars, size_t Index)
{
    const char * szStringPtr;
    const char * szStringEnd;
    char * szBufferEnd = szBufferPtr + nMaxChars - 1;

    // Check for index overflow and buffer overflow
    if (Index >= nColumns)
        return ERROR_INSUFFICIENT_BUFFER;
    if (Columns[Index].nLength > nMaxChars - 1)
        return ERROR_INSUFFICIENT_BUFFER;
    
    // Get the source string
    szStringPtr = Columns[Index].szString;
    szStringEnd = szStringPtr + Columns[Index].nLength;

    // Copy the string
    while (szStringPtr < szStringEnd && szBufferPtr < szBufferEnd)
        *szBufferPtr++ = *szStringPtr++;
    szBufferPtr[0] = 0;
    return ERROR_SUCCESS;
}

TCHAR * CASC_CSV::GetString(size_t Index)
{
    TCHAR * szString;
    size_t nLength;

    // Check for index overflow
    if (Index >= nColumns)
        return NULL;
    nLength = Columns[Index].nLength;

    // Allocate buffer for string. Make it double-zero-terminated
    // in order to be multi-SZ as well
    szString = CASC_ALLOC(TCHAR, nLength + 2);
    if (szString != NULL)
    {
        CopyString(szString, Columns[Index].szString, nLength);
        szString[nLength + 1] = 0;
    }

    return szString;
}

int CASC_CSV::GetBinary(LPBYTE pbBuffer, size_t nMaxBytes, size_t Index)
{
    // Check for index overflow and buffer overflow
    if (Index >= nColumns)
        return ERROR_INSUFFICIENT_BUFFER;
    if (Columns[Index].nLength != nMaxBytes * 2)
        return ERROR_INSUFFICIENT_BUFFER;

    // Convert the string to binary
    return ConvertStringToBinary(Columns[Index].szString, nMaxBytes * 2, pbBuffer);
}

int CASC_CSV::GetData(QUERY_KEY & Key, size_t Index, bool bHexaValue)
{
    // Check for index overflow
    if (Index >= nColumns)
        return ERROR_INSUFFICIENT_BUFFER;

    // Allocate space for the blob
    if (bHexaValue)
    {
        // Allocate space
        Key.pbData = CASC_ALLOC(BYTE, Columns[Index].nLength / 2);
        if (Key.pbData == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Convert the hexa string to binary string
        Key.cbData = Columns[Index].nLength / 2;
        return ConvertStringToBinary(Columns[Index].szString, Columns[Index].nLength, Key.pbData);
    }
    else
    {
        size_t nLength = Columns[Index].nLength;

        // Initialize the blob
        Key.pbData = CASC_ALLOC(BYTE, nLength + 1);
        if (Key.pbData == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Copy the string
        memcpy(Key.pbData, Columns[Index].szString, nLength);
        Key.pbData[nLength] = 0;
        Key.cbData = nLength;
        return ERROR_SUCCESS;
    }
}
